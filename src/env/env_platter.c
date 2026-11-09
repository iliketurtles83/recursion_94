// Platter (level 2 greybox): a giant floppy disk. The path is an Archimedean spiral
// (PathStyle.spiral*) from the rim toward the spindle, where the boss will sit. The
// floor is one fullscreen pass (platter.fs) that draws the sector map from polar
// coordinates around the spindle: 80 concentric tracks of 18 sectors, header / gap /
// data per sector, track skew, FAT12-like file runs from integer hashes, the boot and
// FAT area on the outer track, bad sectors, MFM flux lines and the index mark. Nothing
// is stored: every pixel is a function of (radius, angle, seed).
//
// Greybox scope: no chunk geometry; the spindle is a plain grey cylinder.
#include "env_common.h"
#include "core/rng.h"
#include "render/rmath.h"
#include "render/pipeline.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct PlatterParams {
    PathStyle path;
    int   tracks, sectors, fluxCells;
    float rimMargin, innerMargin;   // data band: spiralStart + rimMargin .. spiralEnd - innerMargin
    float skew;                     // sector-start shift per track (sectors)
    float hubRadius, spindleRadius, spindleHeight;
    float fileStart, fragChance, badChance;
    float headerI, dataI, indexI, ringI;
    float fogStart, fogEnd;
    bool  showFloor, showSpindle;
} PlatterParams;

typedef struct PlatterState {
    uint64_t      seed;
    ShaderLib    *sl;
    int           shFloor;
    PlatterParams p;
} PlatterState;

static const Color PL_FLOOR   = { 0x05, 0x07, 0x0B, 255 };
static const Color PL_HORIZON = { 0x06, 0x0E, 0x18, 255 };
static const Color PL_SYSTEM  = { 0xFF, 0x9E, 0x2E, 255 };
static const Color PL_BAD     = { 0xFF, 0x20, 0x1A, 255 };
static const Color PL_EMPTY   = { 0x22, 0x34, 0x48, 255 };
static const Color PL_HEADER  = { 0xF2, 0xF6, 0xFF, 255 };
static const Color PL_FILE[3] = { { 0x3C, 0xD8, 0xFF, 255 }, { 0x4C, 0xFF, 0x9C, 255 }, { 0xB4, 0x74, 0xFF, 255 } };

static PathStyle pl_path_style(const Environment *env)
{
    const PlatterState *st = (const PlatterState *)env->state;
    PathStyle s = st->p.path;
    s.radiusNoiseAmp = 0.0f;
    s.radiusNoiseScale = 100.0f;
    s.maxYawStepDeg = 0.0f;
    s.maxPitchDeg = 0.0f;
    s.pitchSpring = 0.0f;
    s.twistRateMax = 0.0f;
    s.yawSpring = 0.0f;
    s.hasWalls = false;
    if (s.spiralEnd > s.spiralStart - 1.0f) s.spiralEnd = s.spiralStart - 1.0f;
    return s;
}

