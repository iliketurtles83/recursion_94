// Server Corridor pillars: placement on a world-aligned lattice and stepped bodies.
// Pure CPU and deterministic (no GL); used by env_corridor.c and the tests.
//
// Placement uses lattice_place (each cell owned by exactly one chunk). The corridor's yaw
// spring keeps the heading within a few degrees of a world axis, so each cell has a lane
// axis (the world axis closest to the path tangent at its owner point): "along" runs with
// the lane, "across" points away from it.
//
// Wall rows: cells near the lane. The pillar's lane face sits just outside lane + clearance
// (pushed out until the exact path-to-footprint distance allows it), so the walls follow the
// lane closely with a natural stagger. Pairs of cells along the lane (even, odd along index)
// may merge into one wider slab: the even cell builds it and the odd cell stays empty; the odd
// cell evaluates its partner's decision itself, so ownership stays order-independent.
// Back rows: further out, lower density, placed freely inside the cell, darker, no outline.
//
// Profile: the body is a stack of tiers whose lane face steps in or out by 1-2 units at
// 1-3 heights (never closer than lane + clearance): the castellated silhouette the white
// outlines follow.
#ifndef CT_CORRIDOR_PILLARS_H
#define CT_CORRIDOR_PILLARS_H

#include <stdint.h>
#include "core/path.h"
#include "render/linegeo.h"

#define COR_MAX_TIERS   4
#define COR_MAX_PILLARS 256

typedef struct PillarParams {
    float cell;              // floor grid cell (world units)
    int   latticeCells;      // lattice cell = latticeCells * cell
    float laneHalfWidth;     // path radius
    float clearance;         // gap between the lane edge and any pillar
    float wallBand;          // wall rows: lane face within inner + wallBand
    float bandMax;           // pillars only within this distance of the path
    float wallDensity, backDensity;
    float backRamp;          // back-row density ramps in over this distance behind the wall band
    float mergeChance;       // wall pair merges into one slab
    float widthMin, widthMax;    // along the lane (single cell)
    float depthMin, depthMax;    // across
    float heightMin, heightMax;
    float backHeightMul;     // back rows: height multiplier at bandMax
    int   stepsMin, stepsMax;    // profile steps on wall pillars
    float stepMin, stepMax;      // step size (snapped to 0.5)
    float slackMax;          // extra lane gap of wall pillars, 0..slackMax
    float outlineChance;     // wall pillars with a stepped outline
    float floorY;            // world y of the floor
} PillarParams;

// Pacing archetype multipliers for pillars (taken at each cell's owner point, as in the city).
typedef struct PillarArch {
    float wallDensity, backDensity;
    float clearanceAdd;      // >= 0
    float height;
    float outline;
    float merge;
} PillarArch;

// Archetype parameters at arc length s (blended), for any s: a cell's merge partner may be
// owned by a neighbouring chunk, so the lookup must not depend on the chunk being built.
typedef struct PillarArchFn {
    PillarArch (*at)(const void *user, double s);
    const void *user;
} PillarArchFn;

typedef struct PillarDesc {
    int32_t  ix, iz;         // lattice cell
    double   sOwner;
    float    dist;           // cell centre -> path
    int      axis;           // lane along world x (0) or z (1)
    int      side;           // +1: pillar on the + across side of the path
    int      front;          // +1: the camera-facing end is a1, -1: a0
    int      wall;           // wall row (else back row)
    int      outline;        // carries a stepped outline (Phase C)
    int      tiers;
    double   a0, a1;         // along extent (world coordinate on the lane axis)
    double   cOut;           // across coordinate of the outer face
    double   cLane[COR_MAX_TIERS];   // across coordinate of the lane face per tier
    float    yTop[COR_MAX_TIERS];    // world y of each tier's top; tier 0 starts at baseY
    float    baseY;
    uint32_t hash;
} PillarDesc;

typedef struct PillarClaim {
    int32_t ix, iz;
    double  sOwner;
    int     pillar;          // index into the pillar array, -1 = empty
} PillarClaim;

PillarArch pillar_arch_neutral(void);
// Path arc length a chunk build samples beyond [s0, s1].
double pillars_reach(const PillarParams *p);
// Pillars owned by chunk `chunkIndex`. arch (NULL = neutral) and claims may be NULL.
int pillars_plan(const PillarParams *p, const PillarArchFn *arch, uint64_t seed, const PathSystem *path, int chunkIndex,
                 PillarDesc *out, int maxPillars, PillarClaim *claims, int maxClaims, int *claimCount);
// World xz rectangle covering all tiers of a pillar.
void pillar_bounds(const PillarDesc *d, double *x0, double *z0, double *x1, double *z1);
// Opaque bodies (lane face, camera-facing face, overhang undersides) relative to origin.
// Vertex layout as tower.vs: texcoord face (u, v) world units, texcoord2 (hash, height),
// tangent (face id 0 lane / 1 front / 4 underside, tier, wall, -), colour tint.
void pillars_build(const PillarDesc *d, int n, DVec3 origin, LineGeo *body);
typedef struct PillarDecorParams {
    float rainOffset;        // code rain panels: distance off the face
    float rainIntensity;     // per-panel HDR (the shader varies it per column / dash)
    float backRain;          // back-row multiplier
    float laneRain;          // lane-face multiplier (seen at grazing angles, reads denser)
    float outlineWidth;      // half width
    float outlineIntensity;
    float outlineOffset;     // distance off the pillar edge
    float outlineTopMin;     // the top run covers this..1 of the pillar depth
    float kerbMin, kerbMax;  // floor kerb length along the lane
    float kerbWidth;         // half width
    float floorY;
} PillarDecorParams;

// Code rain panels: one quad per tier on the lane face and on the camera-facing face.
// Layout as glyph.vs: texcoord (u across the face from its start, v above the floor),
// texcoord2 (pillar hash, face 0 lane / 1 front), normal, tangent (width, tier top, intensity, wall).
// Outlines (planar strips in the camera-facing plane, trace.vs packing) follow the lane-side
// silhouette from the floor up the steps and partly across the top; kerbs (camera-facing
// segments, object.vs packing) run from the outline's foot along the lane on the floor.
void pillars_build_decor(const PillarDecorParams *p, const PillarDesc *d, int n, DVec3 origin,
                         LineGeo *rain, LineGeo *outlines, LineGeo *kerbs);
// World position of (along, across, y) for a pillar's lane axis.
DVec3 pillar_point(const PillarDesc *d, double along, double across, double y);

#endif
