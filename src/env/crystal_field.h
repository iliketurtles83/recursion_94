// Crystal field: polyhedra placement around the lane and their geometry.
// Pure CPU and deterministic (no GL); used by env_crystal.c and the tests.
//
// Each chunk owns the clusters and single shapes whose anchor arc length lies in its [s0, s1),
// so ownership needs no lattice. A cluster sits at an angle around the path (biased toward
// left and right) and a distance out to bandMax, and scatters its shapes around its centre.
//
// Keep-out: the flight model's steering box reaches steerLimit x laneRadius on each axis, so
// its corner is sqrt(2) x that from the path. Every shape's bounding sphere stays at least that
// plus `clearance` away from the path at every arc length (checked against the path around the
// shape, not only at its anchor, so curves cannot swing the lane into a shape). Shapes that
// fail are pushed straight away from the nearest path point; ones that still fail are dropped.
#ifndef CT_CRYSTAL_FIELD_H
#define CT_CRYSTAL_FIELD_H

#include <stdint.h>
#include "raylib.h"
#include "core/path.h"
#include "render/linegeo.h"
#include "render/shapes.h"

enum { CF_TETRA, CF_OCTA, CF_ICOSA, CF_CUBE, CF_PRISM, CF_BIPYRAMID, CF_KIND_COUNT };

#define CF_MAX_SHAPES 160
// Shapes per chunk that tumble: two vec4 chunk uniforms each (centre + phase, axis + speed).
// Shapes beyond these stand still.
#define CF_TUMBLE_SLOTS 48

typedef struct CrystalFieldParams {
    float laneRadius;        // path radius (steering box half size = steerLimit x laneRadius)
    float steerLimit;
    float clearance;         // gap beyond the steering box corner
    float bandMax;           // shapes within this distance of the path
    float clusters;          // mean clusters per chunk
    float scatter;           // mean single shapes per chunk
    int   clusterMin, clusterMax;          // shapes per cluster
    float clusterRadMin, clusterRadMax;
    float sideBias;          // fraction of clusters placed left or right (rest anywhere around)
    float nearBias;          // radial distribution exponent (> 1 pulls clusters toward the lane)
    float giantFrac, mediumFrac;            // size classes; the rest are small
    float smallMin, smallMax, mediumMin, mediumMax, giantMin, giantMax;   // circumradius
    float edgeIntensity;
    float edgeWidth;         // edge half width per unit of shape size
    float edgeWidthMin;
    float faceIntensity;     // face brightness relative to the edges
    float tumble;            // largest angular speed (rad/s); giants turn slower
} CrystalFieldParams;

typedef struct CrystalShape {
    DVec3   pos;             // world centre
    Vector3 ax, ay, az;      // basis: rotation x per-axis scale
    float   bound;           // bounding sphere radius
    float   size;            // circumradius before stretch
    int     kind;
    Color   color;
    float   intensity;
    float   hash;
    Vector3 spinAxis;
    float   spinSpeed, spinPhase;
} CrystalShape;

const ShapeSolid *crystal_solid(int kind);
// Distance from the path centreline to the box corner plus clearance.
double crystal_keepout(const CrystalFieldParams *p);
// Lower bound of the distance from p to the path, searching arc lengths within reach of sCenter.
double crystal_path_distance(const PathSystem *path, DVec3 p, double sCenter, double reach, DVec3 *nearest);
// How far beyond a chunk the placement samples the path (Environment.pathReach).
double crystal_path_reach(const CrystalFieldParams *p);
// Shapes owned by the chunk [s0, s1). base: per-seed, per-chunk hash. Returns the count.
int  crystal_field_plan(const CrystalFieldParams *p, const PathSystem *path, uint64_t base, double s0, double s1,
                        CrystalShape *out, int max);
// Edge (and, if faces is not NULL, face) geometry of the shapes, relative to origin. The first
// CF_TUMBLE_SLOTS shapes are built around their own centre with their slot as group, and their
// centre/phase and axis/speed go to uniforms[slot] and uniforms[CF_TUMBLE_SLOTS + slot].
void crystal_field_build(const CrystalFieldParams *p, const CrystalShape *s, int n, DVec3 origin,
                         LineGeo *edges, LineGeo *faces, float (*uniforms)[4]);

#endif
