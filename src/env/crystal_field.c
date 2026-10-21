#include "crystal_field.h"
#include "core/rng.h"
#include "raymath.h"
#include <math.h>
#include <stdbool.h>
#include <string.h>

#define CF_PI 3.14159265358979323846
#define CF_DIST_STEP 2.0      // path sampling step of the keep-out search

static ShapeSolid g_solids[CF_KIND_COUNT];
static bool       g_solidsReady;

// Edges and triangles of the convex hull of s->v (scaled to circumradius 1). Coplanar
// vertices form one polygon: its outline becomes edges and a triangle fan fills it.
static void solid_hull(ShapeSolid *s)
{
    float rmax = 0.0f;
    for (int i = 0; i < s->nv; i++) rmax = fmaxf(rmax, Vector3Length(s->v[i]));
    for (int i = 0; i < s->nv; i++) s->v[i] = Vector3Scale(s->v[i], 1.0f / rmax);

    const float eps = 1e-4f;
    Vector3 planesN[SHAPE_SOLID_MAX_F];
    float planesD[SHAPE_SOLID_MAX_F];
    int np = 0;
    s->ne = s->nf = 0;
    for (int i = 0; i < s->nv; i++)
    for (int j = i + 1; j < s->nv; j++)
    for (int k = j + 1; k < s->nv; k++) {
        Vector3 n = Vector3CrossProduct(Vector3Subtract(s->v[j], s->v[i]), Vector3Subtract(s->v[k], s->v[i]));
        if (Vector3Length(n) < eps) continue;
        n = Vector3Normalize(n);
        float d = Vector3DotProduct(n, s->v[i]);
        bool above = false, below = false;
        for (int m = 0; m < s->nv; m++) {
            float h = Vector3DotProduct(n, s->v[m]) - d;
            if (h > eps) above = true;
            if (h < -eps) below = true;
        }
        if (above && below) continue;
        if (above) { n = Vector3Negate(n); d = -d; }
        bool dup = false;
        for (int q = 0; q < np && !dup; q++)
            dup = Vector3Distance(planesN[q], n) < 1e-3f && fabsf(planesD[q] - d) < 1e-3f;
        if (dup) continue;
        planesN[np] = n;
        planesD[np] = d;
        np++;

        // Polygon on this plane, sorted counter-clockwise seen from outside.
        int poly[SHAPE_SOLID_MAX_V], pn = 0;
        Vector3 cen = { 0 };
        for (int m = 0; m < s->nv; m++)
            if (fabsf(Vector3DotProduct(n, s->v[m]) - d) < eps) { poly[pn++] = m; cen = Vector3Add(cen, s->v[m]); }
        cen = Vector3Scale(cen, 1.0f / (float)pn);
        Vector3 u = Vector3Normalize(Vector3Subtract(s->v[poly[0]], cen)), w = Vector3CrossProduct(n, u);
        float ang[SHAPE_SOLID_MAX_V];
        for (int m = 0; m < pn; m++) {
            Vector3 q = Vector3Subtract(s->v[poly[m]], cen);
            ang[m] = atan2f(Vector3DotProduct(q, w), Vector3DotProduct(q, u));
        }
        for (int a = 1; a < pn; a++)
            for (int b = a; b > 0 && ang[b] < ang[b - 1]; b--) {
                float t = ang[b]; ang[b] = ang[b - 1]; ang[b - 1] = t;
                int ti = poly[b]; poly[b] = poly[b - 1]; poly[b - 1] = ti;
            }
        for (int m = 0; m < pn; m++) {
            int a = poly[m], b = poly[(m + 1) % pn], lo = a < b ? a : b, hi = a < b ? b : a;
            bool have = false;
            for (int q = 0; q < s->ne && !have; q++) have = s->e[q][0] == lo && s->e[q][1] == hi;
            if (!have) { s->e[s->ne][0] = lo; s->e[s->ne][1] = hi; s->ne++; }
        }
        for (int m = 1; m + 1 < pn; m++) {
            s->f[s->nf][0] = poly[0]; s->f[s->nf][1] = poly[m]; s->f[s->nf][2] = poly[m + 1];
            s->nf++;
        }
    }
}

