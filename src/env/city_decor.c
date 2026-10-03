#include "city_decor.h"
#include "core/rng.h"
#include <math.h>
#include <string.h>

#define MAX_ROWS   512
#define MAX_COLS   64
#define MAX_PTS    96
#define MAX_BUS    4

enum { F_TRACES = 2 };      // sub-stream of the tower hash (towers use F_TOWERS = 1)

static const Color COL_TRACE  = { 0xA8, 0x8C, 0xFF, 255 };   // lavender
static const Color COL_TRACE2 = { 0x8A, 0x6C, 0xFF, 255 };   // violet
static const Color COL_VIA    = { 0xE0, 0xD8, 0xFF, 255 };

typedef struct Face {
    DVec3  o;                // bottom corner (world)
    DVec3  u, n;             // along-face direction, outward normal
    double w, h;             // width, height
} Face;

typedef struct Grid {
    uint64_t occ[MAX_ROWS];
    int cols, rows;
    double u0;               // u of column 0 centre
} Grid;

static int cell_free(const Grid *g, int c, int r)
{
    if (r < 0 || r >= g->rows) return 0;
    if (c < 0 || c >= g->cols) return 1;   // margins outside the face are free
    return !((g->occ[r] >> c) & 1u);
}
static int cell_in(const Grid *g, int c, int r) { return c >= 0 && c < g->cols && r >= 0 && r < g->rows; }
static void take(Grid *g, int c, int r) { if (cell_in(g, c, r)) g->occ[r] |= (uint64_t)1 << c; }

// All of columns [c-1, c+w] free at row r (bus plus one margin column each side).
static int band_free(const Grid *g, int c, int w, int r)
{
    for (int i = -1; i <= w; i++) if (!cell_free(g, c + i, r)) return 0;
    return 1;
}

static Vector3 face_point(const Face *f, double u, double v, double off, DVec3 origin)
{
    DVec3 p = dv_add(dv_add(f->o, dv_scale(f->u, u)), dv_add(dv3(0.0, v, 0.0), dv_scale(f->n, off)));
    return dv_to_v3(dv_sub(p, origin));
}

static void emit_trace(const CityDecorParams *p, const Face *f, const Grid *g, const int (*pt)[2], int np,
                       DVec3 origin, LineGeo *out, LineAttr at)
{
    if (np < 2) return;
    Vector3 pts[MAX_PTS], acr[MAX_PTS];
    for (int i = 0; i < np; i++) {
        double u = g->u0 + pt[i][0] * p->tracePitch, v = (pt[i][1] + 0.5) * p->tracePitch;
        pts[i] = face_point(f, u, v, p->faceOffset, origin);
        // In-plane direction at the point (average of the adjacent segments), across = its normal.
        int a = i > 0 ? i - 1 : i, b = i < np - 1 ? i + 1 : i;
        double du = pt[b][0] - pt[a][0], dv = pt[b][1] - pt[a][1];
        double l = sqrt(du * du + dv * dv);
        if (l < 1e-9) { du = 0.0; dv = 1.0; l = 1.0; }
        du /= l; dv /= l;
        DVec3 across = dv_add(dv_scale(f->u, dv), dv3(0.0, -du, 0.0));
        acr[i] = dv_to_v3(dv_norm(across));
    }
    lg_planar_polyline(out, pts, acr, np, p->traceWidth, at);
}

static void emit_via(const CityDecorParams *p, const Face *f, const Grid *g, int c, int r, DVec3 origin, LineGeo *out, LineAttr at)
{
    enum { SEG = 8 };
    Vector3 pts[SEG + 1], acr[SEG + 1];
    double uc = g->u0 + c * p->tracePitch, vc = (r + 0.5) * p->tracePitch, rad = p->viaRadius;
    for (int i = 0; i <= SEG; i++) {
        double a = 6.283185307179586 * i / SEG;
        double du = cos(a), dv = sin(a);
        pts[i] = face_point(f, uc + du * rad, vc + dv * rad, p->faceOffset, origin);
        acr[i] = dv_to_v3(dv_add(dv_scale(f->u, du), dv3(0.0, dv, 0.0)));
    }
    lg_planar_polyline(out, pts, acr, SEG + 1, (float)(rad * 0.6), at);
}

