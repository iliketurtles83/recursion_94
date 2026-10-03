// Server Corridor overhead beams: a broken lattice of wide flat beams above the pillars.
// Pure CPU and deterministic (no GL); used by env_corridor.c and the tests.
//
// Beams follow lines of a coarse world lattice (cell L): a row line (constant z) runs along x,
// a column line (constant x) along z. Each line is selected by its own hash (beamChance) and
// carries its own width, thickness and height, so all cells of a line agree. Cells are owned
// through lattice_place; a cell builds the segments of the selected lines through its centre,
// broken by hashed gaps. Beams running along the lane are kept out from above the lane itself
// (they would roof the corridor); beams crossing it are what frame the sky. A segment gets an
// end cap only where the neighbouring cell on its line has no segment, which every chunk
// evaluates the same way, so beams continue across chunk boundaries without seams.
// Row lines sit on even height levels and column lines on odd ones, so crossing beams never
// share a face plane.
#ifndef CT_CORRIDOR_BEAMS_H
#define CT_CORRIDOR_BEAMS_H

#include <stdint.h>
#include "core/path.h"
#include "render/linegeo.h"

#define COR_MAX_BEAMS 128

typedef struct BeamParams {
    float cell;              // lattice cell (world units)
    float bandMax;           // beam cells only within this distance of the path
    float chance;            // fraction of lattice lines carrying a beam
    float gapChance;         // a cell of a beam line left empty
    float widthMin, widthMax;
    float thickMin, thickMax;
    float heightMin;         // above the floor; levels every 1 unit up to heightMin + 2*levels
    int   levels;
    float laneKeep;          // along-lane beams need their centreline this far from the path
    float floorY;
    // Archetype multiplier on chance at arc length s (NULL = 1), evaluated at each cell's owner
    // point; must not depend on the chunk being built (neighbour cells decide the end caps).
    float (*densityAt)(const void *user, double s);
    const void *densityUser;
} BeamParams;

typedef struct BeamDesc {
    int32_t ix, iz;          // lattice cell
    int     dir;             // 0: runs along x (row line), 1: along z (column line)
    double  a0, a1;          // extent along the beam (world)
    double  c;               // centreline (world coordinate across the beam)
    float   width, thick, yBottom;   // yBottom: world y of the underside
    int     cap0, cap1;      // end caps at a0 / a1
    double  sOwner;
    uint32_t hash;           // line hash
} BeamDesc;

double beams_reach(const BeamParams *p);
// Beam segments owned by chunk `chunkIndex`. ownedCells (optional) receives the claimed cells.
int beams_plan(const BeamParams *p, uint64_t seed, const PathSystem *path, int chunkIndex, BeamDesc *out, int maxBeams,
               int32_t (*ownedCells)[2], int maxCells, int *cellCount);
// Opaque beam bodies (underside, two sides, end caps) relative to origin. Vertex layout as
// tower.vs: texcoord (u along the beam = world coordinate mod 4096, v across / up from the
// bottom edge), texcoord2 (line hash, width), tangent (face id 0 underside / 1 side / 2 cap, -, -, -).
void beams_build(const BeamDesc *b, int n, DVec3 origin, LineGeo *body);

#endif