static void solids_init(void)
{
    if (g_solidsReady) return;
    ShapeSolid *s;
    s = &g_solids[CF_TETRA];
    s->nv = 4;
    s->v[0] = (Vector3){ 1, 1, 1 }; s->v[1] = (Vector3){ 1, -1, -1 }; s->v[2] = (Vector3){ -1, 1, -1 }; s->v[3] = (Vector3){ -1, -1, 1 };
    s = &g_solids[CF_OCTA];
    s->nv = 6;
    for (int i = 0; i < 6; i++) { float v[3] = { 0, 0, 0 }; v[i / 2] = (i & 1) ? -1.0f : 1.0f; s->v[i] = (Vector3){ v[0], v[1], v[2] }; }
    s = &g_solids[CF_ICOSA];
    s->nv = 12;
    const float phi = 1.6180339887f;
    for (int i = 0; i < 4; i++) {
        float a = (i & 1) ? -1.0f : 1.0f, b = (i & 2) ? -phi : phi;
        s->v[i] = (Vector3){ 0, a, b };
        s->v[4 + i] = (Vector3){ a, b, 0 };
        s->v[8 + i] = (Vector3){ b, 0, a };
    }
    s = &g_solids[CF_CUBE];
    s->nv = 8;
    for (int i = 0; i < 8; i++) s->v[i] = (Vector3){ (i & 1) ? 1.0f : -1.0f, (i & 2) ? 1.0f : -1.0f, (i & 4) ? 1.0f : -1.0f };
    s = &g_solids[CF_PRISM];
    s->nv = 6;
    for (int i = 0; i < 3; i++) {
        float a = (float)(CF_PI * 0.5 + 2.0 * CF_PI * i / 3.0);
        s->v[i] = (Vector3){ cosf(a), sinf(a), 0.9f };
        s->v[3 + i] = (Vector3){ cosf(a), sinf(a), -0.9f };
    }
    s = &g_solids[CF_BIPYRAMID];
    s->nv = 8;
    for (int i = 0; i < 6; i++) {
        float a = (float)(2.0 * CF_PI * i / 6.0);
        s->v[i] = (Vector3){ 0.55f * cosf(a), 0.55f * sinf(a), 0.0f };
    }
    s->v[6] = (Vector3){ 0, 0, 1 };
    s->v[7] = (Vector3){ 0, 0, -1 };
    for (int k = 0; k < CF_KIND_COUNT; k++) solid_hull(&g_solids[k]);
    g_solidsReady = true;
}

const ShapeSolid *crystal_solid(int kind)
{
    solids_init();
    return &g_solids[kind];
}

double crystal_keepout(const CrystalFieldParams *p)
{
    return sqrt(2.0) * p->steerLimit * p->laneRadius + p->clearance;
}

double crystal_path_reach(const CrystalFieldParams *p)
{
    return 2.0 * (p->bandMax + p->clusterRadMax + p->giantMax * 3.0) + 20.0;
}

double crystal_path_distance(const PathSystem *path, DVec3 p, double sCenter, double reach, DVec3 *nearest)
{
    double best = 1e300;
    double sa = fmax(0.0, sCenter - reach), sb = sCenter + reach;
    for (double s = sa; s <= sb; s += CF_DIST_STEP) {
        DVec3 q = path_sample(path, s).pos;
        double d = dv_dot(dv_sub(p, q), dv_sub(p, q));
        if (d < best) { best = d; if (nearest) *nearest = q; }
    }
    // The true nearest point lies within half a step of a sample: never overestimate.
    double h = 0.5 * CF_DIST_STEP;
    return sqrt(fmax(0.0, best - h * h));
}

static int poisson_count(Pcg32 *r, float mean)
{
    int n = (int)floorf(mean);
    return n + pcg32_chance(r, mean - (float)n);
}

