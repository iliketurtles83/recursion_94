#include "corridor_pillars.h"
#include "lattice_place.h"
#include "env.h"
#include "core/chunks.h"
#include "core/rng.h"
#include <math.h>

enum { F_PILLARS = 1 };     // feature sub-streams of the corridor's per-cell RNG

PillarArch pillar_arch_neutral(void) { return (PillarArch){ 1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f }; }

static PillarArch arch_at(const PillarArchFn *a, double s)
{
    return a ? a->at(a->user, s) : pillar_arch_neutral();
}

double pillars_reach(const PillarParams *p)
{
    // Odd cells also evaluate their even partner one lattice cell further along.
    return lattice_reach(p->bandMax + (double)p->latticeCells * p->cell);
}

static double snap05(double x) { return floor(x * 2.0 + 0.5) * 0.5; }

// Path position and tangent (xz) at arc length s, interpolated from the window samples.
static void window_at(const LatticeWindow *w, double s, double *x, double *z, double *tx, double *tz)
{
    double f = s / LATTICE_SAMPLE_STEP - (double)w->m0;
    int i = (int)floor(f);
    if (i < 0) i = 0;
    if (i > w->n - 2) i = w->n - 2;
    double t = f - i;
    *x = w->x[i] + (w->x[i + 1] - w->x[i]) * t;
    *z = w->z[i] + (w->z[i + 1] - w->z[i]) * t;
    double dx = w->x[i + 1] - w->x[i], dz = w->z[i + 1] - w->z[i], l = sqrt(dx * dx + dz * dz);
    *tx = l > 0.0 ? dx / l : 0.0;
    *tz = l > 0.0 ? dz / l : -1.0;
}

typedef struct PlanCtx {
    const PillarParams *p;
    const PillarArchFn *arch;
    uint64_t envSeed;
    double L;
    PillarDesc *out;
    int maxPillars, n;
    PillarClaim *claims;
    int maxClaims, nc;
} PlanCtx;

// Everything about a cell that its merge partner also needs, from (seed, params, path) only.
typedef struct CellInfo {
    int    inBand;           // within bandMax (owned by some chunk)
    int    axis, side, front;
    int    wall, occupied, merge;
    double sOwn, dist;
    double cLo, cHi, aLo, aHi;   // cell extent across / along
    double laneMin;          // across coordinate of the closest allowed lane face (lane + clearance)
    double nearEdge;         // across coordinate of the cell edge nearest the lane
    PillarArch arch;
    Pcg32  r;                // stream positioned after the occupancy / merge rolls
} CellInfo;

static int along_index(int axis, int ix, int iz) { return axis ? iz : ix; }

static void cell_info(const PlanCtx *pc, const LatticeWindow *w, int ix, int iz, CellInfo *ci)
{
    const PillarParams *p = pc->p;
    double L = pc->L, cx = (ix + 0.5) * L, cz = (iz + 0.5) * L;
    ci->dist = lattice_nearest(w, cx, cz, &ci->sOwn);
    ci->inBand = ci->dist <= p->bandMax;
    double px, pz, tx, tz;
    window_at(w, ci->sOwn, &px, &pz, &tx, &tz);
    ci->axis = fabs(tz) >= fabs(tx);   // 1: lane runs along z
    double pathAcross = ci->axis ? px : pz, cellAcross = ci->axis ? cx : cz;
    double tAlong = ci->axis ? tz : tx;
    ci->side = cellAcross >= pathAcross ? 1 : -1;
    ci->front = tAlong < 0.0 ? 1 : -1;   // the camera approaches from -T
    int ia = ci->axis ? ix : iz, ia2 = ci->axis ? iz : ix;
    ci->cLo = ia * L;
    ci->cHi = ci->cLo + L;
    ci->aLo = ia2 * L;
    ci->aHi = ci->aLo + L;
    ci->arch = arch_at(pc->arch, ci->sOwn);
    double inner = p->laneHalfWidth + p->clearance + fmax(ci->arch.clearanceAdd, 0.0f);
    ci->laneMin = pathAcross + ci->side * inner;
    ci->nearEdge = ci->side > 0 ? ci->cLo : ci->cHi;
    // Outward distance from the closest allowed lane face to the usable near edge of the cell.
    double usable = ci->side > 0 ? fmax(ci->cLo, ci->laneMin) : fmin(ci->cHi, ci->laneMin);
    double out = (usable - ci->laneMin) * ci->side;
    double room = ci->side > 0 ? ci->cHi - usable : usable - ci->cLo;
    ci->wall = out <= p->wallBand;
    ci->r = pcg32_seed(ct_hash64_3(pc->envSeed, (uint64_t)(int64_t)ix, (uint64_t)(int64_t)iz));
    // Back rows thin out right behind the wall row so the gaps between wall slabs stay dark.
    float ramp = (float)((out - p->wallBand) / fmax(p->backRamp, 1e-3));
    ramp = ramp < 0.0f ? 0.0f : (ramp > 1.0f ? 1.0f : ramp);
    float dens = ci->wall ? p->wallDensity * ci->arch.wallDensity : p->backDensity * ci->arch.backDensity * ramp * ramp;
    int occ = pcg32_chance(&ci->r, dens);
    int mergeRoll = pcg32_chance(&ci->r, p->mergeChance * ci->arch.merge);
    ci->occupied = ci->inBand && occ && room >= p->depthMin + 0.5;
    ci->merge = ci->occupied && ci->wall && mergeRoll && (along_index(ci->axis, ix, iz) & 1) == 0;
}

