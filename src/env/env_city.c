// Environment #2: "Neon Grid City" — open flight low over an infinite neon grid floor
// between circuit-traced monolith towers. See PLAN_ENV2_GRID_CITY.md.
//
// Phase A stub: the real path style (level, no twist, floor below the centerline) and
// atmosphere, plus lane-edge markers on the floor plane so motion, steering and the
// floor clamp can be checked. Content arrives in later phases.
#include "env_common.h"
#include "core/rng.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { CB_MARKERS = 0 };   // chunk batches

#define MARKER_STEP 4.0     // lane-edge marker dash spacing (divides CHUNK_LEN)

typedef struct CityParams {
    PathStyle path;
    float markerIntensity;
    float fogStart, fogEnd;
} CityParams;

typedef struct CityState {
    uint64_t   seed;
    CityParams p;
    ShaderLib *sl;
    int        shLines;
    Material   mat;
} CityState;

static const Color COL_CYAN = { 0x4F, 0xEA, 0xF5, 255 };

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
    const char *g = "Path";
    params_float(reg, g, "cityLaneHalfWidth", &p->path.baseRadius, 9.0f, 3.0f, 40.0f);
    params_float(reg, g, "cityYawStepDeg", &p->path.maxYawStepDeg, 4.0f, 0.0f, 20.0f);
    params_float(reg, g, "cityFloorDepth", &p->path.floorDepth, 2.2f, 0.8f, 20.0f);
    g = "City";
    params_float(reg, g, "cityMarkerIntensity", &p->markerIntensity, 2.0f, 0.0f, 10.0f);
    params_float(reg, g, "cityFogStart", &p->fogStart, 120.0f, 0.0f, 600.0f);
    params_float(reg, g, "cityFogEnd", &p->fogEnd, 420.0f, 1.0f, 800.0f);
}

static void city_init(Environment *env, uint64_t seed, ShaderLib *sl)
{
    CityState *st = (CityState *)env->state;
    st->seed = seed;
    st->sl = sl;
    if (sl) {
        st->shLines = sl_load(sl, "object.vs", "line_facing.fs");
        st->mat = LoadMaterialDefault();
    }
}

static void city_reseed(Environment *env, uint64_t seed) { ((CityState *)env->state)->seed = seed; }

// Dashes along both lane edges on the floor plane, plus a cross bar at the chunk start.
static void city_build_chunk(Environment *env, const PathSystem *path, int chunkIndex, ChunkCpu *out)
{
    CityState *st = (CityState *)env->state;
    const PathStyle *style = &path->style;
    LineAttr at = { COL_CYAN, st->p.markerIntensity, 0.0f, 0.0f, -1.0f, 0.0f };
    LineGeo *g = &out->geo[CB_MARKERS];
    DVec3 down = dv3(0, -style->floorDepth, 0);
    for (double s = out->s0; s < out->s1 - 1e-6; s += MARKER_STEP) {
        PathSample a = path_sample(path, s), b = path_sample(path, s + MARKER_STEP * 0.5);
        for (int side = -1; side <= 1; side += 2) {
            DVec3 pa = dv_add(dv_add(a.pos, down), dv_scale(a.B, side * a.radius));
            DVec3 pb = dv_add(dv_add(b.pos, down), dv_scale(b.B, side * b.radius));
            lg_facing_segment(g, env_local(pa, out->origin), env_local(pb, out->origin), 0.06f, 0.06f, at.intensity, at.intensity, at);
        }
        if (s == out->s0) {
            DVec3 l = dv_add(dv_add(a.pos, down), dv_scale(a.B, -a.radius));
            DVec3 r = dv_add(dv_add(a.pos, down), dv_scale(a.B, a.radius));
            lg_facing_segment(g, env_local(l, out->origin), env_local(r, out->origin), 0.04f, 0.04f, at.intensity * 0.5f, at.intensity * 0.5f, at);
        }
    }
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
}

static void city_update(Environment *env, const FrameContext *ctx) { }
static void city_draw_background(Environment *env, const FrameContext *ctx) { }
static void city_draw_opaque(Environment *env, const FrameContext *ctx, const ChunkGpu *const *chunks, int n) { }

static void city_draw_emissive(Environment *env, const FrameContext *ctx, const ChunkGpu *const *chunks, int n)
{
    CityState *st = (CityState *)env->state;
    ShaderLib *sl = st->sl;
    int sh = st->shLines;
    env_set_common_uniforms(sl, sh, ctx);
    sl_f1(sl, sh, "uEdgeMix", 0.0f);
    sl_f1(sl, sh, "uFlicker", 0.0f);
    st->mat.shader = sl_shader(sl, sh);
    for (int i = 0; i < n; i++) {
        const ChunkGpu *c = chunks[i];
        sl_f1(sl, sh, "uChunkFade", env_chunk_fade(c, ctx->time));
        Matrix xf = env_chunk_transform(c, ctx->renderOrigin);
        for (int m = 0; m < c->meshCount; m++) {
            DrawMesh(c->meshes[m], st->mat, xf);
            ctx->stats->drawCalls++;
            ctx->stats->vertices += c->meshes[m].vertexCount;
        }
    }
}

static const char *city_label(const Environment *env, int archetype) { return "CITY"; }

static void city_unload(Environment *env)
{
    CityState *st = (CityState *)env->state;
    if (st->sl) { st->mat.shader.id = rlGetShaderIdDefault(); UnloadMaterial(st->mat); }
    free(st);
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
