// Minimal environment skeleton. Copy this file to start a new environment.
//
// What an environment implements:
//   pathStyle      how the shared path behaves here (radius, curvature, walls)
//   init           load shaders, build static GPU resources
//   registerParams register tunables (group names show up in the debug UI)
//   buildChunk     PURE CPU: fill ChunkCpu geometry from (seed, params, path, index)
//   uploadChunk    GL upload (chunk_upload_default handles LineGeo batches)
//   releaseChunk   GL release
//   draw*          draw visible chunks; the core has already bound the HDR target
//                  and the camera matrices (render space = world - renderOrigin)
//   unload         free everything
//
// This template draws a single marker circle at the start of each chunk so you can
// see motion and streaming. Replace it with real content.
#include "env_common.h"
#include "core/rng.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { TB_LINES = 0 };   // chunk batches used by this environment

typedef struct TemplateState {
    uint64_t   seed;
    ShaderLib *sl;
    int        shLines;
    Material   mat;
    PathStyle  path;
    float      markerIntensity;
} TemplateState;

static PathStyle tpl_path_style(const Environment *env)
{
    const TemplateState *st = (const TemplateState *)env->state;
    return st->path;
}

static void tpl_register(Environment *env, ParamRegistry *reg)
{
    TemplateState *st = (TemplateState *)env->state;
    const char *g = "Template";
    params_float(reg, g, "tplRadius", &st->path.baseRadius, 16.0f, 3.0f, 60.0f);
    params_float(reg, g, "tplYawStepDeg", &st->path.maxYawStepDeg, 6.0f, 0.0f, 40.0f);
    params_float(reg, g, "tplMarkerIntensity", &st->markerIntensity, 2.0f, 0.0f, 10.0f);
    st->path.radiusNoiseAmp = 0.0f;
    st->path.radiusNoiseScale = 100.0f;
    st->path.maxPitchDeg = 5.0f;
    st->path.pitchSpring = 0.2f;
    st->path.twistRateMax = 0.0f;
    st->path.hasWalls = false;
}

static void tpl_init(Environment *env, uint64_t seed, ShaderLib *sl)
{
    TemplateState *st = (TemplateState *)env->state;
    st->seed = seed;
    st->sl = sl;
    if (sl) {
        st->shLines = sl_load(sl, "object.vs", "line_facing.fs");
        st->mat = LoadMaterialDefault();
    }
}

static void tpl_reseed(Environment *env, uint64_t seed) { ((TemplateState *)env->state)->seed = seed; }

static void tpl_build_chunk(Environment *env, const PathSystem *path, int chunkIndex, ChunkCpu *out)
{
    TemplateState *st = (TemplateState *)env->state;
    PathSample ps = path_sample(path, out->s0);
    float R = (float)ps.radius;
    LineAttr at = { (Color){ 255, 154, 60, 255 }, st->markerIntensity, 0.0f, 0.0f, -1.0f, 0.0f };
    const int segs = 64;
    Vector3 prev = { 0 };
    for (int i = 0; i <= segs; i++) {
        double a = 6.283185307 * i / segs;
        DVec3 w = dv_add(ps.pos, dv_add(dv_scale(ps.B, cos(a) * R), dv_scale(ps.N, sin(a) * R)));
        Vector3 l = env_local(w, out->origin);
        if (i > 0) lg_facing_segment(&out->geo[TB_LINES], prev, l, 0.06f, 0.06f, at.intensity, at.intensity, at);
        prev = l;
    }
}

static void tpl_upload(Environment *env, ChunkCpu *in, ChunkGpu *out) { chunk_upload_default(in, out); }
static void tpl_release(Environment *env, ChunkGpu *gpu) { chunk_release_default(gpu); }
static float tpl_energy(const Environment *env, const PathSystem *path, double s) { return 0.3f; }
static void tpl_update(Environment *env, const FrameContext *ctx) { }
static void tpl_draw_background(Environment *env, const FrameContext *ctx) { }
static void tpl_draw_opaque(Environment *env, const FrameContext *ctx, const ChunkGpu *const *chunks, int n) { }

static void tpl_draw_emissive(Environment *env, const FrameContext *ctx, const ChunkGpu *const *chunks, int n)
{
    TemplateState *st = (TemplateState *)env->state;
    env_set_common_uniforms(st->sl, st->shLines, ctx);
    sl_f1(st->sl, st->shLines, "uEdgeMix", 0.0f);
    sl_f1(st->sl, st->shLines, "uFlicker", 0.0f);
    st->mat.shader = sl_shader(st->sl, st->shLines);
    for (int i = 0; i < n; i++) {
        const ChunkGpu *c = chunks[i];
        sl_f1(st->sl, st->shLines, "uChunkFade", env_chunk_fade(c, ctx->time));
        Matrix xf = env_chunk_transform(c, ctx->renderOrigin);
        for (int m = 0; m < c->meshCount; m++) {
            DrawMesh(c->meshes[m], st->mat, xf);
            ctx->stats->drawCalls++;
            ctx->stats->vertices += c->meshes[m].vertexCount;
        }
    }
}

static const char *tpl_label(const Environment *env, int archetype) { return "TEMPLATE"; }

static void tpl_unload(Environment *env)
{
    TemplateState *st = (TemplateState *)env->state;
    if (st->sl) { st->mat.shader.id = rlGetShaderIdDefault(); UnloadMaterial(st->mat); }
    free(st);
    env->state = NULL;
}

Environment env_template_create(void)
{
    Environment e;
    memset(&e, 0, sizeof(e));
    e.name = "Template";
    e.id = ENV_TEMPLATE;
    e.state = calloc(1, sizeof(TemplateState));
    e.pathStyle = tpl_path_style;
    e.init = tpl_init;
    e.registerParams = tpl_register;
    e.reseed = tpl_reseed;
    e.buildChunk = tpl_build_chunk;
    e.uploadChunk = tpl_upload;
    e.releaseChunk = tpl_release;
    e.energyAt = tpl_energy;
    e.update = tpl_update;
    e.drawBackground = tpl_draw_background;
    e.drawOpaque = tpl_draw_opaque;
    e.drawEmissive = tpl_draw_emissive;
    e.chunkLabel = tpl_label;
    e.unload = tpl_unload;
    return e;
}
