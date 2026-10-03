#include "trace_route.h"
#include <math.h>
#include <string.h>

#define MAX_PTS 96
#define MAX_BUS 4

static int cell_free(const TraceGrid *g, int c, int r)
{
    if (r < 0 || r >= g->rows) return 0;
    if (c < 0 || c >= g->cols) return 1;   // margins outside the face are free
    return !((g->occ[r] >> c) & 1u);
}
static int cell_in(const TraceGrid *g, int c, int r) { return c >= 0 && c < g->cols && r >= 0 && r < g->rows; }
static void take(TraceGrid *g, int c, int r) { if (cell_in(g, c, r)) g->occ[r] |= (uint64_t)1 << c; }

// All of columns [c-1, c+w] free at row r (bus plus one margin column each side).
static int band_free(const TraceGrid *g, int c, int w, int r)
{
    for (int i = -1; i <= w; i++) if (!cell_free(g, c + i, r)) return 0;
    return 1;
}

int trace_grid_init(TraceGrid *g, const TraceFace *f, float pitch)
{
    g->cols = (int)floor(f->w / pitch) - 1;
    g->rows = (int)floor(f->h / pitch);
    if (g->cols > TRACE_MAX_COLS) g->cols = TRACE_MAX_COLS;
    if (g->rows > TRACE_MAX_ROWS) g->rows = TRACE_MAX_ROWS;
    if (g->cols < 3 || g->rows < 6) return 0;
    memset(g->occ, 0, sizeof(uint64_t) * (size_t)g->rows);
    g->u0 = (f->w - (g->cols - 1) * (double)pitch) * 0.5;
    return 1;
}

void trace_grid_block(TraceGrid *g, int c0, int c1, int r0, int r1)
{
    for (int r = r0 - 1; r <= r1 + 1; r++) for (int c = c0 - 1; c <= c1 + 1; c++) take(g, c, r);
}

static Vector3 face_point(const TraceFace *f, double u, double v, double off, DVec3 origin)
{
    DVec3 p = dv_add(dv_add(f->o, dv_scale(f->u, u)), dv_add(dv_scale(f->v, v), dv_scale(f->n, off)));
    return dv_to_v3(dv_sub(p, origin));
}

static void emit_trace(const TraceRouteParams *p, const TraceFace *f, const TraceGrid *g, const int (*pt)[2], int np,
                       DVec3 origin, LineGeo *out, LineAttr at)
{
    if (np < 2) return;
    Vector3 pts[MAX_PTS], acr[MAX_PTS];
    for (int i = 0; i < np; i++) {
        double u = g->u0 + pt[i][0] * p->pitch, v = (pt[i][1] + 0.5) * p->pitch;
        pts[i] = face_point(f, u, v, p->faceOffset, origin);
        // In-plane direction at the point (average of the adjacent segments), across = its normal.
        int a = i > 0 ? i - 1 : i, b = i < np - 1 ? i + 1 : i;
        double du = pt[b][0] - pt[a][0], dv = pt[b][1] - pt[a][1];
        double l = sqrt(du * du + dv * dv);
        if (l < 1e-9) { du = 0.0; dv = 1.0; l = 1.0; }
        du /= l; dv /= l;
        DVec3 across = dv_add(dv_scale(f->u, dv), dv_scale(f->v, -du));
        acr[i] = dv_to_v3(dv_norm(across));
    }
    lg_planar_polyline(out, pts, acr, np, p->width, at);
}

static void emit_via(const TraceRouteParams *p, const TraceFace *f, const TraceGrid *g, int c, int r, DVec3 origin, LineGeo *out, LineAttr at)
{
    enum { SEG = 8 };
    Vector3 pts[SEG + 1], acr[SEG + 1];
    double uc = g->u0 + c * p->pitch, vc = (r + 0.5) * p->pitch, rad = p->viaRadius;
    for (int i = 0; i <= SEG; i++) {
        double a = 6.283185307179586 * i / SEG;
        double du = cos(a), dv = sin(a);
        pts[i] = face_point(f, uc + du * rad, vc + dv * rad, p->faceOffset, origin);
        acr[i] = dv_to_v3(dv_add(dv_scale(f->u, du), dv_scale(f->v, dv)));
    }
    lg_planar_polyline(out, pts, acr, SEG + 1, (float)(rad * 0.6), at);
}

