#include "city_towers.h"
#include "env.h"
#include "core/chunks.h"
#include "core/rng.h"
#include "core/pacing.h"
#include "lattice_place.h"
#include <math.h>

enum { F_TOWERS = 1 };       // feature sub-streams of the city's per-cell RNG

static float smooth01(float a, float b, float x)
{
    float t = (x - a) / (b - a);
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return t * t * (3.0f - 2.0f * t);
}

CityArch city_arch_neutral(void)
{
    return (CityArch){ 1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.5f };
}

CityArch city_arch_at(const CityArchBlend *b, double s)
{
    if (!b) return city_arch_neutral();
    CityArch r;
    pacing_lerp(&r.densityNear, &b->prev.densityNear, &b->cur.densityNear, (int)(sizeof(CityArch) / sizeof(float)),
                pacing_blend_t(s, b->s0, b->len));
    return r;
}

double city_towers_reach(const CityTowerParams *p) { return lattice_reach(p->bandMax); }

typedef struct PlanCtx {
    const CityTowerParams *p;
    const CityArchBlend *blend;
    uint64_t envSeed;
    TowerDesc *out;
    int maxTowers, n;
    CellClaim *claims;
    int maxClaims, nc;
} PlanCtx;

static void plan_cell(void *user, const LatticeWindow *w, const LatticeCell *cell)
{
    PlanCtx *pc = (PlanCtx *)user;
    const CityTowerParams *p = pc->p;
    int ix = cell->ix, iz = cell->iz;
    double sOwn = cell->sOwner, d = cell->dist;
    int towerIdx = -1;
    Pcg32 r = pcg32_seed(ct_hash64_3(pc->envSeed, (uint64_t)(int64_t)ix, (uint64_t)(int64_t)iz));
    CityArch a = city_arch_at(pc->blend, sOwn);
    float inner = p->laneHalfWidth + p->clearance + fmaxf(a.clearanceAdd, 0.0f);
    float far = smooth01(inner, fmaxf(p->bandMax, inner + 1.0f), (float)d);
    float dn = p->densityNear * a.densityNear, df = p->densityFar * a.densityFar;
    float dens = dn + (df - dn) * far;
    if (pcg32_chance(&r, dens) && pc->n < pc->maxTowers) {
        TowerDesc *t = &pc->out[pc->n];
        int lc = p->latticeCells;
        int fw = pcg32_int(&r, p->footMin, p->footMax), fd = pcg32_int(&r, p->footMin, p->footMax);
        if (fw > lc - 1) fw = lc - 1;
        if (fd > lc - 1) fd = lc - 1;
        if (fw < 2) fw = 2;
        if (fd < 2) fd = 2;
        int ox = pcg32_int(&r, 0, lc - 1 - fw), oz = pcg32_int(&r, 0, lc - 1 - fd);
        // Edges on floor grid lines (world multiples of cell).
        double gx = (double)ix * lc + ox, gz = (double)iz * lc + oz;
        t->x0[0] = gx * p->cell;
        t->x1[0] = (gx + fw) * p->cell;
        t->z0[0] = gz * p->cell;
        t->z1[0] = (gz + fd) * p->cell;
        float H = (p->heightMin + pcg32_float(&r) * (p->heightMax - p->heightMin)) * a.height + p->heightFar * a.heightFar * far;
        if (pcg32_chance(&r, p->landmarkChance * a.landmark)) H *= p->landmarkMul;
        int tiers = 1;
        if (pcg32_chance(&r, p->setbackChance)) tiers = pcg32_chance(&r, 0.35f) ? 3 : 2;
        float frac[CITY_MAX_TIERS] = { 1.0f, 1.0f, 1.0f };
        if (tiers == 2) frac[0] = pcg32_range(&r, 0.45f, 0.7f);
        if (tiers == 3) { frac[0] = pcg32_range(&r, 0.35f, 0.5f); frac[1] = pcg32_range(&r, 0.62f, 0.82f); }
        t->baseY = p->floorY;
        int built = 1;
        int cw = fw, cd = fd;
        for (int k = 1; k < tiers; k++) {
            // Inset the next tier by one grid cell on some sides (at least one).
            int l = pcg32_chance(&r, 0.6f), rr = pcg32_chance(&r, 0.6f);
            int f = pcg32_chance(&r, 0.6f), b = pcg32_chance(&r, 0.6f);
            if (!(l | rr | f | b)) l = 1;
            if (cw - l - rr < 2 || cd - f - b < 2) break;
            t->x0[k] = t->x0[k - 1] + l * p->cell;
            t->x1[k] = t->x1[k - 1] - rr * p->cell;
            t->z0[k] = t->z0[k - 1] + f * p->cell;
            t->z1[k] = t->z1[k - 1] - b * p->cell;
            cw -= l + rr;
            cd -= f + b;
            built++;
        }
        t->tiers = built;
        for (int k = 0; k < built; k++) t->yTop[k] = p->floorY + H * (k == built - 1 ? 1.0f : frac[k]);
        t->hash = pcg32_next(&r);
        // The footprint must keep clear of the lane everywhere along the path.
        if (lattice_rect_dist(w, t->x0[0], t->z0[0], t->x1[0], t->z1[0]) >= inner) {
            t->ix = ix;
            t->iz = iz;
            t->sOwner = sOwn;
            t->dist = (float)d;
            t->traceMul = a.traces;
            t->glyphMul = a.glyphs;
            towerIdx = pc->n++;
        }
    }
    if (pc->claims && pc->nc < pc->maxClaims) pc->claims[pc->nc++] = (CellClaim){ ix, iz, sOwn, (float)d, towerIdx };
}

