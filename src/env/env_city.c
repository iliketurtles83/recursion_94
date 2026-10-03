// Environment #2: "Neon Grid City" — open flight low over an infinite neon grid floor
// between circuit-traced monolith towers. See PLAN_ENV2_GRID_CITY.md.
//
// Phases done: A (path style: level, no twist, floor below the centerline; atmosphere),
// B (grid floor, sky) and C (tower placement and bodies, city_towers.c).
#include "env_common.h"
#include "city_towers.h"
#include "core/rng.h"
#include "render/gridfloor.h"
#include "render/rmath.h"
#include "render/pipeline.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { CB_BODY = 0, CB_EDGES };   // chunk batches

typedef struct CityParams {
    PathStyle path;
    GridFloorParams floor;
    CityTowerParams towers;
    float edgeIntensity, edgeWidth;
    float bodyBrightness, rimIntensity, underglow, panelW, panelH, seamDark, recessed;
    bool  showTowers;
    float fogStart, fogEnd;          // fog for chunk content (towers etc.)
    float hazeWidth, hazeGlow, dust; // sky
    bool  showFloor, showSky;
} CityParams;

typedef struct CityState {
    uint64_t   seed;
    CityParams p;
    ShaderLib *sl;
    int        shSky, shTower, shLines;
    Material   mat;
    GridFloor  floor;
    TowerDesc  towers[CITY_MAX_TOWERS];   // build scratch (builds are single-threaded)
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

    g = "Towers";
    CityTowerParams *t = &p->towers;
    params_bool(reg, g, "showTowers", &p->showTowers, true);
    params_int(reg, g, "towerLatticeCells", &t->latticeCells, 8, 3, 16);
    params_float(reg, g, "towerClearance", &t->clearance, 6.0f, 0.0f, 40.0f);
    params_float(reg, g, "towerBandMax", &t->bandMax, 150.0f, 20.0f, 300.0f);
    params_float(reg, g, "towerDensityNear", &t->densityNear, 0.5f, 0.0f, 1.0f);
    params_float(reg, g, "towerDensityFar", &t->densityFar, 0.3f, 0.0f, 1.0f);
    params_float(reg, g, "towerHeightMin", &t->heightMin, 28.0f, 2.0f, 200.0f);
    params_float(reg, g, "towerHeightMax", &t->heightMax, 65.0f, 2.0f, 300.0f);
    params_float(reg, g, "towerHeightFar", &t->heightFar, 40.0f, 0.0f, 300.0f);
    params_float(reg, g, "landmarkChance", &t->landmarkChance, 0.05f, 0.0f, 1.0f);
    params_float(reg, g, "landmarkMul", &t->landmarkMul, 1.7f, 1.0f, 4.0f);
    params_int(reg, g, "towerFootMin", &t->footMin, 3, 2, 15);
    params_int(reg, g, "towerFootMax", &t->footMax, 5, 2, 15);
    params_float(reg, g, "setbackChance", &t->setbackChance, 0.55f, 0.0f, 1.0f);
    params_float(reg, g, "towerEdgeIntensity", &p->edgeIntensity, 0.9f, 0.0f, 8.0f);
    params_float(reg, g, "towerEdgeWidth", &p->edgeWidth, 0.03f, 0.005f, 0.3f);
    params_float(reg, g, "towerBrightness", &p->bodyBrightness, 2.0f, 0.0f, 8.0f);
    params_float(reg, g, "towerRim", &p->rimIntensity, 0.08f, 0.0f, 2.0f);
    params_float(reg, g, "towerUnderglow", &p->underglow, 0.06f, 0.0f, 2.0f);
    params_float(reg, g, "panelW", &p->panelW, 1.1f, 0.2f, 8.0f);
    params_float(reg, g, "panelH", &p->panelH, 2.2f, 0.2f, 16.0f);
    params_float(reg, g, "seamDark", &p->seamDark, 0.6f, 0.0f, 1.0f);
    params_float(reg, g, "recessed", &p->recessed, 0.15f, 0.0f, 1.0f);

    g = "City";
    params_bool(reg, g, "showSky", &p->showSky, true);
    params_float(reg, g, "cityFogStart", &p->fogStart, 120.0f, 0.0f, 600.0f);
    // Fog must be complete before ~358 units: the far chunk becomes visible with its nearest
    // towers about that far away (window s + 400, chunk length 48, camera 6 behind the ship).
    params_float(reg, g, "cityFogEnd", &p->fogEnd, 350.0f, 1.0f, 800.0f);
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
    if (sl) {
        st->shSky = sl_load(sl, "fullscreen.vs", "sky.fs");
        st->shTower = sl_load(sl, "tower.vs", "tower.fs");
        st->shLines = sl_load(sl, "object.vs", "line_facing.fs");
        st->mat = LoadMaterialDefault();
    }
}

