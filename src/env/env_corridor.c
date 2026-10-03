// Environment #3: "Server Corridor" — low flight over the shared neon grid floor between
// walls of server pillars with green code rain and white stepped outlines, under a broken
// lattice of circuit-covered beams. See PLAN_ENV3_SERVER_CORRIDOR.md.
//
// Phases done: A (path style: level, no twist, floor and ceiling, strong yaw spring; grid
// floor, sky, reflection), B (pillar placement and stepped bodies, corridor_pillars.c),
// C (code rain, stepped outlines and floor kerbs), D (overhead beams, corridor_beams.c; sky lattice).
#include "env_common.h"
#include "core/rng.h"
#include "core/pacing.h"
#include "corridor_pillars.h"
#include "corridor_beams.h"
#include "render/gridfloor.h"
#include "render/rmath.h"
#include "render/pipeline.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { KB_BODY = 0, KB_RAIN, KB_OUTLINE, KB_KERB, KB_BEAM };   // chunk batches

#define PILLAR_BAND_LIMIT 150.0f   // largest corPillarBandMax (sets pathReach)
#define BEAM_BAND_LIMIT   120.0f   // largest corBeamBandMax

typedef struct CorridorParams {
    PathStyle path;
    GridFloorParams floor;
    PillarParams pillars;
    PillarDecorParams decor;
    bool  showPillars, showRain, showOutlines;
    float outlineReflect;
    BeamParams beams;
    bool  showBeams, showSkyGrid;
    float beamBrightness, traceIntensity, tracePitch, traceRuns, traceCross, pulseSpeed, pulseAmount;
    GridFloorParams skyGrid;
    float skyGridHeight, skyGridSparse;
    float rainPitch, rainEmpty, rainSpeedMin, rainSpeedMax, rainFill, rainHead, rainTrail, rainFlicker;
    float bodyBrightness, rimIntensity, underglow, panelW, panelH, seamDark, recessed;
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
    int            shBody, shRain, shTrace, shLines, shBeam;
    BeamDesc       beams[COR_MAX_BEAMS];
    PillarDesc     pillars[COR_MAX_PILLARS];   // build scratch (builds are single-threaded)
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
    params_float(reg, g, "corGridIntensity", &f->lineIntensity, 1.7f, 0.0f, 10.0f);
    params_float(reg, g, "corGridHalo", &f->haloIntensity, 0.2f, 0.0f, 3.0f);
    params_float(reg, g, "corGridNodeBoost", &f->nodeBoost, 1.0f, 0.0f, 10.0f);
    params_float(reg, g, "corGridWhiteCore", &f->whiteCore, 0.3f, 0.0f, 1.0f);
    params_float(reg, g, "corFloorFogStart", &f->fogStart, 30.0f, 0.0f, 1000.0f);
    params_float(reg, g, "corFloorFogEnd", &f->fogEnd, 330.0f, 1.0f, 1500.0f);
    params_float(reg, g, "corFloorSheen", &f->sheen, 0.25f, 0.0f, 3.0f);
    params_bool(reg, g, "corGridFilter", &f->filter, true);
    params_float(reg, g, "corGridNodeSize", &f->nodeSize, 0.12f, 0.0f, 1.0f);
    params_float(reg, g, "corFloorReflect", &f->reflectStrength, 1.0f, 0.0f, 3.0f);
    params_float(reg, g, "corFloorReflectR0", &f->reflectR0, 0.3f, 0.0f, 1.0f);
    f->lineColor = COL_CYAN;
    f->floorColor = COL_FLOOR;
    f->horizonColor = COL_HORIZON;

    g = "Pillars";
    PillarParams *pp = &p->pillars;
    params_bool(reg, g, "corShowPillars", &p->showPillars, true);
    params_int(reg, g, "corPillarLatticeCells", &pp->latticeCells, 6, 2, 10);
    params_float(reg, g, "corPillarClearance", &pp->clearance, 1.0f, 0.0f, 20.0f);
    params_float(reg, g, "corWallBand", &pp->wallBand, 12.0f, 0.0f, 40.0f);
    params_float(reg, g, "corPillarBandMax", &pp->bandMax, 90.0f, 20.0f, PILLAR_BAND_LIMIT);
    params_float(reg, g, "corWallDensity", &pp->wallDensity, 0.75f, 0.0f, 1.0f);
    params_float(reg, g, "corBackDensity", &pp->backDensity, 0.25f, 0.0f, 1.0f);
    params_float(reg, g, "corBackRamp", &pp->backRamp, 30.0f, 0.0f, 100.0f);
    params_float(reg, g, "corMergeChance", &pp->mergeChance, 0.3f, 0.0f, 1.0f);
    params_float(reg, g, "corPillarWidthMin", &pp->widthMin, 5.0f, 2.0f, 12.0f);
    params_float(reg, g, "corPillarWidthMax", &pp->widthMax, 9.0f, 2.0f, 12.0f);
    params_float(reg, g, "corPillarDepthMin", &pp->depthMin, 6.0f, 1.0f, 12.0f);
    params_float(reg, g, "corPillarDepthMax", &pp->depthMax, 10.0f, 1.0f, 12.0f);
    params_float(reg, g, "corPillarHeightMin", &pp->heightMin, 14.0f, 4.0f, 120.0f);
    params_float(reg, g, "corPillarHeightMax", &pp->heightMax, 20.0f, 4.0f, 160.0f);
    params_float(reg, g, "corBackHeightMul", &pp->backHeightMul, 1.0f, 0.2f, 4.0f);
    params_int(reg, g, "corStepsMin", &pp->stepsMin, 1, 0, 3);
    params_int(reg, g, "corStepsMax", &pp->stepsMax, 3, 0, 3);
    params_float(reg, g, "corStepMin", &pp->stepMin, 1.5f, 0.5f, 4.0f);
    params_float(reg, g, "corStepMax", &pp->stepMax, 3.0f, 0.5f, 6.0f);
    params_float(reg, g, "corWallSlack", &pp->slackMax, 1.5f, 0.0f, 8.0f);
    params_float(reg, g, "corOutlineChance", &pp->outlineChance, 0.5f, 0.0f, 1.0f);
    params_float(reg, g, "corPillarBrightness", &p->bodyBrightness, 2.6f, 0.0f, 8.0f);
    params_float(reg, g, "corPillarRim", &p->rimIntensity, 0.1f, 0.0f, 2.0f);
    params_float(reg, g, "corPillarUnderglow", &p->underglow, 0.08f, 0.0f, 2.0f);
    params_float(reg, g, "corPanelW", &p->panelW, 2.4f, 0.2f, 8.0f);
    params_float(reg, g, "corPanelH", &p->panelH, 1.8f, 0.2f, 16.0f);
    params_float(reg, g, "corSeamDark", &p->seamDark, 0.55f, 0.0f, 1.0f);
    params_float(reg, g, "corRecessed", &p->recessed, 0.2f, 0.0f, 1.0f);

    PillarDecorParams *dp = &p->decor;
    params_bool(reg, g, "corShowOutlines", &p->showOutlines, true);
    params_float(reg, g, "corOutlineWidth", &dp->outlineWidth, 0.2f, 0.01f, 1.0f);
    params_float(reg, g, "corOutlineIntensity", &dp->outlineIntensity, 4.0f, 0.0f, 20.0f);
    params_float(reg, g, "corOutlineOffset", &dp->outlineOffset, 0.1f, 0.0f, 1.0f);
    params_float(reg, g, "corOutlineTopMin", &dp->outlineTopMin, 0.3f, 0.0f, 1.0f);
    params_float(reg, g, "corOutlineReflect", &p->outlineReflect, 0.35f, 0.0f, 1.0f);
    params_float(reg, g, "corKerbMin", &dp->kerbMin, 2.0f, 0.0f, 20.0f);
    params_float(reg, g, "corKerbMax", &dp->kerbMax, 6.0f, 0.0f, 20.0f);
    params_float(reg, g, "corKerbWidth", &dp->kerbWidth, 0.18f, 0.01f, 1.0f);

    g = "CodeRain";
    params_bool(reg, g, "corShowRain", &p->showRain, true);
    params_float(reg, g, "corRainIntensity", &dp->rainIntensity, 1.6f, 0.0f, 10.0f);
    params_float(reg, g, "corRainBack", &dp->backRain, 0.35f, 0.0f, 2.0f);
    params_float(reg, g, "corRainLane", &dp->laneRain, 0.6f, 0.0f, 2.0f);
    params_float(reg, g, "corRainOffset", &dp->rainOffset, 0.05f, 0.005f, 0.5f);
    params_float(reg, g, "corRainPitch", &p->rainPitch, 0.45f, 0.1f, 2.0f);
    params_float(reg, g, "corRainEmpty", &p->rainEmpty, 0.35f, 0.0f, 1.0f);
    params_float(reg, g, "corRainSpeedMin", &p->rainSpeedMin, 1.0f, 0.0f, 10.0f);
    params_float(reg, g, "corRainSpeedMax", &p->rainSpeedMax, 4.0f, 0.0f, 10.0f);
    params_float(reg, g, "corRainFill", &p->rainFill, 0.7f, 0.0f, 1.0f);
    params_float(reg, g, "corRainHead", &p->rainHead, 1.0f, 0.0f, 8.0f);
    params_float(reg, g, "corRainTrail", &p->rainTrail, 3.0f, 0.1f, 20.0f);
    params_float(reg, g, "corRainFlicker", &p->rainFlicker, 0.03f, 0.0f, 1.0f);

    g = "Beams";
    BeamParams *bp = &p->beams;
    params_bool(reg, g, "corShowBeams", &p->showBeams, true);
    params_float(reg, g, "corBeamCell", &bp->cell, 16.0f, 8.0f, 40.0f);
    params_float(reg, g, "corBeamBandMax", &bp->bandMax, 70.0f, 10.0f, BEAM_BAND_LIMIT);
    params_float(reg, g, "corBeamChance", &bp->chance, 0.33f, 0.0f, 1.0f);
    params_float(reg, g, "corBeamGap", &bp->gapChance, 0.25f, 0.0f, 1.0f);
    params_float(reg, g, "corBeamWidthMin", &bp->widthMin, 6.0f, 1.0f, 14.0f);
    params_float(reg, g, "corBeamWidthMax", &bp->widthMax, 10.0f, 1.0f, 14.0f);
    params_float(reg, g, "corBeamThickMin", &bp->thickMin, 2.0f, 0.5f, 8.0f);
    params_float(reg, g, "corBeamThickMax", &bp->thickMax, 3.0f, 0.5f, 8.0f);
    params_float(reg, g, "corBeamHeight", &bp->heightMin, 24.0f, 10.0f, 60.0f);
    params_int(reg, g, "corBeamLevels", &bp->levels, 3, 1, 8);
    params_float(reg, g, "corBeamLaneKeep", &bp->laneKeep, 8.0f, 0.0f, 40.0f);
    params_float(reg, g, "corBeamBrightness", &p->beamBrightness, 2.2f, 0.0f, 8.0f);
    params_float(reg, g, "corBeamTraceIntensity", &p->traceIntensity, 2.4f, 0.0f, 10.0f);
    params_float(reg, g, "corBeamTracePitch", &p->tracePitch, 0.5f, 0.1f, 2.0f);
    params_float(reg, g, "corBeamTraceRuns", &p->traceRuns, 0.75f, 0.0f, 1.0f);
    params_float(reg, g, "corBeamTraceCross", &p->traceCross, 0.4f, 0.0f, 1.0f);
    params_float(reg, g, "corBeamPulseSpeed", &p->pulseSpeed, 6.0f, 0.0f, 40.0f);
    params_float(reg, g, "corBeamPulseAmount", &p->pulseAmount, 2.0f, 0.0f, 10.0f);
    params_bool(reg, g, "corShowSkyGrid", &p->showSkyGrid, true);
    params_float(reg, g, "corSkyGridHeight", &p->skyGridHeight, 90.0f, 30.0f, 400.0f);
    params_float(reg, g, "corSkyGridCell", &p->skyGrid.cell, 8.0f, 1.0f, 40.0f);
    params_float(reg, g, "corSkyGridSparse", &p->skyGridSparse, 0.3f, 0.0f, 1.0f);
    params_float(reg, g, "corSkyGridWidth", &p->skyGrid.lineWidth, 0.05f, 0.005f, 1.0f);
    params_float(reg, g, "corSkyGridIntensity", &p->skyGrid.lineIntensity, 1.0f, 0.0f, 5.0f);
    params_float(reg, g, "corSkyGridFadeStart", &p->skyGrid.fogStart, 100.0f, 0.0f, 1000.0f);
    params_float(reg, g, "corSkyGridFadeEnd", &p->skyGrid.fogEnd, 300.0f, 1.0f, 1500.0f);
    p->skyGrid.lineColor = (Color){ 0xDC, 0xF2, 0xFF, 255 };

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
        st->shBody = sl_load(sl, "tower.vs", "tower.fs");
        st->shRain = sl_load(sl, "glyph.vs", "coderain.fs");
        st->shTrace = sl_load(sl, "trace.vs", "outline.fs");
        st->shLines = sl_load(sl, "object.vs", "line_facing.fs");
        st->shBeam = sl_load(sl, "tower.vs", "beam.fs");
        st->mat = LoadMaterialDefault();
    }
}

