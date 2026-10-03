#include "corridor_beams.h"
#include "lattice_place.h"
#include "env.h"
#include "core/chunks.h"
#include "core/rng.h"
#include <math.h>

enum { F_BEAMS = 3 };        // corridor feature sub-streams (pillars 1, pillar decor 2)

#define U_PERIOD 4096.0      // u wraps every 4096 units (a whole number of pattern periods)

double beams_reach(const BeamParams *p) { return lattice_reach(p->bandMax + p->cell); }

typedef struct Line {
    int      on;
    float    width, thick, y;
    uint32_t hash;
} Line;

// Line `idx` of direction dir (0 rows: constant z, 1 columns: constant x).
static Line line_of(const BeamParams *p, uint64_t envSeed, int dir, int idx)
{
    Pcg32 r = pcg32_seed(ct_hash64_3(envSeed, (uint64_t)dir, (uint64_t)(int64_t)idx));
    Line l;
    l.on = pcg32_chance(&r, p->chance * p->density);
    l.width = pcg32_range(&r, p->widthMin, p->widthMax);
    l.thick = pcg32_range(&r, p->thickMin, p->thickMax);
    int lv = pcg32_int(&r, 0, p->levels - 1);
    l.y = p->floorY + p->heightMin + (float)(2 * lv + dir);
    l.hash = pcg32_next(&r);
    return l;
}

typedef struct PlanCtx {
    const BeamParams *p;
    uint64_t envSeed;
    BeamDesc *out;
    int maxBeams, n;
    int32_t (*cells)[2];
    int maxCells, nc;
} PlanCtx;

// Does cell (ix, iz) carry a segment of line `dir` through it? (from any chunk's point of view)
static int has_segment(const PlanCtx *pc, const LatticeWindow *w, int dir, int ix, int iz, const Line *l)
{
    const BeamParams *p = pc->p;
    if (!l->on) return 0;
    double L = p->cell, cx = (ix + 0.5) * L, cz = (iz + 0.5) * L, s;
    double d = lattice_nearest(w, cx, cz, &s);
    if (d > p->bandMax) return 0;
    // Keep beams that run along the lane out from above it: the path's direction at the owner.
    double fs = s / LATTICE_SAMPLE_STEP - (double)w->m0;
    int i = (int)floor(fs);
    if (i < 0) i = 0;
    if (i > w->n - 2) i = w->n - 2;
    double tx = w->x[i + 1] - w->x[i], tz = w->z[i + 1] - w->z[i];
    int laneAlongZ = fabs(tz) >= fabs(tx);
    if ((dir == 1) == laneAlongZ) {
        double px = w->x[i] + tx * (fs - i), pz = w->z[i] + tz * (fs - i);
        double off = dir == 1 ? fabs(cx - px) : fabs(cz - pz);
        if (off - l->width * 0.5 < p->laneKeep) return 0;
    }
    Pcg32 r = pcg32_seed(ct_hash64_3(pc->envSeed ^ 0x9E37u, (uint64_t)(int64_t)ix * 2u + (uint64_t)dir, (uint64_t)(int64_t)iz));
    return !pcg32_chance(&r, p->gapChance);
}

static void plan_cell(void *user, const LatticeWindow *w, const LatticeCell *cell)
{
    PlanCtx *pc = (PlanCtx *)user;
    const BeamParams *p = pc->p;
    int ix = cell->ix, iz = cell->iz;
    double L = p->cell;
    if (pc->cells && pc->nc < pc->maxCells) { pc->cells[pc->nc][0] = ix; pc->cells[pc->nc][1] = iz; pc->nc++; }
    for (int dir = 0; dir < 2; dir++) {
        Line l = line_of(p, pc->envSeed, dir, dir == 0 ? iz : ix);
        if (!has_segment(pc, w, dir, ix, iz, &l) || pc->n >= pc->maxBeams) continue;
        BeamDesc *b = &pc->out[pc->n++];
        b->ix = ix;
        b->iz = iz;
        b->dir = dir;
        b->a0 = (dir == 0 ? ix : iz) * L;
        b->a1 = b->a0 + L;
        b->c = ((dir == 0 ? iz : ix) + 0.5) * L;
        b->width = l.width;
        b->thick = l.thick;
        b->yBottom = l.y;
        b->hash = l.hash;
        b->sOwner = cell->sOwner;
        int px = ix - (dir == 0), pz = iz - (dir == 1), nx = ix + (dir == 0), nz = iz + (dir == 1);
        b->cap0 = !has_segment(pc, w, dir, px, pz, &l);
        b->cap1 = !has_segment(pc, w, dir, nx, nz, &l);
    }
}

int beams_plan(const BeamParams *p, uint64_t seed, const PathSystem *path, int chunkIndex, BeamDesc *out, int maxBeams,
               int32_t (*ownedCells)[2], int maxCells, int *cellCount)
{
    static LatticeWindow w;   // large; builds run on one thread
    lattice_window_load(&w, path, chunkIndex, beams_reach(p));
    PlanCtx pc = { p, ct_hash64_2(seed, ((uint64_t)ENV_CORRIDOR << 8) | F_BEAMS), out, maxBeams, 0, ownedCells, maxCells, 0 };
    lattice_for_each(&w, p->cell, p->bandMax, plan_cell, &pc);
    if (cellCount) *cellCount = pc.nc;
    return pc.n;
}

