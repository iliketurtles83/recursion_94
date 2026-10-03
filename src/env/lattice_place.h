// World-lattice cell ownership for open environments (city towers, corridor pillars and beams).
// Pure CPU and deterministic (no GL).
//
// Cells of a world xz lattice that lie within bandMax of the path are each owned by exactly
// one chunk: the chunk containing s*, the arc length of the path point nearest to the cell
// centre. Every chunk evaluates that argmin over the same global set of path samples (every
// LATTICE_SAMPLE_STEP units), so any chunk that looks at a cell agrees on its owner whatever
// the build order. That needs the global argmin to lie within the window a chunk searches
// (lattice_reach); it does as long as the path never bends back on itself within ~2·bandMax,
// which the yaw spring guarantees (heading stays within ~±25° of an axis).
#ifndef CT_LATTICE_PLACE_H
#define CT_LATTICE_PLACE_H

#include <stdint.h>
#include "core/path.h"

#define LATTICE_SAMPLE_STEP 2.0      // global path sample spacing used for nearest-point queries
#define LATTICE_MAX_SAMPLES 2048

// Path polyline around one chunk (xz only).
typedef struct LatticeWindow {
    double  x[LATTICE_MAX_SAMPLES], z[LATTICE_MAX_SAMPLES];
    int64_t m0;              // global index of sample 0 (s = m * LATTICE_SAMPLE_STEP)
    int     n;
    int     p0, p1;          // the chunk's own piece [s0, s1] as sample indices
    int     chunkIndex;
} LatticeWindow;

typedef struct LatticeCell {
    int32_t ix, iz;
    double  cx, cz;          // cell centre (world)
    double  sOwner;          // nearest path point to the centre
    float   dist;            // distance centre -> path
} LatticeCell;

typedef void (*LatticeCellFn)(void *user, const LatticeWindow *w, const LatticeCell *cell);

// Path arc length a chunk build samples beyond [s0, s1] for cells up to bandMax away.
double lattice_reach(double bandMax);
// Samples the path over chunk chunkIndex extended by `reach` on both sides.
void   lattice_window_load(LatticeWindow *w, const PathSystem *path, int chunkIndex, double reach);
// Calls fn for every cell (side L) within bandMax of the path that the window's chunk owns,
// row by row (iz outer, ix inner). The window must have been loaded with reach >= lattice_reach(bandMax).
void   lattice_for_each(const LatticeWindow *w, double L, double bandMax, LatticeCellFn fn, void *user);
// Exact distance from the whole window polyline to an axis-aligned rectangle (0 if it crosses it).
double lattice_rect_dist(const LatticeWindow *w, double x0, double z0, double x1, double z1);
// Nearest point of the whole window polyline to (x, z); returns the distance, *sOut its arc length.
double lattice_nearest(const LatticeWindow *w, double x, double z, double *sOut);

#endif
