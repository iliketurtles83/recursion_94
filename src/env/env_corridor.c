// Environment #3: "Server Corridor" — low flight over the shared neon grid floor between
// walls of server pillars with green code rain and white stepped outlines, under a broken
// lattice of circuit-covered beams. See PLAN_ENV3_SERVER_CORRIDOR.md.
//
// Phases done: A (path style: level, no twist, floor and ceiling, strong yaw spring; grid
// floor, sky, reflection).
#include "env_common.h"
#include "core/rng.h"
#include "core/pacing.h"
#include "render/gridfloor.h"
#include "render/rmath.h"
#include "render/pipeline.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct CorridorParams {
    PathStyle path;
    GridFloorParams floor;
    float fogStart, fogEnd;          // fog for chunk content
    float hazeWidth, hazeGlow, dust; // sky
    bool  showFloor, showSky;
} CorridorParams;

typedef struct CorridorState {
    uint64_t       seed;
    CorridorParams p;
    ShaderLib     *sl;
    int            shSky;
    Material       mat;
    GridFloor      floor;
} CorridorState;

static const Color COL_CYAN    = { 0x4F, 0xEA, 0xF5, 255 };
static const Color COL_FLOOR   = { 0x02, 0x08, 0x10, 255 };
static const Color COL_HORIZON = { 0x06, 0x16, 0x24, 255 };
static const Color COL_ZENITH  = { 0x01, 0x02, 0x05, 255 };

static PathStyle cor_path_style(const Environment *env)
{
    const CorridorState *st = (const CorridorState *)env->state;
    PathStyle s = st->p.path;
    // Level path: N stays world up, so the floor is the plane y = -floorDepth.
    s.radiusNoiseAmp = 0.0f;
    s.radiusNoiseScale = 100.0f;
    s.maxPitchDeg = 0.0f;
    s.pitchSpring = 0.0f;
    s.twistRateMax = 0.0f;
    s.hasWalls = false;
    return s;
}

static void cor_register(Environment *env, ParamRegistry *reg)
{
    CorridorState *st = (CorridorState *)env->state;
    CorridorParams *p = &st->p;
    GridFloorParams *f = &p->floor;
    const char *g = "Path";
    params_float(reg, g, "corLaneHalfWidth", &p->path.baseRadius, 6.5f, 3.0f, 20.0f);
    params_float(reg, g, "corYawStepDeg", &p->path.maxYawStepDeg, 3.0f, 0.0f, 20.0f);
    params_float(reg, g, "corFloorDepth", &p->path.floorDepth, 2.2f, 0.8f, 20.0f);
    params_float(reg, g, "corCeilingHeight", &p->path.ceilingHeight, 14.0f, 0.0f, 40.0f);
    params_float(reg, g, "corYawSpring", &p->path.yawSpring, 0.35f, 0.0f, 1.0f);
    p->path.yawAxisDeg = 90.0f;   // pillars and the floor grid are world-axis aligned

    g = "Floor";
    params_bool(reg, g, "corShowFloor", &p->showFloor, true);
    params_float(reg, g, "corGridCell", &f->cell, 2.0f, 0.25f, 16.0f);
    params_float(reg, g, "corGridLineWidth", &f->lineWidth, 0.035f, 0.005f, 0.5f);
    params_float(reg, g, "corGridHaloWidth", &f->haloWidth, 0.08f, 0.0f, 1.0f);
    params_float(reg, g, "corGridIntensity", &f->lineIntensity, 2.6f, 0.0f, 10.0f);
    params_float(reg, g, "corGridHalo", &f->haloIntensity, 0.4f, 0.0f, 3.0f);
    params_float(reg, g, "corGridNodeBoost", &f->nodeBoost, 2.0f, 0.0f, 10.0f);
    params_float(reg, g, "corGridWhiteCore", &f->whiteCore, 0.3f, 0.0f, 1.0f);
    params_float(reg, g, "corFloorFogStart", &f->fogStart, 30.0f, 0.0f, 1000.0f);
    params_float(reg, g, "corFloorFogEnd", &f->fogEnd, 330.0f, 1.0f, 1500.0f);
    params_float(reg, g, "corFloorSheen", &f->sheen, 0.25f, 0.0f, 3.0f);
    params_bool(reg, g, "corGridFilter", &f->filter, true);
    params_float(reg, g, "corGridNodeSize", &f->nodeSize, 0.12f, 0.0f, 1.0f);
    params_float(reg, g, "corFloorReflect", &f->reflectStrength, 1.3f, 0.0f, 3.0f);
    params_float(reg, g, "corFloorReflectR0", &f->reflectR0, 0.3f, 0.0f, 1.0f);
    f->lineColor = COL_CYAN;
    f->floorColor = COL_FLOOR;
    f->horizonColor = COL_HORIZON;

    g = "Corridor";
    params_bool(reg, g, "corShowSky", &p->showSky, true);
    params_float(reg, g, "corFogStart", &p->fogStart, 80.0f, 0.0f, 600.0f);
    params_float(reg, g, "corFogEnd", &p->fogEnd, 340.0f, 1.0f, 800.0f);
    params_float(reg, g, "corHazeWidth", &p->hazeWidth, 0.05f, 0.005f, 0.5f);
    params_float(reg, g, "corHazeGlow", &p->hazeGlow, 1.2f, 0.0f, 8.0f);
    params_float(reg, g, "corSkyDust", &p->dust, 0.02f, 0.0f, 0.3f);
}

