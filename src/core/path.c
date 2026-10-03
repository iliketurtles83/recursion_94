#include "path.h"
#include "noise.h"
#include "rng.h"
#include <stdlib.h>
#include <string.h>

#define DEG2RADD (3.14159265358979323846 / 180.0)

enum { PATH_FEATURE_YAW = 1, PATH_FEATURE_PITCH, PATH_FEATURE_TWIST, PATH_FEATURE_RADIUS };

static double clampd(double x, double lo, double hi) { return x < lo ? lo : (x > hi ? hi : x); }

static DVec3 dir_from(double yaw, double pitch)
{
    return dv3(sin(yaw) * cos(pitch), sin(pitch), -cos(yaw) * cos(pitch));
}

static DVec3 ctrl_at(const PathSystem *ps, int i) { return ps->ctrl[i % PATH_CTRL_CAP]; }

// Point i+1 from point i.
static void gen_ctrl(PathSystem *ps)
{
    int i = ps->ctrlCount;
    if (i == 0) {
        // Point 0 sits one spacing behind the origin so segment 1 starts at s = 0.
        ps->ctrl[0] = dv3(0.0, 0.0, PATH_CTRL_SPACING);
        ps->ctrlCount = 1;
        return;
    }
    if (i == 1) {
        ps->ctrl[1] = dv3(0.0, 0.0, 0.0);
        ps->ctrlCount = 2;
        return;
    }
    const PathStyle *st = &ps->style;
    double maxYaw = st->maxYawStepDeg * DEG2RADD;
    double maxPitch = st->maxPitchDeg * DEG2RADD;
    double n = (double)(i - 2);
    double ny = ct_noise1(ct_hash64_2(ps->seed, PATH_FEATURE_YAW), n * 0.15);
    double np = ct_noise1(ct_hash64_2(ps->seed, PATH_FEATURE_PITCH), n * 0.15);
    ps->yaw += clampd(ny * maxYaw, -maxYaw, maxYaw);
    ps->pitch += np * maxPitch * 0.35 - st->pitchSpring * ps->pitch;
    ps->pitch = clampd(ps->pitch, -maxPitch, maxPitch);
    DVec3 prev = ctrl_at(ps, i - 1);
    ps->ctrl[i % PATH_CTRL_CAP] = dv_madd(prev, dir_from(ps->yaw, ps->pitch), PATH_CTRL_SPACING);
    ps->ctrlCount++;
}

// Centripetal Catmull-Rom (alpha = 0.5), Barry-Goldman evaluation.
static DVec3 cr_eval(const DVec3 P[4], const double kt[4], double t)
{
    DVec3 A1 = dv_add(dv_scale(P[0], (kt[1] - t) / (kt[1] - kt[0])), dv_scale(P[1], (t - kt[0]) / (kt[1] - kt[0])));
    DVec3 A2 = dv_add(dv_scale(P[1], (kt[2] - t) / (kt[2] - kt[1])), dv_scale(P[2], (t - kt[1]) / (kt[2] - kt[1])));
    DVec3 A3 = dv_add(dv_scale(P[2], (kt[3] - t) / (kt[3] - kt[2])), dv_scale(P[3], (t - kt[2]) / (kt[3] - kt[2])));
    DVec3 B1 = dv_add(dv_scale(A1, (kt[2] - t) / (kt[2] - kt[0])), dv_scale(A2, (t - kt[0]) / (kt[2] - kt[0])));
    DVec3 B2 = dv_add(dv_scale(A2, (kt[3] - t) / (kt[3] - kt[1])), dv_scale(A3, (t - kt[1]) / (kt[3] - kt[1])));
    return dv_add(dv_scale(B1, (kt[2] - t) / (kt[2] - kt[1])), dv_scale(B2, (t - kt[1]) / (kt[2] - kt[1])));
}

static void seg_points(const PathSystem *ps, int j, DVec3 P[4])
{
    for (int q = 0; q < 4; q++) P[q] = ctrl_at(ps, j - 1 + q);
}

static void seg_setup(PathSystem *ps, int j)
{
    while (ps->ctrlCount < j + 3) gen_ctrl(ps);
    DVec3 P[4];
    seg_points(ps, j, P);
    double *kt = ps->segKnots;
    kt[0] = 0.0;
    for (int q = 1; q < 4; q++) kt[q] = kt[q - 1] + sqrt(dv_len(dv_sub(P[q], P[q - 1])));
    ps->segCum[0] = 0.0;
    ps->segT[0] = kt[1];
    DVec3 prev = P[1];
    for (int q = 1; q <= PATH_SUBSAMPLES; q++) {
        double t = kt[1] + (kt[2] - kt[1]) * (double)q / PATH_SUBSAMPLES;
        DVec3 p = cr_eval(P, kt, t);
        ps->segT[q] = t;
        ps->segCum[q] = ps->segCum[q - 1] + dv_len(dv_sub(p, prev));
        prev = p;
    }
    ps->seg = j;
    ps->segLen = ps->segCum[PATH_SUBSAMPLES];
    ps->segCursor = 0;
}