static void pl_register(Environment *env, ParamRegistry *reg)
{
    PlatterState *st = (PlatterState *)env->state;
    PlatterParams *p = &st->p;
    const char *g = "Platter Path";
    params_float(reg, g, "plLaneHalfWidth", &p->path.baseRadius, 6.5f, 3.0f, 20.0f);
    params_float(reg, g, "plFloorDepth", &p->path.floorDepth, 2.2f, 0.8f, 20.0f);
    params_float(reg, g, "plCeilingHeight", &p->path.ceilingHeight, 14.0f, 0.0f, 40.0f);
    params_float(reg, g, "plSpiralStart", &p->path.spiralStart, 300.0f, 60.0f, 1200.0f);
    params_float(reg, g, "plSpiralEnd", &p->path.spiralEnd, 70.0f, 20.0f, 400.0f);
    params_float(reg, g, "plSpiralLaps", &p->path.spiralLaps, 5.0f, 0.5f, 20.0f);

    g = "Platter Disk";
    params_bool(reg, g, "plShowFloor", &p->showFloor, true);
    params_int(reg, g, "plTracks", &p->tracks, 80, 8, 240);
    params_int(reg, g, "plSectors", &p->sectors, 18, 4, 64);
    params_int(reg, g, "plFluxCells", &p->fluxCells, 192, 16, 1024);
    params_float(reg, g, "plRimMargin", &p->rimMargin, 30.0f, 0.0f, 200.0f);
    params_float(reg, g, "plInnerMargin", &p->innerMargin, 20.0f, 0.0f, 200.0f);
    params_float(reg, g, "plSkew", &p->skew, 0.37f, 0.0f, 1.0f);
    params_float(reg, g, "plFileStart", &p->fileStart, 0.22f, 0.02f, 1.0f);
    params_float(reg, g, "plFragChance", &p->fragChance, 0.07f, 0.0f, 0.5f);
    params_float(reg, g, "plBadChance", &p->badChance, 0.004f, 0.0f, 0.1f);
    params_float(reg, g, "plHeaderIntensity", &p->headerI, 1.6f, 0.0f, 10.0f);
    params_float(reg, g, "plDataIntensity", &p->dataI, 0.5f, 0.0f, 10.0f);
    params_float(reg, g, "plIndexIntensity", &p->indexI, 3.0f, 0.0f, 10.0f);
    params_float(reg, g, "plRingIntensity", &p->ringI, 2.0f, 0.0f, 10.0f);
    params_float(reg, g, "plFogStart", &p->fogStart, 60.0f, 0.0f, 1500.0f);
    params_float(reg, g, "plFogEnd", &p->fogEnd, 700.0f, 1.0f, 1500.0f);

    g = "Platter Hub";
    params_bool(reg, g, "plShowSpindle", &p->showSpindle, true);
    params_float(reg, g, "plHubRadius", &p->hubRadius, 30.0f, 0.0f, 200.0f);
    params_float(reg, g, "plSpindleRadius", &p->spindleRadius, 8.0f, 1.0f, 100.0f);
    params_float(reg, g, "plSpindleHeight", &p->spindleHeight, 60.0f, 1.0f, 400.0f);
}

static void pl_init(Environment *env, uint64_t seed, ShaderLib *sl)
{
    PlatterState *st = (PlatterState *)env->state;
    st->seed = seed;
    st->sl = sl;
    if (sl) st->shFloor = sl_load(sl, "fullscreen.vs", "platter.fs");
}

static void pl_reseed(Environment *env, uint64_t seed) { ((PlatterState *)env->state)->seed = seed; }

// No chunk geometry yet: the disk is drawn per pixel.
static void pl_build_chunk(Environment *env, const PathSystem *path, int chunkIndex, ChunkCpu *out) { }
static void pl_upload(Environment *env, ChunkCpu *in, ChunkGpu *out) { chunk_upload_default(in, out); }
static void pl_release(Environment *env, ChunkGpu *gpu) { chunk_release_default(gpu); }

// Energy rises as the spiral closes in on the hub.
static float pl_energy(const Environment *env, const PathSystem *path, double s)
{
    double len = path_spiral_length(&path->style);
    double f = len > 0.0 ? s / len : 0.0;
    f = f < 0.0 ? 0.0 : (f > 1.0 ? 1.0 : f);
    return (float)(0.25 + 0.6 * f);
}

static void pl_update(Environment *env, const FrameContext *ctx) { }
static void pl_draw_background(Environment *env, const FrameContext *ctx) { }

