#include "city_towers.h"
#include "env.h"
#include "core/chunks.h"
#include "core/rng.h"
#include <math.h>

#define SAMPLE_STEP 2.0      // global path sample spacing used for nearest-point queries
#define MAX_SAMPLES 2048

enum { F_TOWERS = 1 };       // feature sub-streams of the city's per-cell RNG

typedef struct Poly {
    double x[MAX_SAMPLES], z[MAX_SAMPLES];
    int64_t m0;              // global index of sample 0 (s = m * SAMPLE_STEP)
    int n;
} Poly;

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
    float t = b->len > 0.0 ? (float)((s - b->s0) / b->len) : 1.0f;
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    t = t * t * (3.0f - 2.0f * t);
    CityArch r;
    const float *pa = &b->prev.densityNear, *pb = &b->cur.densityNear;
    float *pr = &r.densityNear;
    for (int i = 0; i < (int)(sizeof(CityArch) / sizeof(float)); i++) pr[i] = pa[i] + (pb[i] - pa[i]) * t;
    return r;
}

double city_towers_reach(const CityTowerParams *p)
{
    // A cell within bandMax of the chunk's path piece has its nearest path point within
    // bandMax as well, i.e. within 2*bandMax (Euclidean) of the piece. With the heading
    // bounded (|yaw| < ~25 deg off-axis) arc length <= chord / cos(25) < 1.1 * chord.
    return 2.2 * p->bandMax + 2.0 * SAMPLE_STEP;
}

// Nearest point on samples [i0, i1] (segments i..i+1) to (cx, cz).
static double nearest(const Poly *pl, int i0, int i1, double cx, double cz, double *sOut)
{
    double best = 1e300, bs = 0.0;
    for (int i = i0; i < i1; i++) {
        double ax = pl->x[i], az = pl->z[i];
        double dx = pl->x[i + 1] - ax, dz = pl->z[i + 1] - az;
        double l2 = dx * dx + dz * dz;
        double t = l2 > 0.0 ? ((cx - ax) * dx + (cz - az) * dz) / l2 : 0.0;
        t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
        double ex = ax + dx * t - cx, ez = az + dz * t - cz;
        double d2 = ex * ex + ez * ez;
        if (d2 < best) { best = d2; bs = ((double)(pl->m0 + i) + t) * SAMPLE_STEP; }
    }
    *sOut = bs;
    return sqrt(best);
}

static double pt_seg_dist(double px, double pz, double ax, double az, double bx, double bz)
{
    double dx = bx - ax, dz = bz - az, l2 = dx * dx + dz * dz;
    double t = l2 > 0.0 ? ((px - ax) * dx + (pz - az) * dz) / l2 : 0.0;
    t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
    double ex = ax + dx * t - px, ez = az + dz * t - pz;
    return sqrt(ex * ex + ez * ez);
}

static double pt_rect_dist(double px, double pz, double x0, double z0, double x1, double z1)
{
    double dx = px < x0 ? x0 - px : (px > x1 ? px - x1 : 0.0);
    double dz = pz < z0 ? z0 - pz : (pz > z1 ? pz - z1 : 0.0);
    return sqrt(dx * dx + dz * dz);
}

static int seg_hits_rect(double ax, double az, double bx, double bz, double x0, double z0, double x1, double z1)
{
    // Liang-Barsky clip of the segment against the rectangle.
    double t0 = 0.0, t1 = 1.0, dx = bx - ax, dz = bz - az;
    double p[4] = { -dx, dx, -dz, dz }, q[4] = { ax - x0, x1 - ax, az - z0, z1 - az };
    for (int i = 0; i < 4; i++) {
        if (p[i] == 0.0) { if (q[i] < 0.0) return 0; continue; }
        double r = q[i] / p[i];
        if (p[i] < 0.0) { if (r > t1) return 0; if (r > t0) t0 = r; }
        else { if (r < t0) return 0; if (r < t1) t1 = r; }
    }
    return 1;
}

// Exact distance from the path polyline (whole window) to an axis-aligned rectangle.
static double poly_rect_dist(const Poly *pl, double x0, double z0, double x1, double z1)
{
    double best = 1e300;
    const double cx[4] = { x0, x1, x1, x0 }, cz[4] = { z0, z0, z1, z1 };
    for (int i = 0; i + 1 < pl->n; i++) {
        double ax = pl->x[i], az = pl->z[i], bx = pl->x[i + 1], bz = pl->z[i + 1];
        if (seg_hits_rect(ax, az, bx, bz, x0, z0, x1, z1)) return 0.0;
        double d = pt_rect_dist(ax, az, x0, z0, x1, z1);
        if (d < best) best = d;
        for (int c = 0; c < 4; c++) {
            d = pt_seg_dist(cx[c], cz[c], ax, az, bx, bz);
            if (d < best) best = d;
        }
    }
    return best;
}

