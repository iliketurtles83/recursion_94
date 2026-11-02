// Greybox: a plain measuring course for tuning ship movement without visual noise.
//
// The path uses the vortex's default path style, so with the same seed the course is the one
// the vortex is built on. Content is only reference geometry, in flat greys:
//   - distance rings on the tunnel radius every gbRingSpacing metres, brighter at 50 m and
//     brighter again at 100 m, so speed and distance can be read directly;
//   - lengthwise rails at gbRails equal angles on the tunnel radius, to show curvature and
//     lateral drift;
//   - an amber steering-envelope circle every 50 m at steerLimit x radius: the furthest the
//     flight model lets the ship move off the centreline.
#include "env_common.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { GB_LINES = 0 };

typedef struct GreyboxState {
    uint64_t       seed;
    ShaderLib     *sl;
    int            shLines;
    Material       mat;
    PathStyle      path;
    float          ringSpacing, intensity;
    int            rails;
    bool           showRings, showRails, showEnvelope;
    ParamRegistry *reg;     // registry of the last registerParams call, for the flight's steerLimit
} GreyboxState;

static const Color GB_MINOR    = { 0x70, 0x70, 0x70, 255 };
static const Color GB_MID      = { 0xA8, 0xA8, 0xA8, 255 };
static const Color GB_MAJOR    = { 0xE6, 0xE6, 0xE6, 255 };
static const Color GB_RAIL     = { 0x58, 0x58, 0x58, 255 };
static const Color GB_ENVELOPE = { 0xFF, 0x9A, 0x3C, 255 };

static PathStyle gb_path_style(const Environment *env)
{
    const GreyboxState *st = (const GreyboxState *)env->state;
    PathStyle s = st->path;
    s.hasWalls = true;
    return s;
}

static void gb_register(Environment *env, ParamRegistry *reg)
{
    GreyboxState *st = (GreyboxState *)env->state;
    st->reg = reg;
    // Defaults match the vortex "Path" group, so the same seed gives the same course.
    const char *g = "Greybox";
    params_float(reg, g, "gbRadius", &st->path.baseRadius, 12.0f, 3.0f, 40.0f);
    params_float(reg, g, "gbRadiusNoiseAmp", &st->path.radiusNoiseAmp, 0.12f, 0.0f, 0.5f);
    params_float(reg, g, "gbRadiusNoiseScale", &st->path.radiusNoiseScale, 60.0f, 5.0f, 400.0f);
    params_float(reg, g, "gbYawStepDeg", &st->path.maxYawStepDeg, 14.0f, 0.0f, 40.0f);
    params_float(reg, g, "gbPitchDeg", &st->path.maxPitchDeg, 20.0f, 0.0f, 60.0f);
    params_float(reg, g, "gbPitchSpring", &st->path.pitchSpring, 0.15f, 0.0f, 1.0f);
    params_float(reg, g, "gbTwistRateMax", &st->path.twistRateMax, 0.015f, 0.0f, 0.1f);
    params_float(reg, g, "gbRingSpacing", &st->ringSpacing, 10.0f, 2.0f, 50.0f);
    params_int(reg, g, "gbRails", &st->rails, 8, 0, 16);
    params_float(reg, g, "gbIntensity", &st->intensity, 0.7f, 0.0f, 4.0f);
    params_bool(reg, g, "gbShowRings", &st->showRings, true);
    params_bool(reg, g, "gbShowRails", &st->showRails, true);
    params_bool(reg, g, "gbShowEnvelope", &st->showEnvelope, true);
}

static void gb_init(Environment *env, uint64_t seed, ShaderLib *sl)
{
    GreyboxState *st = (GreyboxState *)env->state;
    st->seed = seed;
    st->sl = sl;
    if (sl) {
        st->shLines = sl_load(sl, "object.vs", "line_facing.fs");
        st->mat = LoadMaterialDefault();
    }
}

static void gb_reseed(Environment *env, uint64_t seed) { ((GreyboxState *)env->state)->seed = seed; }

static Vector3 gb_point(const PathSample *ps, double r, double a, DVec3 origin)
{
    DVec3 w = dv_add(ps->pos, dv_add(dv_scale(ps->B, cos(a) * r), dv_scale(ps->N, sin(a) * r)));
    return env_local(w, origin);
}

static void gb_circle(LineGeo *g, const PathSample *ps, double r, DVec3 origin, float width, LineAttr at)
{
    const int segs = 72;
    Vector3 prev = gb_point(ps, r, 0.0, origin);
    for (int i = 1; i <= segs; i++) {
        Vector3 p = gb_point(ps, r, 6.283185307 * i / segs, origin);
        lg_facing_segment(g, prev, p, width, width, at.intensity, at.intensity, at);
        prev = p;
    }
}

// The flight model clamps the ship to steerLimit x radius. Read it from the registry when the
// flight parameters are present (in the running module); tests fall back to the default.
static float gb_steer_limit(const GreyboxState *st)
{
    Param *p = st->reg ? params_find(st->reg, "steerLimit") : NULL;
    return p ? params_get(p) : 0.55f;
}