// Tower params that follow other settings (floor grid, lane, floor height).
static CityTowerParams tower_params(const CityState *st, const PathSystem *path)
{
    CityTowerParams t = st->p.towers;
    t.cell = st->p.floor.cell;
    t.laneHalfWidth = path->style.baseRadius;
    t.floorY = (float)(path_sample(path, 0.0).pos.y - path->style.floorDepth);   // level path: y is constant
    return t;
}

static void city_reseed(Environment *env, uint64_t seed) { ((CityState *)env->state)->seed = seed; }

static void city_build_chunk(Environment *env, const PathSystem *path, int chunkIndex, ChunkCpu *out)
{
    CityState *st = (CityState *)env->state;
    CityTowerParams tp = tower_params(st, path);
    int n = city_towers_plan(&tp, st->seed, path, chunkIndex, st->towers, CITY_MAX_TOWERS, NULL, 0, NULL);
    city_towers_build(st->towers, n, out->origin, &out->geo[CB_BODY], &out->geo[CB_EDGES], st->p.edgeIntensity, st->p.edgeWidth);
}

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
    atmo->farFadeStart = st->p.fogStart;   // emissive lines fade out with the tower fog
    atmo->farFadeEnd = st->p.fogEnd;
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

static void draw_batch(CityState *st, const FrameContext *ctx, int sh, const ChunkGpu *const *chunks, int n, int batch)
{
    st->mat.shader = sl_shader(st->sl, sh);
    for (int i = 0; i < n; i++) {
        const ChunkGpu *c = chunks[i];
        Matrix xf = env_chunk_transform(c, ctx->renderOrigin);
        for (int m = 0; m < c->meshCount; m++) {
            if (c->meshBatch[m] != batch) continue;
            if (batch == CB_EDGES) sl_f1(st->sl, sh, "uChunkFade", env_chunk_fade(c, ctx->time));
            DrawMesh(c->meshes[m], st->mat, xf);
            ctx->stats->drawCalls++;
            ctx->stats->vertices += c->meshes[m].vertexCount;
        }
    }
}

static void draw_towers(CityState *st, const FrameContext *ctx, const ChunkGpu *const *chunks, int n)
{
    const CityParams *p = &st->p;
    ShaderLib *sl = st->sl;
    int sh = st->shTower;
    env_set_common_uniforms(sl, sh, ctx);
    Vector3 body = ct_srgb_to_linear((Color){ 0x1C, 0x24, 0x34, 255 });
    Vector3 cyan = ct_srgb_to_linear(COL_CYAN);
    sl_v3(sl, sh, "uBody", Vector3Scale(body, p->bodyBrightness));
    sl_v3(sl, sh, "uRim", Vector3Scale(cyan, p->rimIntensity));
    sl_v3(sl, sh, "uUnderglow", Vector3Scale(cyan, p->underglow));
    sl_v3(sl, sh, "uKeyDir", Vector3Normalize((Vector3){ 0.35f, 0.55f, 0.75f }));
    sl_f4(sl, sh, "uPanel", p->panelW, p->panelH, p->seamDark, p->recessed);
    sl_f2(sl, sh, "uFogRange", p->fogStart, p->fogEnd);
    sl_v3(sl, sh, "uZenith", ct_srgb_to_linear(COL_ZENITH));
    sl_v3(sl, sh, "uHorizon", ct_srgb_to_linear(COL_HORIZON));
    sl_f2(sl, sh, "uHaze", p->hazeWidth, p->hazeGlow);
    draw_batch(st, ctx, sh, chunks, n, CB_BODY);
}

static void city_draw_opaque(Environment *env, const FrameContext *ctx, const ChunkGpu *const *chunks, int n)
{
    CityState *st = (CityState *)env->state;
    if (st->p.showTowers) draw_towers(st, ctx, chunks, n);
    if (!st->p.showFloor) return;
    // The floor fogs into exactly the sky's horizon colour.
    GridFloorParams fp = st->p.floor;
    fp.horizonGlow = st->p.hazeGlow;
    double floorY = path_sample(ctx->path, ctx->sShip).pos.y - ctx->path->style.floorDepth;
    gridfloor_draw(&st->floor, &fp, ctx, floorY);
}

static void city_draw_emissive(Environment *env, const FrameContext *ctx, const ChunkGpu *const *chunks, int n)
{
    CityState *st = (CityState *)env->state;
    if (!st->p.showTowers) return;
    ShaderLib *sl = st->sl;
    int sh = st->shLines;
    env_set_common_uniforms(sl, sh, ctx);
    sl_f1(sl, sh, "uEdgeMix", 0.0f);
    sl_f1(sl, sh, "uFlicker", 0.0f);
    draw_batch(st, ctx, sh, chunks, n, CB_EDGES);
}

static const char *city_label(const Environment *env, int archetype) { return "CITY"; }

static void city_unload(Environment *env)
{
    CityState *st = (CityState *)env->state;
    if (st->sl) { st->mat.shader.id = rlGetShaderIdDefault(); UnloadMaterial(st->mat); }
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
    e.pathReach = 2.2 * 300.0 + 8.0;   // city_towers_reach() at the largest towerBandMax
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
