// Environment #2: "Neon Grid City" — open flight low over an infinite neon grid floor
// between circuit-traced monolith towers. See PLAN_ENV2_GRID_CITY.md.
//
// Phases done: A (path style: level, no twist, floor below the centerline; atmosphere)
// and B (grid floor, sky). Chunks carry no content yet; towers arrive in Phase C.
#include "env_common.h"
#include "core/rng.h"
#include "render/gridfloor.h"
#include "render/rmath.h"
#include "render/pipeline.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct CityParams {
    PathStyle path;
    GridFloorParams floor;
    float fogStart, fogEnd;          // fog for chunk content (towers etc.)
    float hazeWidth, hazeGlow, dust; // sky
    bool  showFloor, showSky;
} CityParams;

typedef struct CityState {
    uint64_t   seed;
    CityParams p;
    ShaderLib *sl;
    int        shSky;
    GridFloor  floor;
} CityState;

static const Color COL_CYAN    = { 0x4F, 0xEA, 0xF5, 255 };
static const Color COL_FLOOR   = { 0x02, 0x06, 0x0A, 255 };
static const Color COL_HORIZON = { 0x0B, 0x2A, 0x3A, 255 };
static const Color COL_ZENITH  = { 0x02, 0x03, 0x08, 255 };

static PathStyle city_path_style(const Environment *env)
{
    const CityState *st = (const CityState *)env->state;
    PathStyle s = st->p.path;
    // Level path: N stays world up, so the floor is the plane y = -floorDepth.
    s.radiusNoiseAmp = 0.0f;
    s.radiusNoiseScale = 100.0f;
    s.maxPitchDeg = 0.0f;
    s.pitchSpring = 0.0f;
    s.twistRateMax = 0.0f;
    s.hasWalls = false;
    s.ceilingHeight = 0.0f;
    return s;
}

static void city_register(Environment *env, ParamRegistry *reg)
{
    CityState *st = (CityState *)env->state;
    CityParams *p = &st->p;
    GridFloorParams *f = &p->floor;
    const char *g = "Path";
    params_float(reg, g, "cityLaneHalfWidth", &p->path.baseRadius, 9.0f, 3.0f, 40.0f);
    params_float(reg, g, "cityYawStepDeg", &p->path.maxYawStepDeg, 4.0f, 0.0f, 20.0f);
    params_float(reg, g, "cityFloorDepth", &p->path.floorDepth, 1.4f, 0.8f, 20.0f);
    params_float(reg, g, "cityYawSpring", &p->path.yawSpring, 0.12f, 0.0f, 1.0f);
    p->path.yawAxisDeg = 90.0f;   // the floor grid is world-axis aligned

    g = "Floor";
    params_bool(reg, g, "showFloor", &p->showFloor, true);
    params_float(reg, g, "gridCell", &f->cell, 2.2f, 0.25f, 16.0f);
    params_float(reg, g, "gridLineWidth", &f->lineWidth, 0.03f, 0.005f, 0.5f);
    params_float(reg, g, "gridHaloWidth", &f->haloWidth, 0.07f, 0.0f, 1.0f);
    params_float(reg, g, "gridIntensity", &f->lineIntensity, 2.2f, 0.0f, 10.0f);
    params_float(reg, g, "gridHalo", &f->haloIntensity, 0.35f, 0.0f, 3.0f);
    params_float(reg, g, "gridNodeBoost", &f->nodeBoost, 1.5f, 0.0f, 10.0f);
    params_float(reg, g, "gridWhiteCore", &f->whiteCore, 0.25f, 0.0f, 1.0f);
    params_float(reg, g, "floorFogStart", &f->fogStart, 40.0f, 0.0f, 1000.0f);
    params_float(reg, g, "floorFogEnd", &f->fogEnd, 450.0f, 1.0f, 1500.0f);
    params_float(reg, g, "floorSheen", &f->sheen, 0.3f, 0.0f, 3.0f);
    params_bool(reg, g, "gridFilter", &f->filter, true);
    f->lineColor = COL_CYAN;
    f->floorColor = COL_FLOOR;
    f->horizonColor = COL_HORIZON;

    g = "City";
    params_bool(reg, g, "showSky", &p->showSky, true);
    params_float(reg, g, "cityFogStart", &p->fogStart, 120.0f, 0.0f, 600.0f);
    params_float(reg, g, "cityFogEnd", &p->fogEnd, 420.0f, 1.0f, 800.0f);
    params_float(reg, g, "hazeWidth", &p->hazeWidth, 0.06f, 0.005f, 0.5f);
    params_float(reg, g, "hazeGlow", &p->hazeGlow, 1.6f, 0.0f, 8.0f);
    params_float(reg, g, "skyDust", &p->dust, 0.02f, 0.0f, 0.3f);
}

