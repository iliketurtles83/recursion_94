// Crystal field (Env #4): a dark open void filled with floating orange wireframe polyhedra,
// clusters on both sides of a clear lane that leads to a warm glow at the vanishing point.
// See PLAN_ENV4_CRYSTAL_FIELD.md.
//
// Open space with no floor and no walls: the lane exists only where the shapes are not, and
// the flight model's steering box is the only limit on the ship.
//
// Placement and geometry of the polyhedra: crystal_field.c.
#include "env_common.h"
#include "crystal_field.h"
#include "core/rng.h"
#include "render/pipeline.h"
#include "render/rmath.h"
#include "render/streaks.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define DEG (3.14159265358979f / 180.0f)
#define STREAK_COUNT 1200

enum { CB_EDGES = 0, CB_FACES = 1 };   // chunk batches
enum { F_FIELD = 1 };    // RNG feature streams
_Static_assert(2 * CF_TUMBLE_SLOTS <= CHUNK_UNIFORMS, "tumble slots must fit the chunk uniforms");

typedef struct CrystalParams {
    float bgGlow, dust;
    float fogStart, fogEnd;
    bool  showBackground, showStreaks, showShapes, showFaces;
    float whiteCore, faceFresnel;
    float clearStrength, clearRadius, clearNear, clearFar;
    CrystalFieldParams field;
    StreakParams streaks;
} CrystalParams;

typedef struct CrystalState {
    uint64_t     seed;
    ShaderLib   *sl;
    int          shBg, shEdge, shFace;
    Material     mat;
    ParamRegistry *reg;     // registry of the last registerParams call, for the flight's steerLimit
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
    st->reg = reg;
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

    g = "Crystal Field";
    CrystalFieldParams *f = &p->field;
    params_bool(reg, g, "cfShowShapes", &p->showShapes, true);
    params_float(reg, g, "cfClearance", &f->clearance, 3.0f, 0.0f, 20.0f);
    params_float(reg, g, "cfBandMax", &f->bandMax, 90.0f, 20.0f, 200.0f);
    params_float(reg, g, "cfClusters", &f->clusters, 3.0f, 0.0f, 10.0f);
    params_float(reg, g, "cfScatter", &f->scatter, 6.0f, 0.0f, 30.0f);
    params_int(reg, g, "cfClusterMin", &f->clusterMin, 3, 1, 30);
    params_int(reg, g, "cfClusterMax", &f->clusterMax, 12, 1, 30);
    params_float(reg, g, "cfClusterRadMin", &f->clusterRadMin, 4.0f, 0.0f, 40.0f);
    params_float(reg, g, "cfClusterRadMax", &f->clusterRadMax, 14.0f, 0.0f, 40.0f);
    params_float(reg, g, "cfSideBias", &f->sideBias, 0.75f, 0.0f, 1.0f);
    params_float(reg, g, "cfNearBias", &f->nearBias, 1.5f, 0.3f, 4.0f);
    params_float(reg, g, "cfGiantFrac", &f->giantFrac, 0.03f, 0.0f, 0.3f);
    params_float(reg, g, "cfMediumFrac", &f->mediumFrac, 0.35f, 0.0f, 1.0f);
    params_float(reg, g, "cfEdgeIntensity", &f->edgeIntensity, 1.5f, 0.0f, 10.0f);
    params_float(reg, g, "cfEdgeWidth", &f->edgeWidth, 0.012f, 0.0f, 0.1f);
    params_float(reg, g, "cfEdgeWidthMin", &f->edgeWidthMin, 0.02f, 0.0f, 0.2f);
    params_float(reg, g, "cfFaceIntensity", &f->faceIntensity, 0.025f, 0.0f, 1.0f);
    params_float(reg, g, "cfTumble", &f->tumble, 0.3f, 0.0f, 2.0f);
    f->smallMin = 0.5f; f->smallMax = 2.0f;
    f->mediumMin = 2.0f; f->mediumMax = 6.0f;
    f->giantMin = 8.0f; f->giantMax = 16.0f;

    g = "Crystal Look";
    params_bool(reg, g, "cfShowFaces", &p->showFaces, true);
    params_float(reg, g, "cfWhiteCore", &p->whiteCore, 0.15f, 0.0f, 1.0f);
    params_float(reg, g, "cfFaceFresnel", &p->faceFresnel, 1.5f, 0.0f, 10.0f);
    params_float(reg, g, "cfClearStrength", &p->clearStrength, 0.8f, 0.0f, 1.0f);
    params_float(reg, g, "cfClearRadius", &p->clearRadius, 0.2f, 0.01f, 1.0f);
    params_float(reg, g, "cfClearNear", &p->clearNear, 50.0f, 0.0f, 400.0f);
    params_float(reg, g, "cfClearFar", &p->clearFar, 140.0f, 0.0f, 600.0f);

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
    if (sl) {
        st->shBg = sl_load(sl, "fullscreen.vs", "background.fs");
        st->shEdge = sl_load(sl, "crystal.vs", "crystal_edge.fs");
        st->shFace = sl_load(sl, "crystal_face.vs", "crystal_face.fs");
        st->mat = LoadMaterialDefault();
    }
    streaks_init(&st->streaks, sl, STREAK_COUNT);
}

