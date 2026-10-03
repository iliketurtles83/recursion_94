// Small render-math helpers shared by the core and environments.
#ifndef CT_RMATH_H
#define CT_RMATH_H

#include "raylib.h"
#include "raymath.h"

// Projects a render-space point with a combined view*projection matrix (raylib order:
// MatrixMultiply(view, proj)). Returns uv in 0..1 with y up; *w <= 0 means behind the camera.
static inline Vector2 ct_project_uv(Matrix vp, Vector3 p, float *w)
{
    float x = vp.m0 * p.x + vp.m4 * p.y + vp.m8 * p.z + vp.m12;
    float y = vp.m1 * p.x + vp.m5 * p.y + vp.m9 * p.z + vp.m13;
    float ww = vp.m3 * p.x + vp.m7 * p.y + vp.m11 * p.z + vp.m15;
    if (w) *w = ww;
    if (ww <= 1e-6f) ww = 1e-6f;
    Vector2 r = { (x / ww) * 0.5f + 0.5f, (y / ww) * 0.5f + 0.5f };
    return r;
}

static inline Vector3 ct_srgb_to_linear(Color c)
{
    Vector3 r = { powf(c.r / 255.0f, 2.2f), powf(c.g / 255.0f, 2.2f), powf(c.b / 255.0f, 2.2f) };
    return r;
}

#endif