static void emit_sample(PathSystem *ps)
{
    double s = (double)ps->kNext * PATH_DS;
    while (s >= ps->segS0 + ps->segLen) {
        ps->segS0 += ps->segLen;
        seg_setup(ps, ps->seg + 1);
    }
    double local = s - ps->segS0;
    while (ps->segCursor < PATH_SUBSAMPLES - 1 && ps->segCum[ps->segCursor + 1] < local) ps->segCursor++;
    int c = ps->segCursor;
    double span = ps->segCum[c + 1] - ps->segCum[c];
    double f = span > 0.0 ? (local - ps->segCum[c]) / span : 0.0;
    double t = ps->segT[c] + (ps->segT[c + 1] - ps->segT[c]) * f;

    DVec3 P[4];
    seg_points(ps, ps->seg, P);
    const double *kt = ps->segKnots;
    double h = (kt[2] - kt[1]) * 1e-4;
    DVec3 pos = cr_eval(P, kt, t);
    DVec3 T = dv_norm(dv_sub(cr_eval(P, kt, t + h), cr_eval(P, kt, t - h)));

    PathRaw r;
    r.pos = pos;
    r.T = T;
    const PathStyle *st = &ps->style;
    double twistRate = clampd(st->twistRateMax * 1.6 * ct_noise1(ct_hash64_2(ps->seed, PATH_FEATURE_TWIST), s / 90.0),
                              -st->twistRateMax, st->twistRateMax);
    r.radius = st->baseRadius * (1.0 + st->radiusNoiseAmp * ct_noise1(ct_hash64_2(ps->seed, PATH_FEATURE_RADIUS), s / st->radiusNoiseScale));

    if (ps->kNext == 0) {
        DVec3 up = dv3(0, 1, 0);
        r.Nrmf = dv_norm(dv_sub(up, dv_scale(T, dv_dot(up, T))));
        r.roll = 0.0;
        r.curvature = 0.0;
    } else {
        const PathRaw *p0 = &ps->ring[(ps->kNext - 1) % PATH_CAP];
        // Double reflection (Wang et al. 2008).
        DVec3 v1 = dv_sub(pos, p0->pos);
        double c1 = dv_dot(v1, v1);
        DVec3 rL = dv_sub(p0->Nrmf, dv_scale(v1, 2.0 / c1 * dv_dot(v1, p0->Nrmf)));
        DVec3 tL = dv_sub(p0->T, dv_scale(v1, 2.0 / c1 * dv_dot(v1, p0->T)));
        DVec3 v2 = dv_sub(T, tL);
        double c2 = dv_dot(v2, v2);
        DVec3 rN = c2 > 1e-24 ? dv_sub(rL, dv_scale(v2, 2.0 / c2 * dv_dot(v2, rL))) : rL;
        r.Nrmf = dv_norm(dv_sub(rN, dv_scale(T, dv_dot(rN, T))));
        r.roll = p0->roll + twistRate * PATH_DS;
        double d = clampd(dv_dot(p0->T, T), -1.0, 1.0);
        r.curvature = acos(d) / PATH_DS;
        if (r.curvature > ps->maxCurvatureSeen) ps->maxCurvatureSeen = r.curvature;
    }
    ps->ring[ps->kNext % PATH_CAP] = r;
    ps->kNext++;
    if (ps->kNext - ps->kMin > PATH_CAP) ps->kMin = ps->kNext - PATH_CAP;
}

void path_reset(PathSystem *ps, uint64_t seed, PathStyle style)
{
    PathRaw *ring = ps->ring;
    memset(ps, 0, sizeof(*ps));
    ps->ring = ring;
    ps->seed = seed;
    ps->style = style;
    seg_setup(ps, 1);
    ps->segS0 = 0.0;
}

void path_init(PathSystem *ps, uint64_t seed, PathStyle style)
{
    ps->ring = (PathRaw *)calloc(PATH_CAP, sizeof(PathRaw));
    path_reset(ps, seed, style);
}

void path_free(PathSystem *ps)
{
    free(ps->ring);
    ps->ring = NULL;
}

void path_ensure(PathSystem *ps, double sMin, double sMax)
{
    if (sMin < 0.0) sMin = 0.0;
    if (ps->kNext > 0 && sMin < (double)ps->kMin * PATH_DS) path_reset(ps, ps->seed, ps->style);
    int64_t need = (int64_t)ceil(sMax / PATH_DS) + 2;
    while (ps->kNext < need) emit_sample(ps);
}

double path_s_min(const PathSystem *ps) { return (double)ps->kMin * PATH_DS; }
double path_s_max(const PathSystem *ps) { return (double)(ps->kNext - 1) * PATH_DS; }

PathSample path_sample(const PathSystem *ps, double s)
{
    PathSample out;
    memset(&out, 0, sizeof(out));
    if (ps->kNext < 2) return out;
    double fk = s / PATH_DS;
    int64_t k = (int64_t)floor(fk);
    double f = fk - (double)k;
    if (k < ps->kMin) { k = ps->kMin; f = 0.0; }
    if (k > ps->kNext - 2) { k = ps->kNext - 2; f = 1.0; }
    const PathRaw *a = &ps->ring[k % PATH_CAP];
    const PathRaw *b = &ps->ring[(k + 1) % PATH_CAP];
    out.pos = dv_lerp(a->pos, b->pos, f);
    DVec3 T = dv_norm(dv_lerp(a->T, b->T, f));
    DVec3 N = dv_lerp(a->Nrmf, b->Nrmf, f);
    N = dv_norm(dv_sub(N, dv_scale(T, dv_dot(N, T))));
    out.roll = a->roll + (b->roll - a->roll) * f;
    out.radius = a->radius + (b->radius - a->radius) * f;
    out.curvature = a->curvature + (b->curvature - a->curvature) * f;
    DVec3 B = dv_cross(T, N);
    double c = cos(out.roll), sn = sin(out.roll);
    out.T = T;
    out.N = dv_add(dv_scale(N, c), dv_scale(B, sn));
    out.B = dv_cross(T, out.N);
    return out;
}

double path_curvature_limit(const PathStyle *style)
{
    // Heading can change by at most maxYawStep (+ a pitch step) per control spacing;
    // the spline spreads that over the segment but may concentrate it ~2.5x.
    double turn = style->maxYawStepDeg * DEG2RADD + style->maxPitchDeg * DEG2RADD * 0.5;
    return 2.5 * turn / PATH_CTRL_SPACING;
}