// One bus of w traces starting at column c, row r. Returns 1 if anything was drawn.
static int route_bus(const CityDecorParams *p, const Face *f, Grid *g, Pcg32 *r, int c, int row, int w,
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
            if (alive[i] && pcg32_chance(r, p->traceStop * 0.6f)) { alive[i] = 0; nAlive--; }
        }
        if (row >= g->rows - 2 || pcg32_chance(r, p->traceStop)) break;
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

static void glyph_quad(LineGeo *g, const Face *f, double u0, double v0, double w, double h, float hash, int kind,
                       float intensity, float scroll, float off, DVec3 origin)
{
    int b = lg_reserve(g, 4, 6);
    Vector3 n = dv_to_v3(f->n);
    Vector4 t = { (float)w, (float)h, intensity, scroll };
    Vector2 uv2 = { hash, (float)kind };
    const double cu[4] = { 0, w, w, 0 }, cv[4] = { 0, 0, h, h };
    for (int i = 0; i < 4; i++)
        lg_vertex(g, face_point(f, u0 + cu[i], v0 + cv[i], off, origin), (Vector2){ (float)cu[i], (float)cv[i] }, uv2, n, t, WHITE);
    lg_quad(g, b, 0, 1, 2, 3);
}

// Block columns [c0, c1] for rows [r0, r1] (with a one-cell margin).
static void block_cells(Grid *g, int c0, int c1, int r0, int r1)
{
    for (int r = r0 - 1; r <= r1 + 1; r++) for (int c = c0 - 1; c <= c1 + 1; c++) take(g, c, r);
}

static void decorate_face(const CityDecorParams *p, const Face *f, int front, int lane, float lod, float glyphMul, Pcg32 *r,
                          DVec3 origin, DVec3 towardCamera, LineGeo *traces, LineGeo *glyphs)
{
    static Grid g;
    double pitch = p->tracePitch;
    g.cols = (int)floor(f->w / pitch) - 1;
    g.rows = (int)floor(f->h / pitch);
    if (g.cols > MAX_COLS) g.cols = MAX_COLS;
    if (g.rows > MAX_ROWS) g.rows = MAX_ROWS;
    if (g.cols < 3 || g.rows < 6) return;
    memset(g.occ, 0, sizeof(uint64_t) * (size_t)g.rows);
    g.u0 = (f->w - (g.cols - 1) * pitch) * 0.5;

    if (lane) {
        float roll = pcg32_float(r);
        if (roll < p->glyphStripChance * glyphMul) {
            // Narrow tall strip by the edge nearer the approaching camera.
            double w = 2.0 * pitch;
            int nearEnd = dv_dot(f->u, towardCamera) > 0.0;
            double u0 = nearEnd ? f->w - 0.45 - w : 0.45;
            double v0 = f->h * pcg32_range(r, 0.08f, 0.3f), v1 = f->h * pcg32_range(r, 0.6f, 0.95f);
            glyph_quad(glyphs, f, u0, v0, w, v1 - v0, pcg32_float(r), 0, p->glyphIntensity, p->glyphScroll, (float)p->faceOffset, origin);
            int c0 = (int)floor((u0 - g.u0) / pitch), c1 = (int)ceil((u0 + w - g.u0) / pitch);
            block_cells(&g, c0, c1, (int)(v0 / pitch), (int)(v1 / pitch));
        } else if (roll < (p->glyphStripChance + p->glyphBlockChance) * glyphMul) {
            double w = f->w * pcg32_range(r, 0.5f, 0.85f), h = fmin(f->h * pcg32_range(r, 0.15f, 0.4f), 18.0);
            double u0 = (f->w - w) * pcg32_float(r), v0 = (f->h - h) * pcg32_range(r, 0.1f, 0.7f);
            glyph_quad(glyphs, f, u0, v0, w, h, pcg32_float(r), 1, p->glyphIntensity, p->glyphScroll, (float)p->faceOffset, origin);
            int c0 = (int)floor((u0 - g.u0) / pitch), c1 = (int)ceil((u0 + w - g.u0) / pitch);
            block_cells(&g, c0, c1, (int)(v0 / pitch), (int)((v0 + h) / pitch));
        }
    }

    // Buses: dense on front faces, sparse on lane faces; fewer with distance (LOD).
    float density = (front ? 1.0f : 0.45f) * lod * p->traceBuses;
    int buses = (int)floorf(density * (float)g.cols / 10.0f + pcg32_float(r));
    LineAttr at = { COL_TRACE, p->traceIntensity, 0.0f, p->tracePulse, -1.0f, 0.0f };
    LineAttr via = { COL_VIA, p->traceIntensity * 1.3f, 0.5f, 0.0f, -1.0f, 0.0f };
    for (int b = 0, tries = 0; b < buses && tries < buses * 4; tries++) {
        int w = pcg32_int(r, 2, MAX_BUS);
        if (w > g.cols) w = g.cols;
        int c = pcg32_int(r, 0, g.cols - w);
        int row = pcg32_chance(r, 0.7f) ? 0 : pcg32_int(r, 0, g.rows / 2);
        at.color = pcg32_chance(r, 0.3f) ? COL_TRACE2 : COL_TRACE;
        if (route_bus(p, f, &g, r, c, row, w, origin, traces, at, via)) b++;
    }
}

