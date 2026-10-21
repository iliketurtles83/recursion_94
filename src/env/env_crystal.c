// Crystal field (Env #4): a dark open void filled with floating orange wireframe polyhedra,
// clusters on both sides of a clear lane that leads to a warm glow at the vanishing point.
// See PLAN_ENV4_CRYSTAL_FIELD.md.
//
// Open space with no floor and no walls: the lane exists only where the shapes are not, and
// the flight model's steering box is the only limit on the ship.
//
// Phase A: path style, background glow and speed streaks. No polyhedra yet.
#include "env_common.h"
#include "render/pipeline.h"
#include "render/rmath.h"
#include "render/streaks.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define DEG (3.14159265358979f / 180.0f)
#define STREAK_COUNT 1200

enum { CF_EDGES = 0 };   // chunk batches

typedef struct CrystalParams {
    float bgGlow, dust;
    float fogStart, fogEnd;
    bool  showBackground, showStreaks;
    StreakParams streaks;
} CrystalParams;

typedef struct CrystalState {
    uint64_t     seed;
    ShaderLib   *sl;
    int          shBg;
    PathStyle    path;
    CrystalParams p;
    StreakSystem streaks;
} CrystalState;

static const Color CF_BG   = { 0x05, 0x07, 0x0D, 255 };
static const Color CF_GLOW = { 0xC8, 0x8A, 0x50, 255 };

static PathStyle cf_path_style(const Environment *env)
{
    const CrystalState *st = (const CrystalState *)env->state;
    PathStyle s = st->path;
    s.hasWalls = false;
    s.radiusNoiseAmp = 0.0f;     // the lane is the steering box; keep it constant
    s.radiusNoiseScale = 100.0f;
    s.twistRateMax = 0.0f;       // no roll in a void without walls
    return s;
}

static void cf_register(Environment *env, ParamRegistry *reg)
{
    CrystalState *st = (CrystalState *)env->state;
    CrystalParams *p = &st->p;
    const char *g = "Crystal Path";
    params_float(reg, g, "cfLaneRadius", &st->path.baseRadius, 12.0f, 4.0f, 40.0f);
    params_float(reg, g, "cfYawStepDeg", &st->path.maxYawStepDeg, 8.0f, 0.0f, 40.0f);
    params_float(reg, g, "cfPitchDeg", &st->path.maxPitchDeg, 8.0f, 0.0f, 40.0f);
    params_float(reg, g, "cfPitchSpring", &st->path.pitchSpring, 0.2f, 0.0f, 1.0f);

    g = "Crystal";
    params_bool(reg, g, "cfShowBackground", &p->showBackground, true);
    params_float(reg, g, "cfBgGlow", &p->bgGlow, 0.25f, 0.0f, 2.0f);
    params_float(reg, g, "cfDust", &p->dust, 0.04f, 0.0f, 0.3f);
    params_float(reg, g, "cfFogStart", &p->fogStart, 60.0f, 0.0f, 400.0f);
    params_float(reg, g, "cfFogEnd", &p->fogEnd, 320.0f, 20.0f, 800.0f);

    g = "Crystal Streaks";
    StreakParams *sk = &p->streaks;
    params_bool(reg, g, "cfShowStreaks", &p->showStreaks, true);
    params_float(reg, g, "cfStreakDensity", &sk->density, 0.15f, 0.0f, 1.0f);
    params_float(reg, g, "cfStreakIntensity", &sk->intensity, 1.2f, 0.0f, 10.0f);
    params_float(reg, g, "cfStreakLenMul", &sk->lenMul, 1.0f, 0.1f, 5.0f);
    params_float(reg, g, "cfStreakInner", &sk->cylinderInner, 0.6f, 0.0f, 1.2f);
    params_float(reg, g, "cfStreakCenterClear", &sk->centerClear, 0.35f, 0.0f, 1.0f);
    sk->wMin = 0.02f;
    sk->wMax = 0.05f;
    sk->cometFrac = 0.0f;
    sk->accentFrac = 0.0f;
    sk->helixFrac = 0.0f;
    sk->helixRate = 0.0f;
    sk->zRange = 160.0f;
    sk->zBehind = 12.0f;
    sk->speedMul = 1.0f;
    sk->colMain = (Color){ 0xF0, 0xF4, 0xFF, 255 };
    sk->colAccent = sk->colMain;
    sk->volume = 0;
}

static void cf_init(Environment *env, uint64_t seed, ShaderLib *sl)
{
    CrystalState *st = (CrystalState *)env->state;
    st->seed = seed;
    st->sl = sl;
    if (sl) st->shBg = sl_load(sl, "fullscreen.vs", "background.fs");
    streaks_init(&st->streaks, sl, STREAK_COUNT);
}