static void cf_reseed(Environment *env, uint64_t seed) { ((CrystalState *)env->state)->seed = seed; }

// The flight model clamps the ship to steerLimit x radius. Read it from the registry when the
// flight parameters are present (in the running module); tests fall back to the default.
static CrystalFieldParams field_params(const CrystalState *st)
{
    CrystalFieldParams f = st->p.field;
    Param *lim = st->reg ? params_find(st->reg, "steerLimit") : NULL;
    f.steerLimit = lim ? params_get(lim) : 0.8f;
    f.laneRadius = st->path.baseRadius;
    return f;
}

static void cf_build_chunk(Environment *env, const PathSystem *path, int chunkIndex, ChunkCpu *out)
{
    CrystalState *st = (CrystalState *)env->state;
    if (!st->p.showShapes) return;
    CrystalFieldParams f = field_params(st);
    static CrystalShape shapes[CF_MAX_SHAPES];
    uint64_t base = ct_hash64_3(st->seed, ((uint64_t)ENV_CRYSTAL << 8) | F_FIELD, (uint64_t)(int64_t)chunkIndex);
    int n = crystal_field_plan(&f, path, base, out->s0, out->s1, shapes, CF_MAX_SHAPES);
    crystal_field_build(&f, shapes, n, out->origin, &out->geo[CB_EDGES], &out->geo[CB_FACES], out->uniforms);
}

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

// Uniforms shared by the edge and face programs: atmosphere and the clear centre.
static void set_shape_uniforms(CrystalState *st, int sh, const FrameContext *ctx)
{
    const CrystalParams *p = &st->p;
    env_set_common_uniforms(st->sl, sh, ctx);
    sl_f2(st->sl, sh, "uVanishPx", ctx->vanishingUV.x * (float)ctx->width, ctx->vanishingUV.y * (float)ctx->height);
    sl_f4(st->sl, sh, "uClear", p->clearStrength, p->clearRadius * (float)ctx->height, p->clearNear, p->clearFar);
}

static void draw_batch(CrystalState *st, const FrameContext *ctx, int sh, const ChunkGpu *const *chunks, int n, int batch)
{
    st->mat.shader = sl_shader(st->sl, sh);
    for (int i = 0; i < n; i++) {
        const ChunkGpu *c = chunks[i];
        sl_f1(st->sl, sh, "uChunkFade", env_chunk_fade(c, ctx->time));
        sl_vec4v(st->sl, sh, "uShapeA", c->uniforms[0], CF_TUMBLE_SLOTS);
        sl_vec4v(st->sl, sh, "uShapeB", c->uniforms[CF_TUMBLE_SLOTS], CF_TUMBLE_SLOTS);
        Matrix xf = env_chunk_transform(c, ctx->renderOrigin);
        for (int m = 0; m < c->meshCount; m++) {
            if (c->meshBatch[m] != batch) continue;
            DrawMesh(c->meshes[m], st->mat, xf);
            ctx->stats->drawCalls++;
            ctx->stats->vertices += c->meshes[m].vertexCount;
        }
    }
}

static void cf_draw_emissive(Environment *env, const FrameContext *ctx, const ChunkGpu *const *chunks, int n)
{
    CrystalState *st = (CrystalState *)env->state;
    const CrystalParams *p = &st->p;
    if (p->showShapes) {
        if (p->showFaces) {
            set_shape_uniforms(st, st->shFace, ctx);
            sl_f1(st->sl, st->shFace, "uFaceBase", 1.0f);
            sl_f1(st->sl, st->shFace, "uFaceFresnel", p->faceFresnel);
            draw_batch(st, ctx, st->shFace, chunks, n, CB_FACES);
        }
        set_shape_uniforms(st, st->shEdge, ctx);
        sl_f1(st->sl, st->shEdge, "uWhiteCore", p->whiteCore);
        draw_batch(st, ctx, st->shEdge, chunks, n, CB_EDGES);
    }
    if (st->p.showStreaks) streaks_draw(&st->streaks, &st->p.streaks, ctx);
}

static const char *cf_label(const Environment *env, int archetype) { return "CRYSTAL"; }

static void cf_unload(Environment *env)
{
    CrystalState *st = (CrystalState *)env->state;
    streaks_unload(&st->streaks);
    if (st->sl) { st->mat.shader.id = rlGetShaderIdDefault(); UnloadMaterial(st->mat); }
    free(st);
    env->state = NULL;
}

Environment env_crystal_create(void)
{
    Environment e;
    memset(&e, 0, sizeof(e));
    e.name = "Crystal Field";
    e.id = ENV_CRYSTAL;
    e.pathReach = 2.0 * (200.0 + 40.0 + 16.0 * 3.0) + 20.0;   // crystal_path_reach() at the largest cfBandMax and cfClusterRadMax
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