void city_decor_build(const CityDecorParams *p, const TowerDesc *towers, int n, const PathSystem *path,
                      DVec3 origin, LineGeo *traces, LineGeo *glyphs)
{
    for (int ti = 0; ti < n; ti++) {
        const TowerDesc *t = &towers[ti];
        Pcg32 r = pcg32_seed(ct_hash64_2(t->hash, F_TRACES));
        PathSample ps = path_sample(path, t->sOwner);
        DVec3 centre = dv3((t->x0[0] + t->x1[0]) * 0.5, 0.0, (t->z0[0] + t->z1[0]) * 0.5);
        DVec3 toPath = dv_sub(ps.pos, centre);
        toPath.y = 0.0;
        toPath = dv_norm(toPath);
        DVec3 toCam = dv_scale(ps.T, -1.0);   // the camera approaches from behind along the path
        float far = (t->dist - p->inner) / fmaxf(p->bandMax - p->inner, 1.0f);
        far = far < 0.0f ? 0.0f : (far > 1.0f ? 1.0f : far);
        float lod = (1.0f + (p->traceFar - 1.0f) * far) * t->traceMul;
        for (int k = 0; k < t->tiers; k++) {
            double x0 = t->x0[k], x1 = t->x1[k], z0 = t->z0[k], z1 = t->z1[k];
            double yb = k == 0 ? t->baseY : t->yTop[k - 1];
            double h = t->yTop[k] - yb;
            Face faces[4] = {
                { dv3(x0, yb, z0), dv3(1, 0, 0), dv3(0, 0, -1), x1 - x0, h },
                { dv3(x1, yb, z1), dv3(-1, 0, 0), dv3(0, 0, 1), x1 - x0, h },
                { dv3(x0, yb, z1), dv3(0, 0, -1), dv3(-1, 0, 0), z1 - z0, h },
                { dv3(x1, yb, z0), dv3(0, 0, 1), dv3(1, 0, 0), z1 - z0, h },
            };
            for (int fi = 0; fi < 4; fi++) {
                int front = dv_dot(faces[fi].n, toCam) > 0.4;
                int lane = dv_dot(faces[fi].n, toPath) > 0.4;
                if (!front && !lane) continue;
                decorate_face(p, &faces[fi], front, lane && k == 0, lod, t->glyphMul, &r, origin, toCam, traces, glyphs);
            }
        }
    }
}