// Even cell: does it merge into its odd partner? (also needs the partner's own classification)
static int merges(const PlanCtx *pc, const LatticeWindow *w, const CellInfo *even, int ix, int iz)
{
    if (!even->merge) return 0;
    int jx = ix + (even->axis ? 0 : 1), jz = iz + (even->axis ? 1 : 0);
    CellInfo odd;
    cell_info(pc, w, jx, jz, &odd);
    return odd.inBand && odd.wall && odd.axis == even->axis && odd.side == even->side;
}

static void plan_cell(void *user, const LatticeWindow *w, const LatticeCell *cell)
{
    PlanCtx *pc = (PlanCtx *)user;
    const PillarParams *p = pc->p;
    int ix = cell->ix, iz = cell->iz, idx = -1;
    CellInfo ci;
    cell_info(pc, w, ix, iz, &ci);
    int alongIdx = along_index(ci.axis, ix, iz);
    int merged = 0;
    if (alongIdx & 1) {
        // Odd cell: empty if the even partner extends into it.
        int px = ix - (ci.axis ? 0 : 1), pz = iz - (ci.axis ? 1 : 0);
        CellInfo even;
        cell_info(pc, w, px, pz, &even);
        if (ci.wall && even.inBand && even.axis == ci.axis && merges(pc, w, &even, px, pz)) ci.occupied = 0;
    } else if (ci.occupied) {
        merged = merges(pc, w, &ci, ix, iz);
    }

    if (ci.occupied && pc->n < pc->maxPillars) {
        Pcg32 *r = &ci.r;
        PillarDesc *d = &pc->out[pc->n];
        int s = ci.side;
        double farEdge = (s > 0 ? ci.cHi : ci.cLo) - s * 0.5;
        double usable = s > 0 ? fmax(ci.cLo + 0.5, ci.laneMin) : fmin(ci.cHi - 0.5, ci.laneMin);
        double lane, depth;
        if (ci.wall) {
            lane = snap05(usable + s * pcg32_range(r, 0.0f, p->slackMax));
            if ((lane - usable) * s < 0.0) lane += s * 0.5;
        } else {
            double room = (farEdge - usable) * s - p->depthMin;
            lane = snap05(usable + s * pcg32_float(r) * fmax(room, 0.0));
            if ((lane - usable) * s < 0.0) lane += s * 0.5;
        }
        depth = snap05(pcg32_range(r, p->depthMin, p->depthMax));
        if (depth > (farEdge - lane) * s) depth = floor((farEdge - lane) * s * 2.0) * 0.5;
        // Along extent: inside the cell (or across the pair) with gaps of 0.5..2.
        double a0, a1;
        if (merged) {
            a0 = snap05(ci.aLo + pcg32_range(r, 0.5f, 2.0f));
            a1 = snap05(ci.aHi + pc->L - pcg32_range(r, 0.5f, 2.0f));
        } else {
            double wdt = snap05(pcg32_range(r, p->widthMin, p->widthMax));
            if (wdt > pc->L - 1.0) wdt = pc->L - 1.0;
            a0 = snap05(ci.aLo + 0.5 + pcg32_float(r) * (pc->L - 1.0 - wdt));
            a1 = a0 + wdt;
        }
        // Height and tiers.
        float far = (float)((ci.dist - p->laneHalfWidth) / fmax(p->bandMax - p->laneHalfWidth, 1.0));
        far = far < 0.0f ? 0.0f : (far > 1.0f ? 1.0f : far);
        float H = pcg32_range(r, p->heightMin, p->heightMax) * ci.arch.height;
        if (!ci.wall) H *= 1.0f + (p->backHeightMul - 1.0f) * far;
        int steps = ci.wall ? pcg32_int(r, p->stepsMin, p->stepsMax) : pcg32_int(r, 0, 1);
        if (steps > COR_MAX_TIERS - 1) steps = COR_MAX_TIERS - 1;
        d->tiers = steps + 1;
        // Tier tops: wall pillars step within the part of the pillar the chase camera sees
        // (4 units up to ~55 % of the height); back rows anywhere. Last = H.
        for (int k = 0; k < d->tiers; k++) {
            float y = H;
            if (k < d->tiers - 1) {
                float t = ((float)k + 0.5f + pcg32_range(r, -0.3f, 0.3f)) / (float)(d->tiers - 1);
                y = ci.wall ? 4.0f + (0.55f * H - 4.0f) * t : H * (0.3f + 0.5f * t);
            }
            d->yTop[k] = p->floorY + y;
        }
        // Lane face per tier: outward offset from `lane`, stepping by stepMin..stepMax.
        double maxOff = depth - p->depthMin * 0.75, off = 0.0;
        double offs[COR_MAX_TIERS];
        offs[0] = 0.0;
        if (ci.wall && maxOff >= 1.0) off = snap05(pcg32_float(r) * fmin(maxOff, 1.5));
        offs[0] = off;
        for (int k = 1; k < d->tiers; k++) {
            double step = snap05(pcg32_range(r, p->stepMin, p->stepMax));
            double dir = pcg32_chance(r, 0.5f) ? 1.0 : -1.0;
            double nOff = off + dir * step;
            if (nOff < 0.0 || nOff > maxOff) nOff = off - dir * step;
            if (nOff < 0.0) nOff = 0.0;
            if (nOff > maxOff) nOff = fmax(maxOff, 0.0);
            offs[k] = off = snap05(nOff);
        }
        d->outline = ci.wall && pcg32_chance(r, p->outlineChance * ci.arch.outline);
        d->hash = pcg32_next(r);
        d->ix = ix;
        d->iz = iz;
        d->sOwner = ci.sOwn;
        d->dist = (float)ci.dist;
        d->axis = ci.axis;
        d->side = s;
        d->front = ci.front;
        d->wall = ci.wall;
        d->a0 = a0;
        d->a1 = a1;
        d->baseY = p->floorY;
        // Keep clear of the lane everywhere along the path: push out in 0.5 steps if needed.
        double inner = p->laneHalfWidth + p->clearance + fmax(ci.arch.clearanceAdd, 0.0f);
        int ok = 0;
        for (int tries = 0; tries < 12 && !ok; tries++, lane += s * 0.5) {
            if ((farEdge - (lane + s * depth)) * s < -1e-9) break;
            d->cOut = lane + s * depth;
            for (int k = 0; k < d->tiers; k++) d->cLane[k] = lane + s * offs[k];
            double x0, z0, x1, z1;
            pillar_bounds(d, &x0, &z0, &x1, &z1);
            ok = lattice_rect_dist(w, x0, z0, x1, z1) >= inner;
        }
        if (ok && depth >= p->depthMin * 0.75) idx = pc->n++;
    }
    if (pc->claims && pc->nc < pc->maxClaims) pc->claims[pc->nc++] = (PillarClaim){ ix, iz, cell->sOwner, idx };
}