int city_towers_plan(const CityTowerParams *p, const CityArchBlend *blend, uint64_t seed, const PathSystem *path,
                     int chunkIndex, TowerDesc *out, int maxTowers, CellClaim *claims, int maxClaims, int *claimCount)
{
    static LatticeWindow w;   // large; builds run on one thread
    lattice_window_load(&w, path, chunkIndex, city_towers_reach(p));
    PlanCtx pc = { p, blend, ct_hash64_2(seed, ((uint64_t)ENV_CITY << 8) | F_TOWERS), out, maxTowers, 0, claims, maxClaims, 0 };
    lattice_for_each(&w, (double)p->latticeCells * p->cell, p->bandMax, plan_cell, &pc);
    if (claimCount) *claimCount = pc.nc;
    return pc.n;
}

// ---------------------------------------------------------------------------
// Geometry

static Vector3 loc(double x, double y, double z, DVec3 o) { return (Vector3){ (float)(x - o.x), (float)(y - o.y), (float)(z - o.z) }; }

// Quad a,b,c,d (counter-clockwise seen from outside) with per-corner (u, v) in world units.
static void quad(LineGeo *g, const Vector3 p[4], const Vector2 uv[4], Vector3 n, Vector2 uv2, Vector4 tan, Color c)
{
    int b = lg_reserve(g, 4, 6);
    for (int i = 0; i < 4; i++) lg_vertex(g, p[i], uv[i], uv2, n, tan, c);
    lg_quad(g, b, 0, 1, 2, 3);
}

