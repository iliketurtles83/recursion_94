// Circuit-trace router for flat faces (city tower sides, corridor beams). Pure CPU.
//
// Traces are routed on a square lattice (pitch) per face with an occupancy bitmap: a bus of
// 2-4 parallel traces climbs along v in straight runs and jogs diagonally (45 degrees) as one,
// keeping an empty column to any other bus; traces drop out individually and end in a via.
// Callers may block cells first (e.g. under a text panel) with trace_grid_block.
#ifndef CT_TRACE_ROUTE_H
#define CT_TRACE_ROUTE_H

#include <stdint.h>
#include "core/dvec.h"
#include "core/rng.h"
#include "render/linegeo.h"

#define TRACE_MAX_ROWS 512
#define TRACE_MAX_COLS 64

// A rectangle in world space: o + u*[0, w] + v*[0, h]; u, v, n unit and orthogonal, n outward.
typedef struct TraceFace {
    DVec3  o, u, v, n;
    double w, h;
} TraceFace;

typedef struct TraceGrid {
    uint64_t occ[TRACE_MAX_ROWS];
    int cols, rows;
    double u0;               // u of column 0 centre
} TraceGrid;

typedef struct TraceRouteParams {
    float pitch;             // lattice spacing (world units)
    float width;             // trace half width
    float intensity;
    float stop;              // chance a bus ends after a run
    float pulse;             // pulse frequency along the trace (0 = off)
    float viaRadius;
    float faceOffset;        // distance off the face
    float baseStart;         // chance a bus starts at row 0 (else in the lower half)
    float altChance;         // chance a bus uses colorAlt
    Color color, colorAlt, colorVia;
} TraceRouteParams;

// Sets up the lattice for a face. Returns 0 (nothing to route) if the face is too small.
int  trace_grid_init(TraceGrid *g, const TraceFace *f, float pitch);
// Marks columns [c0, c1] x rows [r0, r1] plus a one-cell margin as taken.
void trace_grid_block(TraceGrid *g, int c0, int c1, int r0, int r1);
// Routes floor(density * cols / 10 + rand) buses into `out` (chunk-local, relative to origin).
void trace_route_buses(const TraceRouteParams *p, const TraceFace *f, TraceGrid *g, float density, Pcg32 *r,
                       DVec3 origin, LineGeo *out);

#endif