int city_towers_plan(const CityTowerParams *p, const CityArchBlend *blend, uint64_t seed, const PathSystem *path,
                     int chunkIndex, TowerDesc *out, int maxTowers, CellClaim *claims, int maxClaims, int *claimCount)
{
    static Poly pl;   // large; builds run on one thread
    double s0 = chunkIndex * CHUNK_LEN, s1 = s0 + CHUNK_LEN;
    double reach = city_towers_reach(p);
    int64_t mLo = (int64_t)floor((s0 - reach) / SAMPLE_STEP), mHi = (int64_t)ceil((s1 + reach) / SAMPLE_STEP);
    if (mLo < 0) mLo = 0;
    if (mHi - mLo + 1 > MAX_SAMPLES) mHi = mLo + MAX_SAMPLES - 1;
    pl.m0 = mLo;
    pl.n = (int)(mHi - mLo + 1);
    for (int i = 0; i < pl.n; i++) {
        PathSample q = path_sample(path, (double)(mLo + i) * SAMPLE_STEP);
        pl.x[i] = q.pos.x;
        pl.z[i] = q.pos.z;
    }
    // The chunk's own piece of the polyline.
    int p0 = (int)((int64_t)floor(s0 / SAMPLE_STEP) - mLo), p1 = (int)((int64_t)ceil(s1 / SAMPLE_STEP) - mLo);
    if (p0 < 0) p0 = 0;
    if (p1 > pl.n - 1) p1 = pl.n - 1;

    double L = (double)p->latticeCells * p->cell;
    double minx = 1e300, maxx = -1e300, minz = 1e300, maxz = -1e300;
    for (int i = p0; i <= p1; i++) {
        if (pl.x[i] < minx) minx = pl.x[i];
        if (pl.x[i] > maxx) maxx = pl.x[i];
        if (pl.z[i] < minz) minz = pl.z[i];
        if (pl.z[i] > maxz) maxz = pl.z[i];
    }
    double pad = p->bandMax + L;
    int ix0 = (int)floor((minx - pad) / L), ix1 = (int)floor((maxx + pad) / L);
    int iz0 = (int)floor((minz - pad) / L), iz1 = (int)floor((maxz + pad) / L);

    uint64_t envSeed = ct_hash64_2(seed, ((uint64_t)ENV_CITY << 8) | F_TOWERS);
    int n = 0, nc = 0;
    for (int iz = iz0; iz <= iz1; iz++) {
        for (int ix = ix0; ix <= ix1; ix++) {
            double cx = (ix + 0.5) * L, cz = (iz + 0.5) * L, sOwn;
            if (nearest(&pl, p0, p1, cx, cz, &sOwn) > p->bandMax) continue;
            double d = nearest(&pl, 0, pl.n - 1, cx, cz, &sOwn);
            if (chunk_index_at(sOwn) != chunkIndex || d > p->bandMax) continue;

            int towerIdx = -1;
            Pcg32 r = pcg32_seed(ct_hash64_3(envSeed, (uint64_t)(int64_t)ix, (uint64_t)(int64_t)iz));
            CityArch a = city_arch_at(blend, sOwn);
            float inner = p->laneHalfWidth + p->clearance + fmaxf(a.clearanceAdd, 0.0f);
            float far = smooth01(inner, fmaxf(p->bandMax, inner + 1.0f), (float)d);
            float dn = p->densityNear * a.densityNear, df = p->densityFar * a.densityFar;
            float dens = dn + (df - dn) * far;
            if (pcg32_chance(&r, dens) && n < maxTowers) {
                TowerDesc *t = &out[n];
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
                if (poly_rect_dist(&pl, t->x0[0], t->z0[0], t->x1[0], t->z1[0]) >= inner) {
                    t->ix = ix;
                    t->iz = iz;
                    t->sOwner = sOwn;
                    t->dist = (float)d;
                    t->traceMul = a.traces;
                    t->glyphMul = a.glyphs;
                    towerIdx = n++;
                }
            }
            if (claims && nc < maxClaims) claims[nc++] = (CellClaim){ ix, iz, sOwn, (float)d, towerIdx };
        }
    }
    if (claimCount) *claimCount = nc;
    return n;
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