static void cor_reseed(Environment *env, uint64_t seed) { ((CorridorState *)env->state)->seed = seed; }

// Pillar params that follow other settings (floor grid, lane, floor height).
static PillarParams pillar_params(const CorridorState *st, const PathSystem *path)
{
    PillarParams pp = st->p.pillars;
    pp.cell = st->p.floor.cell;
    pp.laneHalfWidth = path->style.baseRadius;
    pp.floorY = (float)(path_sample(path, 0.0).pos.y - path->style.floorDepth);   // level path: y is constant
    return pp;
}

static void cor_build_chunk(Environment *env, const PathSystem *path, int chunkIndex, ChunkCpu *out)
{
    CorridorState *st = (CorridorState *)env->state;
    out->archetype = 0;
    PillarParams pp = pillar_params(st, path);
    int n = pillars_plan(&pp, NULL, st->seed, path, chunkIndex, st->pillars, COR_MAX_PILLARS, NULL, 0, NULL);
    pillars_build(st->pillars, n, out->origin, &out->geo[KB_BODY]);
    PillarDecorParams dp = st->p.decor;
    dp.floorY = pp.floorY;
    pillars_build_decor(&dp, st->pillars, n, out->origin, &out->geo[KB_RAIN], &out->geo[KB_OUTLINE], &out->geo[KB_KERB]);
    BeamParams bp = st->p.beams;
    bp.floorY = pp.floorY;
    bp.density = 1.0f;
    int nb = beams_plan(&bp, st->seed, path, chunkIndex, st->beams, COR_MAX_BEAMS, NULL, 0, NULL);
    beams_build(st->beams, nb, out->origin, &out->geo[KB_BEAM]);
}
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

