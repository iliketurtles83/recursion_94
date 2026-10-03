#include "gridfloor.h"
#include "env/env_common.h"
#include "render/rmath.h"
#include "pipeline.h"
#include <math.h>

void gridfloor_init(GridFloor *g, ShaderLib *sl)
{
    g->sl = sl;
    if (sl) g->sh = sl_load(sl, "fullscreen.vs", "grid.fs");
}

Vector2 gridfloor_offset(DVec3 renderOrigin, float cell)
{
    double period = GRIDFLOOR_PERIOD_CELLS * (double)cell;
    double x = fmod(renderOrigin.x, period), z = fmod(renderOrigin.z, period);
    if (x < 0.0) x += period;
    if (z < 0.0) z += period;
    return (Vector2){ (float)x, (float)z };
}

void gridfloor_draw(GridFloor *g, const GridFloorParams *p, const FrameContext *ctx, double floorY)
{
    if (!g->sl) return;
    ShaderLib *sl = g->sl;
    int sh = g->sh;
    Camera3D cam = ctx->camera;
    Vector3 fwd = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
    Vector3 right = Vector3Normalize(Vector3CrossProduct(fwd, cam.up));
    Vector3 up = Vector3CrossProduct(right, fwd);
    Vector2 off = gridfloor_offset(ctx->renderOrigin, p->cell);

    rlDrawRenderBatchActive();
    BeginShaderMode(sl_shader(sl, sh));
    env_set_common_uniforms(sl, sh, ctx);
    sl_f1(sl, sh, "uAspect", (float)ctx->width / (float)ctx->height);
    sl_v3(sl, sh, "uCamRight", right);
    sl_v3(sl, sh, "uCamUp", up);
    sl_v3(sl, sh, "uCamFwd", fwd);
    sl_f1(sl, sh, "uTanHalf", tanf(ctx->fovy * 0.5f * DEG2RAD));
    sl_f2(sl, sh, "uClip", ctx->zNear, ctx->zFar);
    sl_f1(sl, sh, "uFloorY", (float)(floorY - ctx->renderOrigin.y));
    sl_f2(sl, sh, "uGridOffset", off.x, off.y);
    sl_f1(sl, sh, "uCell", p->cell);
    sl_f1(sl, sh, "uFilter", p->filter ? 1.0f : 0.0f);
    sl_f1(sl, sh, "uNodeSize", p->nodeSize / p->cell);
    float refl = ctx->hasReflection ? p->reflectStrength : 0.0f;
    sl_f2(sl, sh, "uRefl", refl, p->reflectR0);
    if (ctx->hasReflection) sl_tex(sl, sh, "uReflection", ctx->reflection);
    sl_f2(sl, sh, "uLine", p->lineWidth / p->cell, p->haloWidth / p->cell);
    sl_f4(sl, sh, "uLineI", p->lineIntensity, p->haloIntensity, p->nodeBoost, p->whiteCore);
    sl_f4(sl, sh, "uFloorFog", p->fogStart, p->fogEnd, p->horizonGlow, p->sheen);
    sl_v3(sl, sh, "uLineColor", ct_srgb_to_linear(p->lineColor));
    sl_v3(sl, sh, "uFloorColor", ct_srgb_to_linear(p->floorColor));
    sl_v3(sl, sh, "uHorizonColor", ct_srgb_to_linear(p->horizonColor));
    pipe_fullscreen_quad();
    EndShaderMode();
    ctx->stats->drawCalls++;
}