static void city_init(Environment *env, uint64_t seed, ShaderLib *sl)
{
    CityState *st = (CityState *)env->state;
    st->seed = seed;
    st->sl = sl;
    gridfloor_init(&st->floor, sl);
    if (sl) st->shSky = sl_load(sl, "fullscreen.vs", "sky.fs");
}

static void city_reseed(Environment *env, uint64_t seed) { ((CityState *)env->state)->seed = seed; }

static void city_build_chunk(Environment *env, const PathSystem *path, int chunkIndex, ChunkCpu *out) { }

static void city_upload(Environment *env, ChunkCpu *in, ChunkGpu *out) { chunk_upload_default(in, out); }
static void city_release(Environment *env, ChunkGpu *gpu) { chunk_release_default(gpu); }
static float city_energy(const Environment *env, const PathSystem *path, double s) { return 0.4f; }

// Open space: fog to the dark sky instead of toward a core.
static void city_atmosphere(const Environment *env, Atmosphere *atmo)
{
    const CityState *st = (const CityState *)env->state;
    atmo->fogStart = st->p.fogStart;
    atmo->fogEnd = st->p.fogEnd;
    atmo->fogToCore = 0.0f;
}

static void city_update(Environment *env, const FrameContext *ctx) { }

static void camera_basis(const FrameContext *ctx, Vector3 *right, Vector3 *up, Vector3 *fwd)
{
    Camera3D cam = ctx->camera;
    *fwd = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
    *right = Vector3Normalize(Vector3CrossProduct(*fwd, cam.up));
    *up = Vector3CrossProduct(*right, *fwd);
}

static void city_draw_background(Environment *env, const FrameContext *ctx)
{
    CityState *st = (CityState *)env->state;
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

static void city_draw_opaque(Environment *env, const FrameContext *ctx, const ChunkGpu *const *chunks, int n)
{
    CityState *st = (CityState *)env->state;
    if (!st->p.showFloor) return;
    // The floor fogs into exactly the sky's horizon colour.
    GridFloorParams fp = st->p.floor;
    fp.horizonGlow = st->p.hazeGlow;
    double floorY = path_sample(ctx->path, ctx->sShip).pos.y - ctx->path->style.floorDepth;
    gridfloor_draw(&st->floor, &fp, ctx, floorY);
}

static void city_draw_emissive(Environment *env, const FrameContext *ctx, const ChunkGpu *const *chunks, int n) { }

static const char *city_label(const Environment *env, int archetype) { return "CITY"; }

static void city_unload(Environment *env)
{
    free(env->state);
    env->state = NULL;
}

Environment env_city_create(void)
{
    Environment e;
    memset(&e, 0, sizeof(e));
    e.name = "Neon Grid City";
    e.id = ENV_CITY;
    e.state = calloc(1, sizeof(CityState));
    e.pathStyle = city_path_style;
    e.init = city_init;
    e.registerParams = city_register;
    e.reseed = city_reseed;
    e.buildChunk = city_build_chunk;
    e.uploadChunk = city_upload;
    e.releaseChunk = city_release;
    e.energyAt = city_energy;
    e.atmosphere = city_atmosphere;
    e.update = city_update;
    e.drawBackground = city_draw_background;
    e.drawOpaque = city_draw_opaque;
    e.drawEmissive = city_draw_emissive;
    e.chunkLabel = city_label;
    e.unload = city_unload;
    return e;
}