static void gb_build_chunk(Environment *env, const PathSystem *path, int chunkIndex, ChunkCpu *out)
{
    GreyboxState *st = (GreyboxState *)env->state;
    LineGeo *g = &out->geo[GB_LINES];
    float I = st->intensity;

    // Rings at whole multiples of the spacing; each belongs to the chunk with s0 <= s < s1.
    if (st->showRings) {
        double sp = st->ringSpacing;
        for (int64_t k = (int64_t)ceil(out->s0 / sp - 1e-9); k * sp < out->s1; k++) {
            double s = k * sp;
            PathSample ps = path_sample(path, s);
            long m = lround(s);
            bool on50 = fabs(s - (double)m) < 1e-6 && m % 50 == 0;
            bool on100 = on50 && m % 100 == 0;
            Color c = on100 ? GB_MAJOR : (on50 ? GB_MID : GB_MINOR);
            float w = on100 ? 0.07f : (on50 ? 0.05f : 0.03f);
            float in = I * (on100 ? 1.6f : (on50 ? 1.2f : 1.0f));
            gb_circle(g, &ps, ps.radius, out->origin, w, (LineAttr){ c, in, 0.0f, 0.0f, -1.0f, 0.0f });
        }
    }

    // Steering envelope every 50 m, independent of the ring spacing.
    if (st->showEnvelope) {
        float limit = gb_steer_limit(st);
        for (int64_t k = (int64_t)ceil(out->s0 / 50.0 - 1e-9); k * 50.0 < out->s1; k++) {
            PathSample ps = path_sample(path, k * 50.0);
            gb_circle(g, &ps, ps.radius * limit, out->origin, 0.025f, (LineAttr){ GB_ENVELOPE, I * 0.8f, 0.0f, 0.0f, -1.0f, 0.0f });
        }
    }

    // Rails: polylines every 2 m, continuous across chunk boundaries (both ends sampled exactly).
    if (st->showRails && st->rails > 0) {
        const int steps = (int)ceil(CHUNK_LEN / 2.0);
        LineAttr at = { GB_RAIL, I, 0.0f, 0.0f, -1.0f, 0.0f };
        for (int r = 0; r < st->rails; r++) {
            double a = 6.283185307 * r / st->rails + 3.14159265 / st->rails;   // off the vertical/horizontal axes
            PathSample ps = path_sample(path, out->s0);
            Vector3 prev = gb_point(&ps, ps.radius, a, out->origin);
            for (int i = 1; i <= steps; i++) {
                double s = out->s0 + (out->s1 - out->s0) * i / steps;
                ps = path_sample(path, s);
                Vector3 p = gb_point(&ps, ps.radius, a, out->origin);
                lg_facing_segment(g, prev, p, 0.03f, 0.03f, I, I, at);
                prev = p;
            }
        }
    }
}

static void gb_upload(Environment *env, ChunkCpu *in, ChunkGpu *out) { chunk_upload_default(in, out); }
static void gb_release(Environment *env, ChunkGpu *gpu) { chunk_release_default(gpu); }
static float gb_energy(const Environment *env, const PathSystem *path, double s) { return 0.3f; }
static void gb_update(Environment *env, const FrameContext *ctx) { }
static void gb_draw_background(Environment *env, const FrameContext *ctx) { }
static void gb_draw_opaque(Environment *env, const FrameContext *ctx, const ChunkGpu *const *chunks, int n) { }

static void gb_draw_emissive(Environment *env, const FrameContext *ctx, const ChunkGpu *const *chunks, int n)
{
    GreyboxState *st = (GreyboxState *)env->state;
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

static const char *gb_label(const Environment *env, int archetype) { return "GREYBOX"; }

static void gb_unload(Environment *env)
{
    GreyboxState *st = (GreyboxState *)env->state;
    if (st->sl) { st->mat.shader.id = rlGetShaderIdDefault(); UnloadMaterial(st->mat); }
    free(st);
    env->state = NULL;
}

Environment env_greybox_create(void)
{
    Environment e;
    memset(&e, 0, sizeof(e));
    e.name = "Greybox";
    e.id = ENV_GREYBOX;
    e.state = calloc(1, sizeof(GreyboxState));
    e.pathStyle = gb_path_style;
    e.init = gb_init;
    e.registerParams = gb_register;
    e.reseed = gb_reseed;
    e.buildChunk = gb_build_chunk;
    e.uploadChunk = gb_upload;
    e.releaseChunk = gb_release;
    e.energyAt = gb_energy;
    e.update = gb_update;
    e.drawBackground = gb_draw_background;
    e.drawOpaque = gb_draw_opaque;
    e.drawEmissive = gb_draw_emissive;
    e.chunkLabel = gb_label;
    e.unload = gb_unload;
    return e;
}
