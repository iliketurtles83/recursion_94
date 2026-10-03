// Neon Grid City towers: placement on a world-aligned lattice and body/edge geometry.
// Pure CPU and deterministic (no GL); used by env_city.c and the tests.
//
// Placement. Towers sit on a world xz lattice whose cell is latticeCells floor-grid cells,
// so tower edges land on grid lines. Every lattice cell belongs to exactly one chunk: the
// chunk containing s*, the arc length of the path point nearest to the cell centre. Each
// chunk evaluates that argmin over the same global set of path samples (every SAMPLE_STEP
// units), so any chunk that looks at a cell agrees on its owner whatever the build order.
// That needs the global argmin to lie within the window a chunk searches
// (city_towers_reach); it does as long as the path never bends back on itself within
// ~2·bandMax, which the yaw spring guarantees (heading stays within ~±25° of an axis).
#ifndef CT_CITY_TOWERS_H
#define CT_CITY_TOWERS_H

#include <stdint.h>
#include "core/path.h"
#include "render/linegeo.h"

#define CITY_MAX_TIERS  3
#define CITY_MAX_TOWERS 160

typedef struct CityTowerParams {
    float cell;             // floor grid cell (world units)
    int   latticeCells;     // lattice cell = latticeCells * cell
    float laneHalfWidth;    // path radius
    float clearance;        // gap between the lane edge and any tower
    float bandMax;          // towers only within this distance of the path
    float densityNear, densityFar;
    float heightMin, heightMax, heightFar;   // heightFar: extra height at bandMax
    float landmarkChance, landmarkMul;
    int   footMin, footMax; // footprint side, in grid cells
    float setbackChance;
    float floorY;           // world y of the floor
} CityTowerParams;

// Pacing archetype: multipliers/offsets on the base parameters. Blended per cell by the
// cell's owner point s* across the first chunk of a new archetype, so a cell's parameters
// depend only on (seed, params, s*) and placement stays order-independent.
typedef struct CityArch {
    float densityNear, densityFar;   // x base densities
    float clearanceAdd;              // >= 0, added to the lane clearance (never closer than base)
    float height, heightFar;         // x base height range, x heightFar
    float landmark;                  // x landmarkChance
    float traces, glyphs;            // x decor bus density, x glyph panel chances
    float props;                     // x propsMax
    float energy;                    // pacing energy 0..1
} CityArch;

typedef struct CityArchBlend {
    CityArch prev, cur;
    double   s0, len;                // blend prev -> cur over [s0, s0 + len]
} CityArchBlend;

CityArch city_arch_neutral(void);
CityArch city_arch_at(const CityArchBlend *b, double s);

typedef struct TowerDesc {
    int32_t  ix, iz;        // lattice cell
    double   sOwner;        // nearest path point to the cell centre
    float    dist;          // distance cell centre -> path
    int      tiers;
    double   x0[CITY_MAX_TIERS], z0[CITY_MAX_TIERS], x1[CITY_MAX_TIERS], z1[CITY_MAX_TIERS];  // world rect per tier
    float    yTop[CITY_MAX_TIERS];   // world y of each tier's top; tier 0 starts at baseY
    float    baseY;
    uint32_t hash;
    float    traceMul, glyphMul;   // from the archetype at sOwner
} TowerDesc;

// Every cell a chunk claimed, occupied or not (debug / tests).
typedef struct CellClaim {
    int32_t ix, iz;
    double  sOwner;
    float   dist;
    int     tower;          // index into the tower array, -1 = empty
} CellClaim;

// Path arc length a chunk build samples beyond [s0, s1].
double city_towers_reach(const CityTowerParams *p);
// Towers owned by chunk `chunkIndex` (covering [s0, s1)). blend and claims may be NULL.
int city_towers_plan(const CityTowerParams *p, const CityArchBlend *blend, uint64_t seed, const PathSystem *path,
                     int chunkIndex, TowerDesc *out, int maxTowers, CellClaim *claims, int maxClaims, int *claimCount);
// Opaque bodies into `body` and outline edges into `edges`, relative to `origin`.
void city_towers_build(const TowerDesc *t, int n, DVec3 origin, LineGeo *body, LineGeo *edges,
                       float edgeIntensity, float edgeWidth);

#endif