// One bus of w traces starting at column c, row r. Returns 1 if anything was drawn.
static int route_bus(const TraceRouteParams *p, const TraceFace *f, TraceGrid *g, Pcg32 *r, int c, int row, int w,
                     DVec3 origin, LineGeo *out, LineAttr at, LineAttr via)
{
    if (!band_free(g, c, w, row)) return 0;
    static int pts[MAX_BUS][MAX_PTS][2];
    int np[MAX_BUS] = { 0 }, alive[MAX_BUS];
    for (int i = 0; i < w; i++) {
        alive[i] = 1;
        pts[i][0][0] = c + i; pts[i][0][1] = row; np[i] = 1;
        take(g, c + i, row);
    }
    int startVia = row > 0;   // traces that start mid-face begin at a via
    int startRow = row;
    int nAlive = w;
    for (int iter = 0; iter < 40 && nAlive > 0; iter++) {
        int run = pcg32_int(r, 3, 14), k = 0;
        while (k < run && band_free(g, c, w, row + k + 1)) k++;
        if (k < 2) break;
        for (int i = 0; i < w; i++) if (alive[i]) for (int y = 1; y <= k; y++) take(g, c + i, row + y);
        row += k;
        for (int i = 0; i < w; i++) if (alive[i] && np[i] < MAX_PTS) { pts[i][np[i]][0] = c + i; pts[i][np[i]][1] = row; np[i]++; }
        // Individual traces drop out (staggered endings, as in the reference).
        for (int i = 0; i < w && nAlive > 1; i++) {
            if (alive[i] && pcg32_chance(r, p->stop * 0.6f)) { alive[i] = 0; nAlive--; }
        }
        if (row >= g->rows - 2 || pcg32_chance(r, p->stop)) break;
        // Diagonal jog of the whole bus.
        int j = pcg32_int(r, 1, 3), dir = pcg32_chance(r, 0.5f) ? 1 : -1, jogged = 0;
        for (int attempt = 0; attempt < 2 && !jogged; attempt++, dir = -dir) {
            int ok = c + dir * j >= 0 && c + dir * j + w <= g->cols;
            for (int y = 1; y <= j && ok; y++) ok = band_free(g, c + dir * y, w, row + y);
            if (!ok) continue;
            for (int y = 1; y <= j; y++) for (int i = 0; i < w; i++) if (alive[i]) take(g, c + dir * y + i, row + y);
            c += dir * j;
            row += j;
            for (int i = 0; i < w; i++) if (alive[i] && np[i] < MAX_PTS) { pts[i][np[i]][0] = c + i; pts[i][np[i]][1] = row; np[i]++; }
            jogged = 1;
        }
    }
    int drew = 0;
    if (row - startRow < 5) return 0;   // stubs read as blobs, not traces (cells stay taken)
    for (int i = 0; i < w; i++) {
        if (np[i] < 2) continue;
        LineAttr a = at;
        a.hash = pcg32_float(r);
        emit_trace(p, f, g, (const int (*)[2])pts[i], np[i], origin, out, a);
        const int *e = pts[i][np[i] - 1];
        emit_via(p, f, g, e[0], e[1], origin, out, via);
        if (startVia) emit_via(p, f, g, pts[i][0][0], pts[i][0][1], origin, out, via);
        drew = 1;
    }
    return drew;
}

void trace_route_buses(const TraceRouteParams *p, const TraceFace *f, TraceGrid *g, float density, Pcg32 *r,
                       DVec3 origin, LineGeo *out)
{
    int buses = (int)floorf(density * (float)g->cols / 10.0f + pcg32_float(r));
    LineAttr at = { p->color, p->intensity, 0.0f, p->pulse, -1.0f, 0.0f };
    LineAttr via = { p->colorVia, p->intensity * 1.3f, 0.5f, 0.0f, -1.0f, 0.0f };
    for (int b = 0, tries = 0; b < buses && tries < buses * 4; tries++) {
        int w = pcg32_int(r, 2, MAX_BUS);
        if (w > g->cols) w = g->cols;
        int c = pcg32_int(r, 0, g->cols - w);
        int row = pcg32_chance(r, p->baseStart) ? 0 : pcg32_int(r, 0, g->rows / 2);
        at.color = pcg32_chance(r, p->altChance) ? p->colorAlt : p->color;
        if (route_bus(p, f, g, r, c, row, w, origin, out, at, via)) b++;
    }
}