static void draw_batch(CorridorState *st, const FrameContext *ctx, int sh, const ChunkGpu *const *chunks, int n, int batch)
{
    st->mat.shader = sl_shader(st->sl, sh);
    for (int i = 0; i < n; i++) {
        const ChunkGpu *c = chunks[i];
        Matrix xf = env_chunk_transform(c, ctx->renderOrigin);
        for (int m = 0; m < c->meshCount; m++) {
            if (c->meshBatch[m] != batch) continue;
            if (batch != KB_BODY) sl_f1(st->sl, sh, "uChunkFade", env_chunk_fade(c, ctx->time));
            DrawMesh(c->meshes[m], st->mat, xf);
            ctx->stats->drawCalls++;
            ctx->stats->vertices += c->meshes[m].vertexCount;
        }
    }
}

static void draw_pillars(CorridorState *st, const FrameContext *ctx, const ChunkGpu *const *chunks, int n)
{
    const CorridorParams *p = &st->p;
    ShaderLib *sl = st->sl;
    int sh = st->shBody;
    env_set_common_uniforms(sl, sh, ctx);
    Vector3 body = ct_srgb_to_linear((Color){ 0x1A, 0x22, 0x2C, 255 });
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
    draw_batch(st, ctx, sh, chunks, n, KB_BODY);
}

