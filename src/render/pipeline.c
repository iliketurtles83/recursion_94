#include "pipeline.h"
#include "rlgl.h"
#include "raymath.h"
#include <stdio.h>
#include <string.h>

void post_register_params(PostSettings *s, ParamRegistry *reg)
{
    const char *g = "Bloom/Post";
    params_bool(reg, g, "bloomOn", &s->bloomOn, true);
    params_float(reg, g, "bloomThreshold", &s->bloomThreshold, 1.0f, 0.0f, 8.0f);
    params_float(reg, g, "bloomKnee", &s->bloomKnee, 0.6f, 0.0f, 2.0f);
    params_float(reg, g, "bloomIntensity", &s->bloomIntensity, 0.8f, 0.0f, 4.0f);
    params_float(reg, g, "bloomRadius", &s->bloomRadius, 0.85f, 0.0f, 1.5f);
    params_int(reg, g, "bloomMips", &s->bloomMips, 6, 1, PIPE_MAX_MIPS);
    params_bool(reg, g, "bloomQuarterRes", &s->bloomQuarterRes, false);
    params_bool(reg, g, "zoomBlurOn", &s->zoomBlurOn, true);
    params_float(reg, g, "zoomBlur", &s->zoomBlur, 0.06f, 0.0f, 0.3f);
    params_float(reg, g, "zoomBlurInner", &s->zoomBlurInner, 0.08f, 0.0f, 1.0f);
    params_float(reg, g, "zoomBlurOuter", &s->zoomBlurOuter, 0.75f, 0.0f, 2.0f);
    params_int(reg, g, "zoomTaps", &s->zoomTaps, 10, 2, 16);
    params_bool(reg, g, "caOn", &s->caOn, true);
    params_float(reg, g, "caPixels", &s->caPixels, 2.0f, 0.0f, 8.0f);
    params_bool(reg, g, "tonemapOn", &s->tonemapOn, true);
    params_float(reg, g, "exposure", &s->exposure, 1.0f, 0.05f, 8.0f);
    params_float(reg, g, "saturation", &s->saturation, 1.25f, 0.0f, 2.0f);
    params_bool(reg, g, "vignetteOn", &s->vignetteOn, true);
    params_float(reg, g, "vignette", &s->vignette, 0.55f, 0.0f, 1.5f);
    params_bool(reg, g, "grainOn", &s->grainOn, true);
    params_float(reg, g, "grain", &s->grain, 0.025f, 0.0f, 0.2f);
}

