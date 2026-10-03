// Neon Grid City tower decoration: circuit traces and code-text ("glyph") panels.
// Pure CPU and deterministic: everything derives from the TowerDesc hash.
//
// Faces are classified against the path at the tower's owner point: faces turned toward
// the approaching camera ("front") get dense trace buses; faces turned toward the lane get a
// glyph panel (narrow strip by the front edge, or a block) and sparser traces around it.
// Faces turned away get nothing (never seen in the chase view).
//
// Traces are routed on a square lattice (pitch) per face with an occupancy bitmap: a bus of
// 2-4 parallel traces climbs in vertical runs and jogs diagonally (45 degrees) as one, keeping
// an empty column to any other bus; traces drop out individually and end in a via.
#ifndef CT_CITY_DECOR_H
#define CT_CITY_DECOR_H

#include "city_towers.h"

typedef struct CityDecorParams {
    float tracePitch;        // lattice spacing (world units)
    float traceWidth;        // half width
    float traceIntensity;
    float traceBuses;        // buses per 10 columns of face width
    float traceFar;          // bus density multiplier at bandMax (LOD)
    float traceStop;         // chance the bus ends after a run
    float tracePulse;        // pulse frequency along the trace (0 = off)
    float viaRadius;
    float glyphStripChance, glyphBlockChance, glyphIntensity, glyphScroll;
    float faceOffset;        // distance off the face (depth precision at the fog distance)
    float inner, bandMax;    // lane + clearance, tower band (for LOD)
} CityDecorParams;

// Glyph quads use: position, texcoord (u, v in world units inside the panel),
// texcoord2 (panel hash, kind 0 = strip / 1 = block), normal (face normal),
// tangent (width, height, intensity, scroll speed), color (unused).
void city_decor_build(const CityDecorParams *p, const TowerDesc *towers, int n, const PathSystem *path,
                      DVec3 origin, LineGeo *traces, LineGeo *glyphs);

#endif