int pillars_plan(const PillarParams *p, const PillarArchFn *arch, uint64_t seed, const PathSystem *path, int chunkIndex,
                 PillarDesc *out, int maxPillars, PillarClaim *claims, int maxClaims, int *claimCount)
{
    static LatticeWindow w;   // large; builds run on one thread
    lattice_window_load(&w, path, chunkIndex, pillars_reach(p));
    double L = (double)p->latticeCells * p->cell;
    PlanCtx pc = { p, arch, ct_hash64_2(seed, ((uint64_t)ENV_CORRIDOR << 8) | F_PILLARS), L, out, maxPillars, 0, claims, maxClaims, 0 };
    lattice_for_each(&w, L, p->bandMax, plan_cell, &pc);
    if (claimCount) *claimCount = pc.nc;
    return pc.n;
}

DVec3 pillar_point(const PillarDesc *d, double along, double across, double y)
{
    return d->axis ? dv3(across, y, along) : dv3(along, y, across);
}

void pillar_bounds(const PillarDesc *d, double *x0, double *z0, double *x1, double *z1)
{
    double cMin = d->cOut, cMax = d->cOut;
    for (int k = 0; k < d->tiers; k++) {
        if (d->cLane[k] < cMin) cMin = d->cLane[k];
        if (d->cLane[k] > cMax) cMax = d->cLane[k];
    }
    if (d->axis) { *x0 = cMin; *x1 = cMax; *z0 = d->a0; *z1 = d->a1; }
    else { *x0 = d->a0; *x1 = d->a1; *z0 = cMin; *z1 = cMax; }
}

