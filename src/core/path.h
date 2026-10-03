// Procedural tunnel centerline: control points -> centripetal Catmull-Rom ->
// uniform arc-length samples with rotation-minimizing frames, roll and radius.
// Everything here is double precision. Generation is strictly sequential from
// s = 0, so a sample depends only on (seed, style, s).
#ifndef CT_PATH_H
#define CT_PATH_H

#include <stdint.h>
#include <stdbool.h>
#include "dvec.h"

typedef struct PathStyle {
    float baseRadius, radiusNoiseAmp, radiusNoiseScale;
    float maxYawStepDeg, maxPitchDeg, pitchSpring;
    float twistRateMax;      // roll rad/unit
    bool  hasWalls;          // false for open environments (future grid city)
} PathStyle;

typedef struct PathSample {
    DVec3  pos;
    DVec3  T, N, B;          // tangent, up-ish normal, right (B = T x N); N/B include roll
    double radius;
    double roll;
    double curvature;        // 1/units
} PathSample;

#define PATH_DS           0.5     // uniform sample spacing (units)
#define PATH_CAP          8192    // samples kept in the ring (4096 units)
#define PATH_CTRL_CAP     64
#define PATH_SUBSAMPLES   96      // arc-length subsamples per spline segment
#define PATH_CTRL_SPACING 40.0

typedef struct PathRaw {
    DVec3  pos, T, Nrmf;     // Nrmf: rotation-minimizing normal before roll
    double roll, radius, curvature;
} PathRaw;

typedef struct PathSystem {
    uint64_t  seed;
    PathStyle style;

    // control point generator
    int    ctrlCount;        // number of control points generated
    double yaw, pitch;
    DVec3  ctrl[PATH_CTRL_CAP];

    // spline walker
    int    seg;              // current segment j (between ctrl j and j+1), j >= 1
    double segS0, segLen;
    double segKnots[4];
    double segT[PATH_SUBSAMPLES + 1];
    double segCum[PATH_SUBSAMPLES + 1];
    int    segCursor;

    // uniform samples (ring buffer indexed by global sample index)
    PathRaw *ring;
    int64_t  kMin, kNext;    // valid sample indices: [kMin, kNext)
    double   maxCurvatureSeen;
} PathSystem;

void       path_init(PathSystem *ps, uint64_t seed, PathStyle style);
void       path_free(PathSystem *ps);
void       path_reset(PathSystem *ps, uint64_t seed, PathStyle style);
// Make sure samples cover [sMin, sMax]. Regenerates from 0 if sMin fell behind the ring.
void       path_ensure(PathSystem *ps, double sMin, double sMax);
PathSample path_sample(const PathSystem *ps, double s);
double     path_s_min(const PathSystem *ps);
double     path_s_max(const PathSystem *ps);
// Theoretical curvature bound implied by the style (used by tests/debug).
double     path_curvature_limit(const PathStyle *style);

#endif
