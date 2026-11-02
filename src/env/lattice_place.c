#include "lattice_place.h"
#include "core/chunks.h"
#include <math.h>

double lattice_reach(double bandMax)
{
    // A cell within bandMax of the chunk's path piece has its nearest path point within
    // bandMax as well, i.e. within 2*bandMax (Euclidean) of the piece. With the heading
    // bounded (|yaw| < ~25 deg off-axis) arc length <= chord / cos(25) < 1.1 * chord.
    return 2.2 * bandMax + 2.0 * LATTICE_SAMPLE_STEP;
}

void lattice_window_load(LatticeWindow *w, const PathSystem *path, int chunkIndex, double reach)
{
    double s0 = chunkIndex * CHUNK_LEN, s1 = s0 + CHUNK_LEN;
    int64_t mLo = (int64_t)floor((s0 - reach) / LATTICE_SAMPLE_STEP), mHi = (int64_t)ceil((s1 + reach) / LATTICE_SAMPLE_STEP);
    if (mLo < 0) mLo = 0;
    if (mHi - mLo + 1 > LATTICE_MAX_SAMPLES) mHi = mLo + LATTICE_MAX_SAMPLES - 1;
    w->m0 = mLo;
    w->n = (int)(mHi - mLo + 1);
    w->chunkIndex = chunkIndex;
    for (int i = 0; i < w->n; i++) {
        PathSample q = path_sample(path, (double)(mLo + i) * LATTICE_SAMPLE_STEP);
        w->x[i] = q.pos.x;
        w->z[i] = q.pos.z;
    }
    int p0 = (int)((int64_t)floor(s0 / LATTICE_SAMPLE_STEP) - mLo), p1 = (int)((int64_t)ceil(s1 / LATTICE_SAMPLE_STEP) - mLo);
    if (p0 < 0) p0 = 0;
    if (p1 > w->n - 1) p1 = w->n - 1;
    w->p0 = p0;
    w->p1 = p1;
}

// Nearest point on samples [i0, i1] (segments i..i+1) to (cx, cz).
static double nearest(const LatticeWindow *w, int i0, int i1, double cx, double cz, double *sOut)
{
    double best = 1e300, bs = 0.0;
    for (int i = i0; i < i1; i++) {
        double ax = w->x[i], az = w->z[i];
        double dx = w->x[i + 1] - ax, dz = w->z[i + 1] - az;
        double l2 = dx * dx + dz * dz;
        double t = l2 > 0.0 ? ((cx - ax) * dx + (cz - az) * dz) / l2 : 0.0;
        t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
        double ex = ax + dx * t - cx, ez = az + dz * t - cz;
        double d2 = ex * ex + ez * ez;
        if (d2 < best) { best = d2; bs = ((double)(w->m0 + i) + t) * LATTICE_SAMPLE_STEP; }
    }
    *sOut = bs;
    return sqrt(best);
}

double lattice_nearest(const LatticeWindow *w, double x, double z, double *sOut) { return nearest(w, 0, w->n - 1, x, z, sOut); }

void lattice_for_each(const LatticeWindow *w, double L, double bandMax, LatticeCellFn fn, void *user)
{
    double minx = 1e300, maxx = -1e300, minz = 1e300, maxz = -1e300;
    for (int i = w->p0; i <= w->p1; i++) {
        if (w->x[i] < minx) minx = w->x[i];
        if (w->x[i] > maxx) maxx = w->x[i];
        if (w->z[i] < minz) minz = w->z[i];
        if (w->z[i] > maxz) maxz = w->z[i];
    }
    double pad = bandMax + L;
    int ix0 = (int)floor((minx - pad) / L), ix1 = (int)floor((maxx + pad) / L);
    int iz0 = (int)floor((minz - pad) / L), iz1 = (int)floor((maxz + pad) / L);
    for (int iz = iz0; iz <= iz1; iz++) {
        for (int ix = ix0; ix <= ix1; ix++) {
            double cx = (ix + 0.5) * L, cz = (iz + 0.5) * L, sOwn;
            if (nearest(w, w->p0, w->p1, cx, cz, &sOwn) > bandMax) continue;
            double d = nearest(w, 0, w->n - 1, cx, cz, &sOwn);
            if (chunk_index_at(sOwn) != w->chunkIndex || d > bandMax) continue;
            LatticeCell c = { ix, iz, cx, cz, sOwn, (float)d };
            fn(user, w, &c);
        }
    }
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

double lattice_rect_dist(const LatticeWindow *w, double x0, double z0, double x1, double z1)
{
    double best = 1e300;
    const double cx[4] = { x0, x1, x1, x0 }, cz[4] = { z0, z0, z1, z1 };
    for (int i = 0; i + 1 < w->n; i++) {
        double ax = w->x[i], az = w->z[i], bx = w->x[i + 1], bz = w->z[i + 1];
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