static void pl_draw_floor(PlatterState *st, const FrameContext *ctx)
{
    const PlatterParams *p = &st->p;
    const PathStyle *ps = &ctx->path->style;
    ShaderLib *sl = st->sl;
    int sh = st->shFloor;
    Camera3D cam = ctx->camera;
    Vector3 fwd = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
    Vector3 right = Vector3Normalize(Vector3CrossProduct(fwd, cam.up));
    Vector3 up = Vector3CrossProduct(right, fwd);
    DVec3 c = dv_sub(path_spiral_centre(ps), ctx->renderOrigin);
    float rOut = ps->spiralStart + p->rimMargin;
    float rIn = ps->spiralEnd - p->innerMargin;
    if (rIn < p->hubRadius + 1.0f) rIn = p->hubRadius + 1.0f;

    rlDrawRenderBatchActive();
    BeginShaderMode(sl_shader(sl, sh));
    env_set_common_uniforms(sl, sh, ctx);
    sl_f1(sl, sh, "uAspect", (float)ctx->width / (float)ctx->height);
    sl_v3(sl, sh, "uCamRight", right);
    sl_v3(sl, sh, "uCamUp", up);
    sl_v3(sl, sh, "uCamFwd", fwd);
    sl_f1(sl, sh, "uTanHalf", tanf(ctx->fovy * 0.5f * DEG2RAD));
    sl_f2(sl, sh, "uClip", ctx->zNear, ctx->zFar);
    sl_f1(sl, sh, "uFloorY", (float)(-(double)ps->floorDepth - ctx->renderOrigin.y));
    sl_f2(sl, sh, "uCentre", (float)c.x, (float)c.z);
    sl_f4(sl, sh, "uDisk", rOut, rIn, (float)p->tracks, (float)p->sectors);
    sl_f4(sl, sh, "uLayout", p->skew, (float)p->fluxCells, p->hubRadius, 0.0f);
    sl_f4(sl, sh, "uAlloc", p->fileStart, p->fragChance, p->badChance, 0.0f);
    sl_f4(sl, sh, "uLevels", p->headerI, p->dataI, p->indexI, p->ringI);
    sl_f2(sl, sh, "uFloorFog", p->fogStart, p->fogEnd);
    sl_i1(sl, sh, "uSeed", (int)(ct_hash64_2(st->seed, 0x504C4154u) & 0x7FFFFFFF));
    sl_v3(sl, sh, "uFloorColor", ct_srgb_to_linear(PL_FLOOR));
    sl_v3(sl, sh, "uHorizonColor", ct_srgb_to_linear(PL_HORIZON));
    sl_v3(sl, sh, "uSystemColor", ct_srgb_to_linear(PL_SYSTEM));
    sl_v3(sl, sh, "uBadColor", ct_srgb_to_linear(PL_BAD));
    sl_v3(sl, sh, "uEmptyColor", ct_srgb_to_linear(PL_EMPTY));
    sl_v3(sl, sh, "uHeaderColor", ct_srgb_to_linear(PL_HEADER));
    sl_v3(sl, sh, "uFileA", ct_srgb_to_linear(PL_FILE[0]));
    sl_v3(sl, sh, "uFileB", ct_srgb_to_linear(PL_FILE[1]));
    sl_v3(sl, sh, "uFileC", ct_srgb_to_linear(PL_FILE[2]));
    pipe_fullscreen_quad();
    EndShaderMode();
    ctx->stats->drawCalls++;
}

static void pl_draw_opaque(Environment *env, const FrameContext *ctx, const ChunkGpu *const *chunks, int n)
{
    PlatterState *st = (PlatterState *)env->state;
    if (ctx->reflectionPass) return;
    if (st->p.showFloor) pl_draw_floor(st, ctx);
    if (st->p.showSpindle) {
        const PathStyle *ps = &ctx->path->style;
        DVec3 c = dv_sub(path_spiral_centre(ps), ctx->renderOrigin);
        Vector3 base = { (float)c.x, (float)(-(double)ps->floorDepth - ctx->renderOrigin.y), (float)c.z };
        DrawCylinder(base, st->p.spindleRadius, st->p.spindleRadius, st->p.spindleHeight, 48, (Color){ 0x5A, 0x5E, 0x66, 255 });
        DrawCylinderWires(base, st->p.spindleRadius * 1.01f, st->p.spindleRadius * 1.01f, st->p.spindleHeight, 24, (Color){ 0xB0, 0xB4, 0xBC, 255 });
        rlDrawRenderBatchActive();
        ctx->stats->drawCalls++;
    }
}

static void pl_draw_emissive(Environment *env, const FrameContext *ctx, const ChunkGpu *const *chunks, int n) { }

static const char *pl_label(const Environment *env, int archetype) { return "PLATTER"; }

static void pl_unload(Environment *env)
{
    free(env->state);
    env->state = NULL;
}

Environment env_platter_create(void)
{
    Environment e;
    memset(&e, 0, sizeof(e));
    e.name = "Platter";
    e.id = ENV_PLATTER;
    e.state = calloc(1, sizeof(PlatterState));
    e.pathStyle = pl_path_style;
    e.init = pl_init;
    e.registerParams = pl_register;
    e.reseed = pl_reseed;
    e.buildChunk = pl_build_chunk;
    e.uploadChunk = pl_upload;
    e.releaseChunk = pl_release;
    e.energyAt = pl_energy;
    e.update = pl_update;
    e.drawBackground = pl_draw_background;
    e.drawOpaque = pl_draw_opaque;
    e.drawEmissive = pl_draw_emissive;
    e.chunkLabel = pl_label;
    e.unload = pl_unload;
    return e;
}