static void draw_beams(CorridorState *st, const FrameContext *ctx, const ChunkGpu *const *chunks, int n)
{
    const CorridorParams *p = &st->p;
    ShaderLib *sl = st->sl;
    int sh = st->shBeam;
    env_set_common_uniforms(sl, sh, ctx);
    sl_v3(sl, sh, "uBody", Vector3Scale(ct_srgb_to_linear((Color){ 0x10, 0x16, 0x20, 255 }), p->beamBrightness));
    sl_v3(sl, sh, "uTraceColor", ct_srgb_to_linear(COL_CYAN));
    sl_f4(sl, sh, "uTrace", p->tracePitch, p->traceIntensity, p->traceRuns, p->traceCross);
    sl_f2(sl, sh, "uPulse", p->pulseSpeed, p->pulseAmount);
    sl_f2(sl, sh, "uFogRange", p->fogStart, p->fogEnd);
    sl_v3(sl, sh, "uZenith", ct_srgb_to_linear(COL_ZENITH));
    sl_v3(sl, sh, "uHorizon", ct_srgb_to_linear(COL_HORIZON));
    sl_f2(sl, sh, "uHaze", p->hazeWidth, p->hazeGlow);
    draw_batch(st, ctx, sh, chunks, n, KB_BEAM);
}

