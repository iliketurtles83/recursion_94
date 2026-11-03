// The original RECURSION_94 probe (shipModel=2), ported from the old game's
// DrawPlayerShip as static meshes in ship space (x right, y up, z forward).
// Built at the old game's arena scale (1 arena unit = 0.4 world units), so it is
// drawn unscaled, about the dart's size.
#ifndef CT_SHIPPROBE_H
#define CT_SHIPPROBE_H
#include "linegeo.h"

// Half width of the engine glows (the data trail starts at this width).
#define SHIPPROBE_GLOW_HALF 0.075f

// Hull faces for the flat ship shader (ship_dart.fs).
void shipprobe_hull(LineGeo *g);
// Neon edges, lights and the two engine glows (engineIntensity baked in).
void shipprobe_lights(LineGeo *g, const Vector3 engines[2], float engineIntensity);

#endif