static void cor_init(Environment *env, uint64_t seed, ShaderLib *sl)
{
    CorridorState *st = (CorridorState *)env->state;
    st->seed = seed;
    st->sl = sl;
    gridfloor_init(&st->floor, sl);
    if (sl) {
        st->shSky = sl_load(sl, "fullscreen.vs", "sky.fs");
        st->mat = LoadMaterialDefault();
    }
}

static void cor_reseed(Environment *env, uint64_t seed) { ((CorridorState *)env->state)->seed = seed; }

static void cor_build_chunk(Environment *env, const PathSystem *path, int chunkIndex, ChunkCpu *out) { out->archetype = 0; }
static void cor_upload(Environment *env, ChunkCpu *in, ChunkGpu *out) { chunk_upload_default(in, out); }
static void cor_release(Environment *env, ChunkGpu *gpu) { chunk_release_default(gpu); }
static float cor_energy(const Environment *env, const PathSystem *path, double s) { return 0.5f; }

// Enclosed open space: fog to the dark sky instead of toward a core.
static void cor_atmosphere(const Environment *env, Atmosphere *atmo)
{
    const CorridorState *st = (const CorridorState *)env->state;
    atmo->fogStart = st->p.fogStart;
    atmo->fogEnd = st->p.fogEnd;
    atmo->fogToCore = 0.0f;
    atmo->farFadeStart = st->p.fogStart;
    atmo->farFadeEnd = st->p.fogEnd;
}

static void cor_update(Environment *env, const FrameContext *ctx) { }

static void camera_basis(const FrameContext *ctx, Vector3 *right, Vector3 *up, Vector3 *fwd)
{
    Camera3D cam = ctx->camera;
    *fwd = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
    *right = Vector3Normalize(Vector3CrossProduct(*fwd, cam.up));
    *up = Vector3CrossProduct(*right, *fwd);
}

static void cor_draw_background(Environment *env, const FrameContext *ctx)
{
    CorridorState *st = (CorridorState *)env->state;
    if (!st->p.showSky) return;
    ShaderLib *sl = st->sl;
    int sh = st->shSky;
    Vector3 right, up, fwd;
    camera_basis(ctx, &right, &up, &fwd);
    rlDrawRenderBatchActive();
    rlDisableDepthTest();
    rlDisableDepthMask();
    BeginShaderMode(sl_shader(sl, sh));
    sl_f1(sl, sh, "uAspect", (float)ctx->width / (float)ctx->height);
    sl_v3(sl, sh, "uCamRight", right);
    sl_v3(sl, sh, "uCamUp", up);
    sl_v3(sl, sh, "uCamFwd", fwd);
    sl_f1(sl, sh, "uTanHalf", tanf(ctx->fovy * 0.5f * DEG2RAD));
    sl_v3(sl, sh, "uZenith", ct_srgb_to_linear(COL_ZENITH));
    sl_v3(sl, sh, "uHorizon", ct_srgb_to_linear(COL_HORIZON));
    sl_f2(sl, sh, "uHaze", st->p.hazeWidth, st->p.hazeGlow);
    sl_f1(sl, sh, "uDust", st->p.dust);
    pipe_fullscreen_quad();
    EndShaderMode();
    ctx->stats->drawCalls++;
    rlEnableDepthTest();
    rlEnableDepthMask();
}

static void cor_draw_opaque(Environment *env, const FrameContext *ctx, const ChunkGpu *const *chunks, int n)
{
    CorridorState *st = (CorridorState *)env->state;
    if (!st->p.showFloor || ctx->reflectionPass) return;
    GridFloorParams fp = st->p.floor;
    fp.horizonGlow = st->p.hazeGlow;
    double floorY = path_sample(ctx->path, ctx->sShip).pos.y - ctx->path->style.floorDepth;
    gridfloor_draw(&st->floor, &fp, ctx, floorY);
}

static void cor_draw_emissive(Environment *env, const FrameContext *ctx, const ChunkGpu *const *chunks, int n) { }

static const char *cor_label(const Environment *env, int archetype) { return "CORRIDOR"; }

static void cor_unload(Environment *env)
{
    CorridorState *st = (CorridorState *)env->state;
    if (st->sl) { st->mat.shader.id = rlGetShaderIdDefault(); UnloadMaterial(st->mat); }
    free(env->state);
    env->state = NULL;
}

Environment env_corridor_create(void)
{
    Environment e;
    memset(&e, 0, sizeof(e));
    e.name = "Server Corridor";
    e.id = ENV_CORRIDOR;
    e.state = calloc(1, sizeof(CorridorState));
    e.pathReach = 0.0;
    e.reflectiveFloor = true;
    e.pathStyle = cor_path_style;
    e.init = cor_init;
    e.registerParams = cor_register;
    e.reseed = cor_reseed;
    e.buildChunk = cor_build_chunk;
    e.uploadChunk = cor_upload;
    e.releaseChunk = cor_release;
    e.energyAt = cor_energy;
    e.atmosphere = cor_atmosphere;
    e.update = cor_update;
    e.drawBackground = cor_draw_background;
    e.drawOpaque = cor_draw_opaque;
    e.drawEmissive = cor_draw_emissive;
    e.chunkLabel = cor_label;
    e.unload = cor_unload;
    return e;
}
