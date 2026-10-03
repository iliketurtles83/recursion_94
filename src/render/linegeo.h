// Neon line geometry builder. Appends into growable SoA arrays laid out exactly
// like raylib's Mesh, split into batches of <= 65535 vertices (Mesh.indices is
// 16-bit). Allocation happens only when a buffer grows; clearing keeps capacity.
//
// Vertex attribute packing
// ------------------------
// Planar strip (rings, ribbons; geometry lies in a plane given by the builder):
//   position  : vertex position (already offset by +-across*halfWidth)
//   texcoord  : (u = arc length along the strip, v = -1..1 across)
//   texcoord2 : (group index, animation param)   e.g. ring slot, angular velocity
//   normal    : across direction (unit, same space as position)
//   tangent   : (halfWidth, hdrIntensity, hash 0..1, pulse frequency)
//   color     : tint (RGBA8)
//
// Camera-facing segment (edges, holo rings, trails; expanded in the vertex shader):
//   position  : this endpoint
//   normal    : the other endpoint
//   texcoord  : (u = 0 at A / 1 at B, v = side -1|+1)
//   texcoord2 : (group index or -1, animation param)
//   tangent   : (halfWidth, hdrIntensity, hash 0..1, endFlag 0 = A, 1 = B)
//   color     : tint
//
// Fill triangles (faces) use position, texcoord (face uv), texcoord2 (group, face id),
// tangent (0, intensity, hash, 0), normal (face normal), color.
#ifndef CT_LINEGEO_H
#define CT_LINEGEO_H

#include <stdint.h>
#include "raylib.h"

#define LG_MAX_BATCH_VERTS 65535
#define LG_MAX_BATCHES     16

typedef struct LineGeo {
    float         *pos, *uv, *uv2, *nrm, *tan;
    unsigned char *col;
    unsigned short *idx;
    int vcount, vcap;
    int icount, icap;
    // batches: [batchV[i], batchV[i+1]) vertices, [batchI[i], batchI[i+1]) indices
    int batchV[LG_MAX_BATCHES + 1], batchI[LG_MAX_BATCHES + 1];
    int batchCount;
} LineGeo;

typedef struct LineAttr {
    Color color;
    float intensity;
    float hash;
    float param;     // planar: pulse frequency
    float group;     // texcoord2.x
    float anim;      // texcoord2.y
} LineAttr;

void lg_free(LineGeo *g);
void lg_clear(LineGeo *g);
// Ensures nverts/nidx fit in the current batch (starting a new one if needed).
// Returns the batch-local index of the next vertex.
int  lg_reserve(LineGeo *g, int nverts, int nidx);
void lg_vertex(LineGeo *g, Vector3 p, Vector2 uv, Vector2 uv2, Vector3 n, Vector4 t, Color c);
void lg_index(LineGeo *g, int i);
void lg_quad(LineGeo *g, int base, int a, int b, int c, int d);

// Strip through n points with per-point across directions (unit).
void lg_planar_polyline(LineGeo *g, const Vector3 *pts, const Vector3 *across, int n, float halfWidth, LineAttr a);
// Arc in the XY plane (z = z0) around the origin, radius r, angles a0..a1 (radians).
void lg_planar_arc(LineGeo *g, float r, float a0, float a1, float z0, float halfWidth, float maxStepRad, LineAttr a);
// Straight radial strip from r0 to r1 at angle ang in the XY plane.
void lg_planar_radial(LineGeo *g, float r0, float r1, float ang, float z0, float halfWidth, LineAttr a);
// Camera-facing segment with per-end width/intensity.
void lg_facing_segment(LineGeo *g, Vector3 a, Vector3 b, float wa, float wb, float ia, float ib, LineAttr at);
// Fill quad (p0..p3 in order) with face uv 0..1.
void lg_fill_quad(LineGeo *g, Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, Vector3 n, LineAttr a);
void lg_fill_tri(LineGeo *g, Vector3 p0, Vector3 p1, Vector3 p2, Vector3 n, LineAttr a);

int  lg_batch_count(const LineGeo *g);
// Fills a Mesh whose data pointers alias the builder arrays for batch b (do not free them).
Mesh lg_batch_mesh(const LineGeo *g, int b);
uint64_t lg_hash(const LineGeo *g);
// Uploads batch b to the GPU. The returned Mesh keeps no CPU data; its `indices`
// points at a sentinel so DrawMesh still issues an indexed draw. Release with lg_unload_mesh.
Mesh lg_upload_mesh(const LineGeo *g, int b, bool dynamic);
void lg_unload_mesh(Mesh m);

#endif