// ---------------------------------------------------------------------------
// Geometry

static Vector3 bpt(const BeamDesc *b, double along, double across, double y, DVec3 o)
{
    DVec3 p = b->dir == 0 ? dv3(along, y, across) : dv3(across, y, along);
    return dv_to_v3(dv_sub(p, o));
}

static void quad(LineGeo *g, const Vector3 p[4], const Vector2 uv[4], Vector3 n, Vector2 uv2, Vector4 tan)
{
    int b = lg_reserve(g, 4, 6);
    for (int i = 0; i < 4; i++) lg_vertex(g, p[i], uv[i], uv2, n, tan, WHITE);
    lg_quad(g, b, 0, 1, 2, 3);
}

static float wrap_u(double a)
{
    double u = fmod(a, U_PERIOD);
    if (u < 0.0) u += U_PERIOD;
    return (float)u;
}

void beams_build(const BeamDesc *beams, int n, DVec3 o, LineGeo *body)
{
    for (int i = 0; i < n; i++) {
        const BeamDesc *b = &beams[i];
        double c0 = b->c - b->width * 0.5, c1 = b->c + b->width * 0.5;
        double y0 = b->yBottom, y1 = b->yBottom + b->thick;
        float u0 = wrap_u(b->a0), u1 = u0 + (float)(b->a1 - b->a0);   // u0 + L may exceed the period at the wrap: fine, continuous inside the segment
        float h01 = (float)(b->hash & 0xFFFFFF) / 16777216.0f;
        Vector2 uv2 = { h01, b->width };
        Vector3 nUp = { 0, -1, 0 };
        Vector3 nC0 = b->dir == 0 ? (Vector3){ 0, 0, -1 } : (Vector3){ -1, 0, 0 };
        Vector3 nC1 = b->dir == 0 ? (Vector3){ 0, 0, 1 } : (Vector3){ 1, 0, 0 };
        Vector3 nA0 = b->dir == 0 ? (Vector3){ -1, 0, 0 } : (Vector3){ 0, 0, -1 };
        Vector3 nA1 = b->dir == 0 ? (Vector3){ 1, 0, 0 } : (Vector3){ 0, 0, 1 };
        float W = b->width, T = b->thick;
        // Underside: u along, v across from c0.
        {
            Vector3 p[4] = { bpt(b, b->a0, c0, y0, o), bpt(b, b->a1, c0, y0, o), bpt(b, b->a1, c1, y0, o), bpt(b, b->a0, c1, y0, o) };
            Vector2 uv[4] = { { u0, 0 }, { u1, 0 }, { u1, W }, { u0, W } };
            quad(body, p, uv, nUp, uv2, (Vector4){ 0, 0, 0, 0 });
        }
        // Sides: u along, v up from the bottom edge.
        {
            Vector3 p[4] = { bpt(b, b->a0, c0, y0, o), bpt(b, b->a1, c0, y0, o), bpt(b, b->a1, c0, y1, o), bpt(b, b->a0, c0, y1, o) };
            Vector2 uv[4] = { { u0, 0 }, { u1, 0 }, { u1, T }, { u0, T } };
            quad(body, p, uv, nC0, uv2, (Vector4){ 1, 0, 0, 0 });
        }
        {
            Vector3 p[4] = { bpt(b, b->a0, c1, y0, o), bpt(b, b->a1, c1, y0, o), bpt(b, b->a1, c1, y1, o), bpt(b, b->a0, c1, y1, o) };
            Vector2 uv[4] = { { u0, 0 }, { u1, 0 }, { u1, T }, { u0, T } };
            quad(body, p, uv, nC1, uv2, (Vector4){ 1, 0, 0, 0 });
        }
        // End caps: u across, v up.
        if (b->cap0) {
            Vector3 p[4] = { bpt(b, b->a0, c0, y0, o), bpt(b, b->a0, c1, y0, o), bpt(b, b->a0, c1, y1, o), bpt(b, b->a0, c0, y1, o) };
            Vector2 uv[4] = { { 0, 0 }, { W, 0 }, { W, T }, { 0, T } };
            quad(body, p, uv, nA0, uv2, (Vector4){ 2, 0, 0, 0 });
        }
        if (b->cap1) {
            Vector3 p[4] = { bpt(b, b->a1, c0, y0, o), bpt(b, b->a1, c1, y0, o), bpt(b, b->a1, c1, y1, o), bpt(b, b->a1, c0, y1, o) };
            Vector2 uv[4] = { { 0, 0 }, { W, 0 }, { W, T }, { 0, T } };
            quad(body, p, uv, nA1, uv2, (Vector4){ 2, 0, 0, 0 });
        }
    }
}