static bool make_target(HdrTarget *t, int w, int h, bool hdr16, bool withDepth)
{
    memset(t, 0, sizeof(*t));
    t->w = w; t->h = h;
    t->fbo = rlLoadFramebuffer();
    if (!t->fbo) return false;
    int fmt = hdr16 ? PIXELFORMAT_UNCOMPRESSED_R16G16B16A16 : PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    t->color.id = rlLoadTexture(NULL, w, h, fmt, 1);
    t->color.width = w; t->color.height = h; t->color.mipmaps = 1; t->color.format = fmt;
    rlTextureParameters(t->color.id, RL_TEXTURE_MIN_FILTER, RL_TEXTURE_FILTER_LINEAR);
    rlTextureParameters(t->color.id, RL_TEXTURE_MAG_FILTER, RL_TEXTURE_FILTER_LINEAR);
    rlTextureParameters(t->color.id, RL_TEXTURE_WRAP_S, RL_TEXTURE_WRAP_CLAMP);
    rlTextureParameters(t->color.id, RL_TEXTURE_WRAP_T, RL_TEXTURE_WRAP_CLAMP);
    rlFramebufferAttach(t->fbo, t->color.id, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
    if (withDepth) {
        t->depth = rlLoadTextureDepth(w, h, true);
        rlFramebufferAttach(t->fbo, t->depth, RL_ATTACHMENT_DEPTH, RL_ATTACHMENT_RENDERBUFFER, 0);
    }
    bool ok = rlFramebufferComplete(t->fbo);
    if (!ok) fprintf(stderr, "pipeline: framebuffer %dx%d incomplete\n", w, h);
    return ok;
}

static void free_target(HdrTarget *t)
{
    if (t->color.id) rlUnloadTexture(t->color.id);
    if (t->fbo) rlUnloadFramebuffer(t->fbo);   // also deletes attached depth renderbuffer
    memset(t, 0, sizeof(*t));
}

static RenderTexture2D as_rt(const HdrTarget *t)
{
    RenderTexture2D rt;
    memset(&rt, 0, sizeof(rt));
    rt.id = t->fbo;
    rt.texture = t->color;
    rt.depth.id = t->depth;
    rt.depth.width = t->w; rt.depth.height = t->h;
    return rt;
}

RenderTexture2D pipe_scene_rt(const Pipeline *p) { return as_rt(&p->scene); }

static void alloc_targets(Pipeline *p, bool quarter)
{
    bool ok = make_target(&p->scene, p->w, p->h, p->hdr16, true);
    if (!ok && p->hdr16) {
        fprintf(stderr, "pipeline: RGBA16F unsupported, falling back to RGBA8\n");
        free_target(&p->scene);
        p->hdr16 = false;
        make_target(&p->scene, p->w, p->h, false, true);
    }
    int bw = p->w / (quarter ? 4 : 2), bh = p->h / (quarter ? 4 : 2);
    p->mipCount = 0;
    for (int i = 0; i < PIPE_MAX_MIPS && bw >= 2 && bh >= 2; i++) {
        make_target(&p->down[i], bw, bh, p->hdr16, false);
        make_target(&p->up[i], bw, bh, p->hdr16, false);
        p->mipCount++;
        bw /= 2; bh /= 2;
    }
    p->quarterAllocated = quarter;
}

static void free_targets(Pipeline *p)
{
    free_target(&p->scene);
    for (int i = 0; i < PIPE_MAX_MIPS; i++) { free_target(&p->down[i]); free_target(&p->up[i]); }
    p->mipCount = 0;
}

bool pipe_init(Pipeline *p, ShaderLib *sl, int w, int h, bool hdr16, bool quarterBloom)
{
    memset(p, 0, sizeof(*p));
    p->sl = sl;
    p->w = w; p->h = h;
    p->hdr16 = hdr16;
    p->shPrefilter = sl_load(sl, "fullscreen.vs", "bloom_prefilter.fs");
    p->shDown = sl_load(sl, "fullscreen.vs", "bloom_down.fs");
    p->shUp = sl_load(sl, "fullscreen.vs", "bloom_up.fs");
    p->shComposite = sl_load(sl, "fullscreen.vs", "composite.fs");
    alloc_targets(p, quarterBloom);
    return p->scene.fbo != 0;
}

void pipe_resize(Pipeline *p, int w, int h, bool quarterBloom)
{
    if (w == p->w && h == p->h && quarterBloom == p->quarterAllocated) return;
    if (w < 8 || h < 8) return;
    free_targets(p);
    p->w = w; p->h = h;
    alloc_targets(p, quarterBloom);
}

void pipe_unload(Pipeline *p) { free_targets(p); }

void pipe_fullscreen_quad(void)
{
    rlBegin(RL_QUADS);
    rlColor4ub(255, 255, 255, 255);
    rlTexCoord2f(0.0f, 0.0f); rlVertex2f(-1.0f, -1.0f);
    rlTexCoord2f(1.0f, 0.0f); rlVertex2f(1.0f, -1.0f);
    rlTexCoord2f(1.0f, 1.0f); rlVertex2f(1.0f, 1.0f);
    rlTexCoord2f(0.0f, 1.0f); rlVertex2f(-1.0f, 1.0f);
    rlEnd();
}

void pipe_begin_scene(Pipeline *p, Matrix view, Matrix proj, Color clear)
{
    double t0 = GetTime();
    BeginTextureMode(as_rt(&p->scene));
    ClearBackground(clear);
    rlDrawRenderBatchActive();
    rlMatrixMode(RL_PROJECTION);
    rlPushMatrix();
    rlLoadIdentity();
    rlMultMatrixf(MatrixToFloat(proj));
    rlMatrixMode(RL_MODELVIEW);
    rlLoadIdentity();
    rlMultMatrixf(MatrixToFloat(view));
    rlEnableDepthTest();
    rlEnableDepthMask();
    rlDisableBackfaceCulling();
    p->ms.scene = t0;   // finished in pipe_end_scene
}

void pipe_set_emissive(bool on)
{
    rlDrawRenderBatchActive();
    if (on) { rlSetBlendMode(BLEND_ADDITIVE); rlDisableDepthMask(); }
    else { rlSetBlendMode(BLEND_ALPHA); rlEnableDepthMask(); }
}

void pipe_end_scene(Pipeline *p)
{
    rlDrawRenderBatchActive();
    rlSetBlendMode(BLEND_ALPHA);
    rlEnableDepthMask();
    rlDisableDepthTest();
    rlEnableBackfaceCulling();
    rlMatrixMode(RL_PROJECTION);
    rlPopMatrix();
    rlMatrixMode(RL_MODELVIEW);
    rlLoadIdentity();
    EndTextureMode();
    p->ms.scene = (GetTime() - p->ms.scene) * 1000.0;
}

static void pass(Pipeline *p, int sh, HdrTarget *dst, Texture2D src)
{
    BeginTextureMode(as_rt(dst));
    BeginShaderMode(sl_shader(p->sl, sh));
    rlSetTexture(src.id);
    pipe_fullscreen_quad();
    rlSetTexture(0);
    EndShaderMode();
    EndTextureMode();
    p->drawCalls++;
}

void pipe_post(Pipeline *p, const PostSettings *s, RenderTexture2D *dst, int outW, int outH,
               Vector2 vanishingUV, float zoomStrength, float whiteout, float time)
{
    ShaderLib *sl = p->sl;
    double t0 = GetTime();
    p->drawCalls = 0;
    rlDrawRenderBatchActive();
    rlDisableColorBlend();
    rlDisableDepthTest();
    rlDisableBackfaceCulling();

    if (s->bloomQuarterRes != p->quarterAllocated) pipe_resize(p, p->w, p->h, s->bloomQuarterRes);
    int mips = s->bloomMips < p->mipCount ? s->bloomMips : p->mipCount;
    Texture2D bloomTex = p->down[0].color;
    if (s->bloomOn && mips > 0) {
        sl_f2(sl, p->shPrefilter, "uTexel", 1.0f / p->scene.w, 1.0f / p->scene.h);
        sl_f2(sl, p->shPrefilter, "uThreshold", s->bloomThreshold, s->bloomKnee);
        pass(p, p->shPrefilter, &p->down[0], p->scene.color);
        for (int i = 1; i < mips; i++) {
            sl_f2(sl, p->shDown, "uTexel", 1.0f / p->down[i - 1].w, 1.0f / p->down[i - 1].h);
            pass(p, p->shDown, &p->down[i], p->down[i - 1].color);
        }
        Texture2D src = p->down[mips - 1].color;
        for (int i = mips - 2; i >= 0; i--) {
            BeginTextureMode(as_rt(&p->up[i]));
            BeginShaderMode(sl_shader(sl, p->shUp));
            sl_f2(sl, p->shUp, "uTexel", 1.0f / src.width, 1.0f / src.height);
            sl_f1(sl, p->shUp, "uRadius", s->bloomRadius);
            sl_tex(sl, p->shUp, "uBase", p->down[i].color);
            rlSetTexture(src.id);
            pipe_fullscreen_quad();
            rlSetTexture(0);
            EndShaderMode();
            EndTextureMode();
            p->drawCalls++;
            src = p->up[i].color;
        }
        bloomTex = src;
    }
    double t1 = GetTime();
    p->ms.bloom = (t1 - t0) * 1000.0;

    if (dst) BeginTextureMode(*dst);
    else rlViewport(0, 0, outW, outH);
    int c = p->shComposite;
    BeginShaderMode(sl_shader(sl, c));
    sl_tex(sl, c, "uBloom", bloomTex);
    sl_f1(sl, c, "uBloomIntensity", (s->bloomOn && mips > 0) ? s->bloomIntensity : 0.0f);
    sl_f2(sl, c, "uResolution", (float)outW, (float)outH);
    sl_f2(sl, c, "uVanish", vanishingUV.x, vanishingUV.y);
    sl_f1(sl, c, "uZoom", s->zoomBlurOn ? zoomStrength : 0.0f);
    sl_f2(sl, c, "uZoomMask", s->zoomBlurInner, s->zoomBlurOuter);
    sl_i1(sl, c, "uZoomTaps", s->zoomTaps);
    sl_f1(sl, c, "uCA", s->caOn ? s->caPixels : 0.0f);
    sl_f1(sl, c, "uExposure", s->exposure);
    sl_i1(sl, c, "uTonemap", s->tonemapOn ? 1 : 0);
    sl_f1(sl, c, "uSaturation", s->saturation);
    sl_f1(sl, c, "uVignette", s->vignetteOn ? s->vignette : 0.0f);
    sl_f1(sl, c, "uGrain", s->grainOn ? s->grain : 0.0f);
    sl_f1(sl, c, "uWhiteout", whiteout);
    sl_f1(sl, c, "uTime", time);
    rlSetTexture(p->scene.color.id);
    pipe_fullscreen_quad();
    rlSetTexture(0);
    EndShaderMode();
    if (dst) EndTextureMode();
    p->drawCalls++;
    rlEnableColorBlend();
    rlEnableBackfaceCulling();
    p->ms.composite = (GetTime() - t1) * 1000.0;
}