static void cf_reseed(Environment *env, uint64_t seed) { ((CrystalState *)env->state)->seed = seed; }

static void cf_build_chunk(Environment *env, const PathSystem *path, int chunkIndex, ChunkCpu *out) { }

static void cf_upload(Environment *env, ChunkCpu *in, ChunkGpu *out) { chunk_upload_default(in, out); }
static void cf_release(Environment *env, ChunkGpu *gpu) { chunk_release_default(gpu); }
static float cf_energy(const Environment *env, const PathSystem *path, double s) { return 0.3f; }

// Open void: fog toward the dark background, far shapes sink into it.
static void cf_atmosphere(const Environment *env, Atmosphere *atmo)
{
    const CrystalState *st = (const CrystalState *)env->state;
    atmo->fogStart = st->p.fogStart;
    atmo->fogEnd = st->p.fogEnd;
    atmo->fogToCore = 0.0f;
    atmo->farFadeStart = st->p.fogStart;
    atmo->farFadeEnd = st->p.fogEnd;
}

static void cf_update(Environment *env, const FrameContext *ctx) { }

static void cf_draw_background(Environment *env, const FrameContext *ctx)
{
    CrystalState *st = (CrystalState *)env->state;
    if (!st->p.showBackground || ctx->reflectionPass) return;
    ShaderLib *sl = st->sl;
    int sh = st->shBg;
    Camera3D cam = ctx->camera;
    Vector3 fwd = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
    Vector3 right = Vector3Normalize(Vector3CrossProduct(fwd, cam.up));
    Vector3 up = Vector3CrossProduct(right, fwd);
    rlDrawRenderBatchActive();
    rlDisableDepthTest();
    rlDisableDepthMask();
    BeginShaderMode(sl_shader(sl, sh));
    sl_f2(sl, sh, "uVanish", ctx->vanishingUV.x, ctx->vanishingUV.y);
    sl_f1(sl, sh, "uAspect", (float)ctx->width / (float)ctx->height);
    sl_v3(sl, sh, "uCamRight", right);
    sl_v3(sl, sh, "uCamUp", up);
    sl_v3(sl, sh, "uCamFwd", fwd);
    sl_f1(sl, sh, "uTanHalf", tanf(ctx->fovy * 0.5f * DEG));
    sl_v3(sl, sh, "uBgColor", ct_srgb_to_linear(CF_BG));
    sl_v3(sl, sh, "uGlowColor", ct_srgb_to_linear(CF_GLOW));
    sl_f1(sl, sh, "uGlow", st->p.bgGlow * (0.6f + 0.8f * ctx->intensity));
    sl_f1(sl, sh, "uDust", st->p.dust);
    sl_f1(sl, sh, "uGlowFocus", 1.0f);
    pipe_fullscreen_quad();
    EndShaderMode();
    ctx->stats->drawCalls++;
    rlEnableDepthTest();
    rlEnableDepthMask();
}

static void cf_draw_opaque(Environment *env, const FrameContext *ctx, const ChunkGpu *const *chunks, int n) { }

static void cf_draw_emissive(Environment *env, const FrameContext *ctx, const ChunkGpu *const *chunks, int n)
{
    CrystalState *st = (CrystalState *)env->state;
    if (st->p.showStreaks) streaks_draw(&st->streaks, &st->p.streaks, ctx);
}

static const char *cf_label(const Environment *env, int archetype) { return "CRYSTAL"; }

static void cf_unload(Environment *env)
{
    CrystalState *st = (CrystalState *)env->state;
    streaks_unload(&st->streaks);
    free(st);
    env->state = NULL;
}

Environment env_crystal_create(void)
{
    Environment e;
    memset(&e, 0, sizeof(e));
    e.name = "Crystal Field";
    e.id = ENV_CRYSTAL;
    e.state = calloc(1, sizeof(CrystalState));
    e.pathStyle = cf_path_style;
    e.init = cf_init;
    e.registerParams = cf_register;
    e.reseed = cf_reseed;
    e.buildChunk = cf_build_chunk;
    e.uploadChunk = cf_upload;
    e.releaseChunk = cf_release;
    e.energyAt = cf_energy;
    e.atmosphere = cf_atmosphere;
    e.update = cf_update;
    e.drawBackground = cf_draw_background;
    e.drawOpaque = cf_draw_opaque;
    e.drawEmissive = cf_draw_emissive;
    e.chunkLabel = cf_label;
    e.unload = cf_unload;
    return e;
}
