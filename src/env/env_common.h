// Helpers shared by environment implementations.
#ifndef CT_ENV_COMMON_H
#define CT_ENV_COMMON_H

#include "env.h"
#include "render/shaderlib.h"
#include "raymath.h"
#include "rlgl.h"

// Shared emissive uniforms (see shaders/common.glsl).
static inline void env_set_common_uniforms(ShaderLib *sl, int sh, const FrameContext *ctx)
{
    const Atmosphere *a = &ctx->atmo;
    sl_f1(sl, sh, "uTime", ctx->shaderTime);
    sl_v3(sl, sh, "uCamPos", ctx->camera.position);
    sl_f4(sl, sh, "uFog", a->fogStart, a->fogEnd, a->fogToCore, 0.0f);
    sl_f4(sl, sh, "uFade", a->farFadeStart, a->farFadeEnd, a->nearFadeStart, a->nearFadeEnd);
    sl_v3(sl, sh, "uCoreColor", a->coreColor);
    sl_f1(sl, sh, "uChunkFade", 1.0f);
    sl_f1(sl, sh, "uPixelScale", ctx->pixelScale);
    sl_f1(sl, sh, "uMinPx", a->minPixelWidth);
    sl_f1(sl, sh, "uIntensity", ctx->intensity);
    // Only scenery opts into extra distance simplification. Ship lights and
    // other environments may share shader entries with the tunnel.
    sl_f3(sl, sh, "uSceneryFade", 0, 0, 1);
}

static inline float env_chunk_fade(const ChunkGpu *c, double time)
{
    float f = (float)((time - c->fadeStart) / 0.3);
    return f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
}

static inline Matrix env_chunk_transform(const ChunkGpu *c, DVec3 renderOrigin)
{
    DVec3 d = dv_sub(c->origin, renderOrigin);
    return MatrixTranslate((float)d.x, (float)d.y, (float)d.z);
}

static inline Vector3 env_local(DVec3 p, DVec3 origin) { return dv_to_v3(dv_sub(p, origin)); }

#endif
