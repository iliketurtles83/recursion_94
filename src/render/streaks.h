// GPU-only speed streaks shared by environments. One static mesh of quads; every
// streak is derived from its id in streak.vs and placed around the path using 16
// path frames sampled along the streak volume, so streaks follow the path's curves.
// The owning environment keeps a StreakParams in its own params struct and registers
// the fields under its own names.
#ifndef CT_STREAKS_H
#define CT_STREAKS_H

#include <stdbool.h>
#include "raylib.h"
#include "linegeo.h"

struct ShaderLib;
struct FrameContext;

typedef struct StreakParams {
    float density, lenMul, wMin, wMax, intensity;
    float cometFrac, accentFrac, helixFrac, helixRate;
    float zRange, zBehind, speedMul;
    float cylinderInner, centerClear;  // radial fraction; clear screen center (0 disables)
    Color colMain, colAccent;    // sRGB
    // Volume: 0 = cylinder of the path radius (tunnels), 1 = slab above a floor (open spaces).
    int   volume;
    float slabHalfWidth, slabYMin, slabYMax;   // path frame (B right, N up), relative to the centerline
    float fallTilt;              // slab: streaks start higher the further ahead (falling rain)
    float slabGap, slabGapFrac;  // slab: half width of a mostly empty centre band, fraction of streaks in it (1 = no gap)
} StreakParams;

typedef struct StreakSystem {
    struct ShaderLib *sl;
    int      sh;
    int      count;
    Material mat;
    Mesh     mesh;
    LineGeo  geo;
    bool     uploaded;
} StreakSystem;

// sl may be NULL (headless tests): nothing is loaded and draw is a no-op.
void streaks_init(StreakSystem *s, struct ShaderLib *sl, int count);
void streaks_draw(StreakSystem *s, const StreakParams *p, const struct FrameContext *ctx);
void streaks_unload(StreakSystem *s);

#endif
