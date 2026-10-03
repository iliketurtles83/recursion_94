// Infinite neon grid floor shared by open environments (Env #2 city, Env #3 canyon).
// Drawn as one fullscreen pass in the opaque phase: each pixel intersects its view ray
// with the plane y = floorY, shades a filtered (alias-free) grid and writes gl_FragDepth,
// so later geometry depth-tests against it. Pixels whose ray misses the plane (or goes
// beyond the far clip) are discarded and show the background.
//
// Precision: the grid is world-axis aligned. The shader never sees world coordinates,
// only render-space positions plus gridfloor_offset(), i.e. the render origin reduced
// modulo a whole number of cells (computed in double here).
#ifndef CT_GRIDFLOOR_H
#define CT_GRIDFLOOR_H

#include "raylib.h"
#include "core/dvec.h"

struct ShaderLib;
struct FrameContext;

typedef struct GridFloorParams {
    float cell;              // world units between lines
    float lineWidth;         // core half width (world units)
    float haloWidth;         // gaussian halo width (world units)
    float lineIntensity;     // HDR of the line core
    float haloIntensity;     // HDR of the halo relative to the core colour
    float nodeBoost;         // brightness of the square pads where lines cross
    float nodeSize;          // pad half size (world units)
    float whiteCore;         // 0..1 how white-hot the core gets
    float fogStart, fogEnd;  // distance fade toward horizonColor
    float horizonGlow;       // HDR multiplier of horizonColor
    float sheen;             // grazing-angle sheen toward the horizon colour (fake gloss)
    bool  filter;            // debug: false = naive point-sampled lines (shows the aliasing)
    float reflectStrength;   // planar reflection (ctx->reflection) weight
    float reflectR0;         // Fresnel reflectance looking straight down (1 at grazing)
    Color lineColor, floorColor, horizonColor;   // sRGB
} GridFloorParams;

typedef struct GridFloor {
    struct ShaderLib *sl;
    int sh, shCeiling;
} GridFloor;

#define GRIDFLOOR_PERIOD_CELLS 1024.0   // offset wraps after this many cells

void    gridfloor_init(GridFloor *g, struct ShaderLib *sl);
// Ceiling mode: a sparse lattice on the plane y = planeY above the camera (skygrid.fs). Uses
// cell, lineWidth, lineIntensity, lineColor and fogStart/fogEnd (as a distance fade); `sparse`
// is the fraction of cell edges drawn. Call from drawEmissive (additive; depth-tested).
void    gridfloor_draw_ceiling(GridFloor *g, const GridFloorParams *p, const struct FrameContext *ctx, double planeY, float sparse);
// floorY: world-space height of the plane.
void    gridfloor_draw(GridFloor *g, const GridFloorParams *p, const struct FrameContext *ctx, double floorY);
// Render origin xz reduced modulo GRIDFLOOR_PERIOD_CELLS * cell (world grid phase of render-space 0).
Vector2 gridfloor_offset(DVec3 renderOrigin, float cell);

#endif
