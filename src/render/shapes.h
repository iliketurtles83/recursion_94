// Wireframe solids for debris/props, centred at the origin: camera-facing edge
// segments into `edges`, data-pattern fill faces into `faces` (face index in anim).
#ifndef CT_SHAPES_H
#define CT_SHAPES_H

#include "linegeo.h"

// Cube with half extent h.
void shape_cube(LineGeo *edges, LineGeo *faces, float h, LineAttr e, LineAttr f, float width);
// Octahedron with vertex distance h.
void shape_octa(LineGeo *edges, LineGeo *faces, float h, LineAttr e, LineAttr f, float width);
// Wire sphere of radius r: three orthogonal great circles (edges only).
void shape_sphere(LineGeo *edges, float r, LineAttr e, float width);

#endif