// Uniform random rotation (Shoemake) as three orthonormal axes.
static void random_axes(Pcg32 *r, Vector3 *x, Vector3 *y, Vector3 *z)
{
    float u1 = pcg32_float(r), u2 = pcg32_float(r) * 6.2831853f, u3 = pcg32_float(r) * 6.2831853f;
    float a = sqrtf(1.0f - u1), b = sqrtf(u1);
    Quaternion q = { a * sinf(u2), a * cosf(u2), b * sinf(u3), b * cosf(u3) };
    *x = Vector3RotateByQuaternion((Vector3){ 1, 0, 0 }, q);
    *y = Vector3RotateByQuaternion((Vector3){ 0, 1, 0 }, q);
    *z = Vector3RotateByQuaternion((Vector3){ 0, 0, 1 }, q);
}

static const Color CF_ORANGE = { 0xFF, 0x8C, 0x2E, 255 };
static const Color CF_AMBER  = { 0xFF, 0xB0, 0x4A, 255 };
static const Color CF_GOLD   = { 0xFF, 0xD8, 0x90, 255 };

static CrystalShape make_shape(const CrystalFieldParams *p, Pcg32 *r, DVec3 pos)
{
    CrystalShape sh;
    memset(&sh, 0, sizeof(sh));
    static const float kindW[CF_KIND_COUNT] = { 0.12f, 0.25f, 0.30f, 0.05f, 0.13f, 0.15f };
    sh.kind = pcg32_weighted(r, kindW, CF_KIND_COUNT);
    float cls = pcg32_float(r);
    if (cls < p->giantFrac) sh.size = pcg32_range(r, p->giantMin, p->giantMax);
    else if (cls < p->giantFrac + p->mediumFrac) sh.size = pcg32_range(r, p->mediumMin, p->mediumMax);
    else sh.size = pcg32_range(r, p->smallMin, p->smallMax);
    // Bipyramids are long crystals; a few other solids are stretched or squashed on one axis.
    float st[3] = { 1.0f, 1.0f, 1.0f };
    if (sh.kind == CF_BIPYRAMID) st[2] = pcg32_range(r, 1.6f, 2.6f);
    else if (pcg32_chance(r, 0.15f)) st[pcg32_int(r, 0, 2)] = pcg32_range(r, 0.7f, 1.6f);
    random_axes(r, &sh.ax, &sh.ay, &sh.az);
    sh.ax = Vector3Scale(sh.ax, sh.size * st[0]);
    sh.ay = Vector3Scale(sh.ay, sh.size * st[1]);
    sh.az = Vector3Scale(sh.az, sh.size * st[2]);
    sh.bound = sh.size * fmaxf(st[0], fmaxf(st[1], st[2]));
    float c = pcg32_float(r);
    sh.color = c < 0.70f ? CF_ORANGE : (c < 0.95f ? CF_AMBER : CF_GOLD);
    sh.intensity = p->edgeIntensity * pcg32_range(r, 0.7f, 1.2f);
    sh.hash = pcg32_float(r);
    float z = pcg32_range(r, -1.0f, 1.0f), a = pcg32_float(r) * 6.2831853f, q = sqrtf(1.0f - z * z);
    sh.spinAxis = (Vector3){ q * cosf(a), q * sinf(a), z };
    sh.spinSpeed = p->tumble * pcg32_range(r, 0.3f, 1.0f) / (1.0f + 0.15f * sh.size) * (pcg32_chance(r, 0.5f) ? 1.0f : -1.0f);
    sh.spinPhase = pcg32_float(r) * 6.2831853f;
    sh.pos = pos;
    return sh;
}

// Push the shape straight away from the nearest path point until its bounding sphere clears
// the keep-out. False if it still does not clear (the shape is dropped).
static bool clear_lane(const CrystalFieldParams *p, const PathSystem *path, double sAnchor, DVec3 anchor, CrystalShape *sh)
{
    double need = crystal_keepout(p) + sh->bound, maxReach = crystal_path_reach(p);
    for (int it = 0; it < 4; it++) {
        double reach = fmin(maxReach, 2.0 * dv_len(dv_sub(sh->pos, anchor)) + sh->bound + 20.0);
        DVec3 q = anchor;
        double d = crystal_path_distance(path, sh->pos, sAnchor, reach, &q);
        if (d >= need) return true;
        DVec3 away = dv_sub(sh->pos, q);
        if (dv_len(away) < 1e-6) return false;
        sh->pos = dv_madd(sh->pos, dv_norm(away), need - d + 0.25);
    }
    return false;
}

