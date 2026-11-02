// Double-precision 3D vectors for world space (path, origins).
#ifndef CT_DVEC_H
#define CT_DVEC_H

#include <math.h>
#include "raylib.h"

typedef struct DVec3 { double x, y, z; } DVec3;

static inline DVec3 dv3(double x, double y, double z) { DVec3 r = { x, y, z }; return r; }
static inline DVec3 dv_add(DVec3 a, DVec3 b) { return dv3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline DVec3 dv_sub(DVec3 a, DVec3 b) { return dv3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline DVec3 dv_scale(DVec3 a, double s) { return dv3(a.x * s, a.y * s, a.z * s); }
static inline double dv_dot(DVec3 a, DVec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline DVec3 dv_cross(DVec3 a, DVec3 b) { return dv3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static inline double dv_len(DVec3 a) { return sqrt(dv_dot(a, a)); }
static inline DVec3 dv_norm(DVec3 a) { double l = dv_len(a); return l > 1e-300 ? dv_scale(a, 1.0 / l) : dv3(0, 0, 0); }
static inline DVec3 dv_lerp(DVec3 a, DVec3 b, double t) { return dv3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t); }
static inline DVec3 dv_madd(DVec3 a, DVec3 b, double s) { return dv3(a.x + b.x * s, a.y + b.y * s, a.z + b.z * s); }
static inline Vector3 dv_to_v3(DVec3 a) { Vector3 r = { (float)a.x, (float)a.y, (float)a.z }; return r; }
static inline DVec3 v3_to_dv(Vector3 a) { return dv3(a.x, a.y, a.z); }
// Rotate v around unit axis k by angle a (Rodrigues).
static inline DVec3 dv_rotate(DVec3 v, DVec3 k, double a)
{
    double c = cos(a), s = sin(a);
    return dv_add(dv_add(dv_scale(v, c), dv_scale(dv_cross(k, v), s)), dv_scale(k, dv_dot(k, v) * (1.0 - c)));
}

#endif
