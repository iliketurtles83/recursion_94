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

// Convex solid as vertex, edge and triangle tables (unit size, centred at the origin).
#define SHAPE_SOLID_MAX_V 16
#define SHAPE_SOLID_MAX_E 32
#define SHAPE_SOLID_MAX_F 24
typedef struct ShapeSolid {
    int     nv, ne, nf;
    Vector3 v[SHAPE_SOLID_MAX_V];
    int     e[SHAPE_SOLID_MAX_E][2];
    int     f[SHAPE_SOLID_MAX_F][3];
} ShapeSolid;

// Solid placed at c with basis axes ax, ay, az (rotation and per-axis scale): vertex v lands at
// c + v.x ax + v.y ay + v.z az. Edges into `edges` (camera-facing, width), triangles into `faces`
// (may be NULL) with outward normals and the face index in anim.
void shape_solid(LineGeo *edges, LineGeo *faces, const ShapeSolid *s, Vector3 c, Vector3 ax, Vector3 ay, Vector3 az,
                 LineAttr e, LineAttr f, float width);

#endif