static void cor_draw_opaque(Environment *env, const FrameContext *ctx, const ChunkGpu *const *chunks, int n)
{
    CorridorState *st = (CorridorState *)env->state;
    if (st->p.showPillars) draw_pillars(st, ctx, chunks, n);
    if (st->p.showBeams) draw_beams(st, ctx, chunks, n);
    if (!st->p.showFloor || ctx->reflectionPass) return;
    GridFloorParams fp = st->p.floor;
    fp.horizonGlow = st->p.hazeGlow;
    double floorY = path_sample(ctx->path, ctx->sShip).pos.y - ctx->path->style.floorDepth;
    gridfloor_draw(&st->floor, &fp, ctx, floorY);
}

static const Color COL_GREEN = { 0x39, 0xFF, 0x6A, 255 };
static const Color COL_MINT  = { 0x9C, 0xFF, 0xC4, 255 };

static void cor_draw_emissive(Environment *env, const FrameContext *ctx, const ChunkGpu *const *chunks, int n)
{
    CorridorState *st = (CorridorState *)env->state;
    const CorridorParams *p = &st->p;
    ShaderLib *sl = st->sl;
    if (p->showSkyGrid && !ctx->reflectionPass) {
        double floorY = path_sample(ctx->path, ctx->sShip).pos.y - ctx->path->style.floorDepth;
        gridfloor_draw_ceiling(&st->floor, &p->skyGrid, ctx, floorY + p->skyGridHeight, p->skyGridSparse);
    }
    if (p->showPillars && p->showRain) {
        int sh = st->shRain;
        env_set_common_uniforms(sl, sh, ctx);
        sl_v3(sl, sh, "uGreen", ct_srgb_to_linear(COL_GREEN));
        sl_v3(sl, sh, "uMint", ct_srgb_to_linear(COL_MINT));
        sl_v3(sl, sh, "uWhite", (Vector3){ 1.0f, 1.0f, 1.0f });
        sl_f4(sl, sh, "uRain", p->rainPitch, p->rainEmpty, p->rainSpeedMin, p->rainSpeedMax);
        sl_f4(sl, sh, "uRain2", p->rainFill, p->rainHead, p->rainTrail, p->rainFlicker);
        draw_batch(st, ctx, sh, chunks, n, KB_RAIN);
    }
    if (p->showPillars && p->showOutlines) {
        int t = st->shTrace;
        env_set_common_uniforms(sl, t, ctx);
        sl_f1(sl, t, "uGain", ctx->reflectionPass ? p->outlineReflect : 1.0f);
        draw_batch(st, ctx, t, chunks, n, KB_OUTLINE);
        int l = st->shLines;
        env_set_common_uniforms(sl, l, ctx);
        sl_f1(sl, l, "uEdgeMix", 0.0f);
        sl_f1(sl, l, "uFlicker", 0.0f);
        draw_batch(st, ctx, l, chunks, n, KB_KERB);
    }
}

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
    e.pathReach = 2.2 * (PILLAR_BAND_LIMIT + 10.0 * 4.0) + 4.0;   // pillars_reach() at the largest band and lattice (> beams_reach())
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