int crystal_field_plan(const CrystalFieldParams *p, const PathSystem *path, uint64_t base, double s0, double s1,
                       CrystalShape *out, int max)
{
    Pcg32 r = pcg32_seed(base);
    int nc = poisson_count(&r, p->clusters), ns = poisson_count(&r, p->scatter);
    double keep = crystal_keepout(p);
    int n = 0;
    for (int g = 0; g < nc + ns && n < max; g++) {
        // Each group draws from its own stream, so a change in counts leaves the others in place.
        Pcg32 gr = pcg32_seed(ct_hash64_2(base, (uint64_t)g + 1));
        bool cluster = g < nc;
        double s = s0 + (s1 - s0) * pcg32_float(&gr);
        PathSample ps = path_sample(path, s);
        float crad = cluster ? pcg32_range(&gr, p->clusterRadMin, p->clusterRadMax) : 0.0f;
        double theta;
        if (pcg32_chance(&gr, p->sideBias)) theta = (pcg32_chance(&gr, 0.5f) ? 0.0 : CF_PI) + (pcg32_float(&gr) - 0.5f) * 1.2f;
        else theta = pcg32_float(&gr) * 2.0 * CF_PI;
        double rmin = keep + 0.5 * crad + 1.0, rmax = fmax(rmin, (double)p->bandMax);
        double rc = rmin + (rmax - rmin) * pow(pcg32_float(&gr), p->nearBias);
        DVec3 dir = dv_add(dv_scale(ps.B, cos(theta)), dv_scale(ps.N, sin(theta)));
        DVec3 centre = dv_madd(ps.pos, dir, rc);
        int count = cluster ? pcg32_int(&gr, p->clusterMin, p->clusterMax) : 1;
        for (int i = 0; i < count && n < max; i++) {
            DVec3 pos = centre;
            if (cluster) {
                // Uniform in the cluster ball, in the path frame.
                double x, y, z;
                do { x = pcg32_float(&gr) * 2.0 - 1.0; y = pcg32_float(&gr) * 2.0 - 1.0; z = pcg32_float(&gr) * 2.0 - 1.0; }
                while (x * x + y * y + z * z > 1.0);
                pos = dv_add(pos, dv_add(dv_scale(ps.B, x * crad), dv_add(dv_scale(ps.N, y * crad), dv_scale(ps.T, z * crad))));
            }
            CrystalShape sh = make_shape(p, &gr, pos);
            if (clear_lane(p, path, s, ps.pos, &sh)) out[n++] = sh;
        }
    }
    return n;
}

void crystal_field_build(const CrystalFieldParams *p, const CrystalShape *s, int n, DVec3 origin,
                         LineGeo *edges, LineGeo *faces, float (*uniforms)[4])
{
    for (int i = 0; i < n; i++) {
        const CrystalShape *sh = &s[i];
        Vector3 c = dv_to_v3(dv_sub(sh->pos, origin));
        float w = fmaxf(p->edgeWidthMin, p->edgeWidth * sh->size);
        LineAttr e = { sh->color, sh->intensity, sh->hash, 0.0f, -1.0f, 0.0f };
        if (i < CF_TUMBLE_SLOTS) {
            float *a = uniforms[i], *b = uniforms[CF_TUMBLE_SLOTS + i];
            a[0] = c.x; a[1] = c.y; a[2] = c.z; a[3] = sh->spinPhase;
            b[0] = sh->spinAxis.x; b[1] = sh->spinAxis.y; b[2] = sh->spinAxis.z; b[3] = sh->spinSpeed;
            e.group = (float)i;
            c = (Vector3){ 0 };
        }
        LineAttr f = e;
        f.intensity = sh->intensity * p->faceIntensity;
        shape_solid(edges, faces, crystal_solid(sh->kind), c, sh->ax, sh->ay, sh->az, e, f, w);
    }
}