// ---------------------------------------------------------------------------
// Geometry

static void quad(LineGeo *g, const Vector3 p[4], const Vector2 uv[4], Vector3 n, Vector2 uv2, Vector4 tan, Color c)
{
    int b = lg_reserve(g, 4, 6);
    for (int i = 0; i < 4; i++) lg_vertex(g, p[i], uv[i], uv2, n, tan, c);
    lg_quad(g, b, 0, 1, 2, 3);
}

static Vector3 loc(DVec3 p, DVec3 o) { return dv_to_v3(dv_sub(p, o)); }

static Vector3 axis_vec(const PillarDesc *d, double along, double across)
{
    return d->axis ? (Vector3){ (float)across, 0.0f, (float)along } : (Vector3){ (float)along, 0.0f, (float)across };
}

void pillars_build(const PillarDesc *pl, int n, DVec3 o, LineGeo *body)
{
    for (int i = 0; i < n; i++) {
        const PillarDesc *d = &pl[i];
        float h01 = (float)(d->hash & 0xFFFFFF) / 16777216.0f;
        float H = d->yTop[d->tiers - 1] - d->baseY;
        uint32_t hv = d->hash >> 24;
        unsigned char dim = d->wall ? 0 : 40;
        Color tint = { (unsigned char)(150 + (hv & 31) - dim), (unsigned char)(170 + ((hv >> 2) & 31) - dim), (unsigned char)(185 + ((hv >> 3) & 31) - dim), 255 };
        Vector2 uv2 = { h01, H };
        int s = d->side;
        double aF = d->front > 0 ? d->a1 : d->a0;   // camera-facing end
        Vector3 nLane = axis_vec(d, 0.0, -s), nFront = axis_vec(d, d->front, 0.0);
        for (int k = 0; k < d->tiers; k++) {
            double yb = k == 0 ? d->baseY : d->yTop[k - 1], yt = d->yTop[k];
            float vb = (float)(yb - d->baseY), vt = (float)(yt - d->baseY);
            double cl = d->cLane[k];
            Vector4 tan = { 0.0f, (float)k, (float)d->wall, 0.0f };
            // Lane face: u along the lane from a0.
            {
                Vector3 p[4] = { loc(pillar_point(d, d->a0, cl, yb), o), loc(pillar_point(d, d->a1, cl, yb), o),
                                 loc(pillar_point(d, d->a1, cl, yt), o), loc(pillar_point(d, d->a0, cl, yt), o) };
                Vector2 uv[4] = { { 0.0f, vb }, { (float)(d->a1 - d->a0), vb }, { (float)(d->a1 - d->a0), vt }, { 0.0f, vt } };
                tan.x = 0.0f;
                quad(body, p, uv, nLane, uv2, tan, tint);
            }
            // Camera-facing face: u across from the outer face.
            {
                Vector3 p[4] = { loc(pillar_point(d, aF, d->cOut, yb), o), loc(pillar_point(d, aF, cl, yb), o),
                                 loc(pillar_point(d, aF, cl, yt), o), loc(pillar_point(d, aF, d->cOut, yt), o) };
                float w = (float)fabs(d->cOut - cl);
                Vector2 uv[4] = { { 0.0f, vb }, { w, vb }, { w, vt }, { 0.0f, vt } };
                tan.x = 1.0f;
                quad(body, p, uv, nFront, uv2, tan, tint);
            }
            // Underside of an overhang: this tier reaches closer to the lane than the one below.
            if (k > 0 && (d->cLane[k - 1] - cl) * s > 1e-6) {
                double cb = d->cLane[k - 1];
                Vector3 p[4] = { loc(pillar_point(d, d->a0, cl, yb), o), loc(pillar_point(d, d->a0, cb, yb), o),
                                 loc(pillar_point(d, d->a1, cb, yb), o), loc(pillar_point(d, d->a1, cl, yb), o) };
                Vector2 uv[4] = { { 0.0f, 0.0f }, { (float)fabs(cb - cl), 0.0f }, { (float)fabs(cb - cl), (float)(d->a1 - d->a0) }, { 0.0f, (float)(d->a1 - d->a0) } };
                tan.x = 4.0f;
                quad(body, p, uv, (Vector3){ 0.0f, -1.0f, 0.0f }, uv2, tan, tint);
            }
            // Ledge on top of this tier where the next one steps back from the lane.
            if (k + 1 < d->tiers && (d->cLane[k + 1] - cl) * s > 1e-6) {
                double cu = d->cLane[k + 1];
                Vector3 p[4] = { loc(pillar_point(d, d->a0, cl, yt), o), loc(pillar_point(d, d->a1, cl, yt), o),
                                 loc(pillar_point(d, d->a1, cu, yt), o), loc(pillar_point(d, d->a0, cu, yt), o) };
                Vector2 uv[4] = { { 0.0f, 0.0f }, { (float)(d->a1 - d->a0), 0.0f }, { (float)(d->a1 - d->a0), (float)fabs(cu - cl) }, { 0.0f, (float)fabs(cu - cl) } };
                tan.x = 4.0f;
                quad(body, p, uv, (Vector3){ 0.0f, 1.0f, 0.0f }, uv2, tan, tint);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Decor: code rain panels, stepped outlines, floor kerbs

static const Color COL_OUTLINE = { 0xC8, 0xF4, 0xFF, 255 };

static void rain_quad(LineGeo *g, const Vector3 p[4], float w, float vb, float vt, Vector3 n, float hash, float face, float top,
                      float intensity, float wall)
{
    int b = lg_reserve(g, 4, 6);
    Vector4 t = { w, top, intensity, wall };
    Vector2 uv2 = { hash, face };
    Vector2 uv[4] = { { 0.0f, vb }, { w, vb }, { w, vt }, { 0.0f, vt } };
    for (int i = 0; i < 4; i++) lg_vertex(g, p[i], uv[i], uv2, n, t, WHITE);
    lg_quad(g, b, 0, 1, 2, 3);
}

void pillars_build_decor(const PillarDecorParams *p, const PillarDesc *pl, int n, DVec3 o,
                         LineGeo *rain, LineGeo *outlines, LineGeo *kerbs)
{
    for (int i = 0; i < n; i++) {
        const PillarDesc *d = &pl[i];
        Pcg32 r = pcg32_seed(ct_hash64_2(d->hash, 2));
        float h01 = (float)(d->hash & 0xFFFFFF) / 16777216.0f;
        int s = d->side;
        double aF = d->front > 0 ? d->a1 : d->a0;
        double off = p->rainOffset;
        float inten = p->rainIntensity * (d->wall ? 1.0f : p->backRain);
        Vector3 nLane = axis_vec(d, 0.0, -s), nFront = axis_vec(d, d->front, 0.0);
        float top = d->yTop[d->tiers - 1] - d->baseY;
        if (rain && inten > 0.0f) {
            for (int k = 0; k < d->tiers; k++) {
                double yb = k == 0 ? d->baseY : d->yTop[k - 1], yt = d->yTop[k];
                float vb = (float)(yb - d->baseY), vt = (float)(yt - d->baseY);
                double cl = d->cLane[k] - s * off;
                // Lane face, u along from a0 (all tiers share it, so columns line up).
                Vector3 pa[4] = { loc(pillar_point(d, d->a0, cl, yb), o), loc(pillar_point(d, d->a1, cl, yb), o),
                                  loc(pillar_point(d, d->a1, cl, yt), o), loc(pillar_point(d, d->a0, cl, yt), o) };
                rain_quad(rain, pa, (float)(d->a1 - d->a0), vb, vt, nLane, h01, 0.0f, top, inten * p->laneRain, (float)d->wall);
                // Camera-facing face, u across from the outer face.
                double af = aF + d->front * off;
                Vector3 pf[4] = { loc(pillar_point(d, af, d->cOut, yb), o), loc(pillar_point(d, af, d->cLane[k], yb), o),
                                  loc(pillar_point(d, af, d->cLane[k], yt), o), loc(pillar_point(d, af, d->cOut, yt), o) };
                rain_quad(rain, pf, (float)fabs(d->cOut - d->cLane[k]), vb, vt, nFront, h01, 1.0f, top, inten, (float)d->wall);
            }
        }
        if (!d->outline) continue;
        // Outline polyline in the camera-facing plane: (across, y) corners.
        enum { MAXP = 2 * COR_MAX_TIERS + 4 };
        double pc[MAXP], py[MAXP];
        int np = 0;
        double eo = p->outlineOffset, af = aF + d->front * eo, y0 = p->floorY + 0.03;
        pc[np] = d->cLane[0] - s * eo; py[np++] = y0;
        for (int k = 0; k < d->tiers; k++) {
            double c = d->cLane[k] - s * eo;
            if (k > 0 && fabs(c - pc[np - 1]) > 1e-6) { pc[np] = c; py[np] = py[np - 1]; np++; }
            pc[np] = c; py[np++] = d->yTop[k];
        }
        double depth = fabs(d->cOut - d->cLane[d->tiers - 1]);
        double frac = pcg32_range(&r, p->outlineTopMin, 1.0f);
        pc[np] = pc[np - 1] + s * (depth + eo) * frac; py[np] = py[np - 1]; np++;
        Vector3 pts[MAXP], acr[MAXP];
        Vector3 up = { 0.0f, 1.0f, 0.0f };
        for (int j = 0; j < np; j++) {
            pts[j] = loc(pillar_point(d, af, pc[j], py[j]), o);
            // In-plane normals of the adjacent segments; their sum is the miter at a right angle.
            Vector3 m = { 0.0f, 0.0f, 0.0f };
            for (int e = -1; e <= 0; e++) {
                int a = j + e, b = a + 1;
                if (a < 0 || b >= np) continue;
                // Normal = direction turned toward the outside of the silhouette: up the lane edge it
                // points at the lane, on an outward step up (ledge), on an inward step down (overhang).
                int vertical = fabs(py[b] - py[a]) > fabs(pc[b] - pc[a]);
                Vector3 nn = vertical ? axis_vec(d, 0.0, -s) : ((pc[b] - pc[a]) * s > 0.0 ? up : (Vector3){ 0.0f, -1.0f, 0.0f });
                m.x += nn.x; m.y += nn.y; m.z += nn.z;
            }
            acr[j] = m;
        }
        LineAttr at = { COL_OUTLINE, p->outlineIntensity, pcg32_float(&r), 0.0f, -1.0f, 0.0f };
        if (outlines) lg_planar_polyline(outlines, pts, acr, np, p->outlineWidth, at);
        // Floor kerb from the outline's foot along the lane toward the camera.
        if (kerbs) {
            double len = pcg32_range(&r, p->kerbMin, p->kerbMax);
            Vector3 ka = loc(pillar_point(d, af, d->cLane[0] - s * eo, y0), o);
            Vector3 kb = loc(pillar_point(d, af + d->front * len, d->cLane[0] - s * eo, y0), o);
            LineAttr ka2 = { COL_OUTLINE, p->outlineIntensity, at.hash, 0.0f, -1.0f, 0.0f };
            lg_facing_segment(kerbs, ka, kb, p->kerbWidth, p->kerbWidth, p->outlineIntensity, p->outlineIntensity * 0.5f, ka2);
        }
    }
}