void city_towers_build(const TowerDesc *towers, int n, DVec3 o, LineGeo *body, LineGeo *edges,
                       float edgeIntensity, float edgeWidth)
{
    for (int ti = 0; ti < n; ti++) {
        const TowerDesc *t = &towers[ti];
        float h01 = (float)(t->hash & 0xFFFFFF) / 16777216.0f;
        float H = t->yTop[t->tiers - 1] - t->baseY;
        uint32_t hv = t->hash >> 24;
        Color tint = { (unsigned char)(150 + (hv & 31)), (unsigned char)(165 + ((hv >> 2) & 31)), (unsigned char)(190 + ((hv >> 3) & 31)), 255 };
        Vector2 uv2 = { h01, H };
        double X0 = t->x0[0], Z0 = t->z0[0], B = t->baseY;
        LineAttr ea = { (Color){ 0x8A, 0xE8, 0xF5, 255 }, edgeIntensity, h01, 0.0f, -1.0f, 0.0f };
        for (int k = 0; k < t->tiers; k++) {
            double x0 = t->x0[k], x1 = t->x1[k], z0 = t->z0[k], z1 = t->z1[k];
            double yb = k == 0 ? B : t->yTop[k - 1], yt = t->yTop[k];
            float vb = (float)(yb - B), vt = (float)(yt - B);
            Vector4 tan = { 0.0f, (float)k, 0.0f, 0.0f };
            // -z face (u along +x), +z face, -x face (u along +z), +x face.
            {
                Vector3 p[4] = { loc(x0, yb, z0, o), loc(x1, yb, z0, o), loc(x1, yt, z0, o), loc(x0, yt, z0, o) };
                Vector2 uv[4] = { { (float)(x0 - X0), vb }, { (float)(x1 - X0), vb }, { (float)(x1 - X0), vt }, { (float)(x0 - X0), vt } };
                tan.x = 0.0f; quad(body, p, uv, (Vector3){ 0, 0, -1 }, uv2, tan, tint);
            }
            {
                Vector3 p[4] = { loc(x1, yb, z1, o), loc(x0, yb, z1, o), loc(x0, yt, z1, o), loc(x1, yt, z1, o) };
                Vector2 uv[4] = { { (float)(x1 - X0), vb }, { (float)(x0 - X0), vb }, { (float)(x0 - X0), vt }, { (float)(x1 - X0), vt } };
                tan.x = 1.0f; quad(body, p, uv, (Vector3){ 0, 0, 1 }, uv2, tan, tint);
            }
            {
                Vector3 p[4] = { loc(x0, yb, z1, o), loc(x0, yb, z0, o), loc(x0, yt, z0, o), loc(x0, yt, z1, o) };
                Vector2 uv[4] = { { (float)(z1 - Z0), vb }, { (float)(z0 - Z0), vb }, { (float)(z0 - Z0), vt }, { (float)(z1 - Z0), vt } };
                tan.x = 2.0f; quad(body, p, uv, (Vector3){ -1, 0, 0 }, uv2, tan, tint);
            }
            {
                Vector3 p[4] = { loc(x1, yb, z0, o), loc(x1, yb, z1, o), loc(x1, yt, z1, o), loc(x1, yt, z0, o) };
                Vector2 uv[4] = { { (float)(z0 - Z0), vb }, { (float)(z1 - Z0), vb }, { (float)(z1 - Z0), vt }, { (float)(z0 - Z0), vt } };
                tan.x = 3.0f; quad(body, p, uv, (Vector3){ 1, 0, 0 }, uv2, tan, tint);
            }
            {
                Vector3 p[4] = { loc(x0, yt, z0, o), loc(x0, yt, z1, o), loc(x1, yt, z1, o), loc(x1, yt, z0, o) };
                Vector2 uv[4] = { { (float)(x0 - X0), (float)(z0 - Z0) }, { (float)(x0 - X0), (float)(z1 - Z0) },
                                  { (float)(x1 - X0), (float)(z1 - Z0) }, { (float)(x1 - X0), (float)(z0 - Z0) } };
                tan.x = 4.0f; quad(body, p, uv, (Vector3){ 0, 1, 0 }, uv2, tan, tint);
            }
            if (edges && edgeIntensity > 0.0f) {
                Vector3 c[4] = { loc(x0, yb, z0, o), loc(x1, yb, z0, o), loc(x1, yb, z1, o), loc(x0, yb, z1, o) };
                Vector3 c2[4] = { loc(x0, yt, z0, o), loc(x1, yt, z0, o), loc(x1, yt, z1, o), loc(x0, yt, z1, o) };
                for (int i = 0; i < 4; i++) {
                    lg_facing_segment(edges, c[i], c2[i], edgeWidth, edgeWidth, edgeIntensity * 0.6f, edgeIntensity, ea);
                    lg_facing_segment(edges, c2[i], c2[(i + 1) & 3], edgeWidth, edgeWidth, edgeIntensity, edgeIntensity, ea);
                }
            }
        }
    }
}
