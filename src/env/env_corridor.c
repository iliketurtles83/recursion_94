// Environment #3: "Server Corridor" — low flight over the shared neon grid floor between
// walls of server pillars with green code rain and white stepped outlines, under a broken
// lattice of circuit-covered beams. See PLAN_ENV3_SERVER_CORRIDOR.md.
//
// Phases done: A (path style: level, no twist, floor and ceiling, strong yaw spring; grid
// floor, sky, reflection), B (pillar placement and stepped bodies, corridor_pillars.c),
// C (code rain, stepped outlines and floor kerbs), D (overhead beams, corridor_beams.c; sky
// lattice), E (holo rings, light rain, props), F (pacing archetypes COLONNADE / SERVER HALL / ATRIUM).
#include "env_common.h"
#include "core/rng.h"
#include "core/pacing.h"
#include "corridor_pillars.h"
#include "corridor_beams.h"
#include "render/gridfloor.h"
#include "render/shapes.h"
#include "render/streaks.h"
#include "render/rmath.h"
#include "render/pipeline.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { KB_BODY = 0, KB_RAIN, KB_OUTLINE, KB_KERB, KB_BEAM, KB_RING, KB_PROP_EDGES, KB_PROP_FACES };

// Per-chunk uniforms: ring cluster bases (ring.vs) and props (object.vs / face.vs).
enum { U_RING_A = 0, U_RING_B = 16, U_RING_C = 32, U_OBJ_A = 48, U_OBJ_B = 64, U_OBJ_C = 80 };
#define MAX_CLUSTERS 16
#define MAX_PROPS    8
#define RAIN_COUNT   600

enum { F_RINGS = 4, F_PROPS = 5, F_PACING = 6 };   // corridor feature sub-streams (pillars 1, decor 2, beams 3)

// Pacing archetypes: a hash-driven Markov chain over runs of chunks (core/pacing.c).
typedef struct CorArch {
    PillarArch pillar;       // wall/back density, clearance, height, outline, merge multipliers
    float beams;             // x beam line chance
    float ringTop, ringLane; // x ring chances
    float ringSize;          // x ring radius
    float props;             // x propsMax
    float energy;            // pacing energy 0..1
} CorArch;

enum { CA_COLONNADE, CA_HALL, CA_ATRIUM, CA_COUNT, CA_DEFAULT = CA_COUNT };
static const char *CA_NAMES[CA_COUNT + 1] = { "COLONNADE", "SERVER HALL", "ATRIUM", "DEFAULT" };
//                       wall  back  clear  height outl  merge   beams  rTop  rLane rSize props energy
static const CorArch CA_TABLE[CA_COUNT + 1] = {
    [CA_COLONNADE] = { { 1.00f, 1.0f,  0.0f, 1.00f, 1.0f, 0.7f }, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.50f },   // the reference framing
    [CA_HALL]      = { { 1.30f, 1.6f,  0.0f, 1.00f, 1.6f, 2.0f }, 1.9f, 0.6f, 0.5f, 1.0f, 0.5f, 0.85f },   // dense walls, many outlines, dense beams
    [CA_ATRIUM]    = { { 0.45f, 0.6f, 10.0f, 0.85f, 0.5f, 0.5f }, 0.0f, 3.0f, 1.0f, 1.4f, 2.0f, 0.30f },   // walls pushed back, open sky, big rings
    [CA_DEFAULT]   = { { 1.00f, 1.0f,  0.0f, 1.00f, 1.0f, 1.0f }, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.50f },
};
static const int CA_RUN_MIN[CA_COUNT] = { 4, 3, 2 };
static const int CA_RUN_MAX[CA_COUNT] = { 9, 6, 4 };
static const float CA_TRANSITION[CA_COUNT][CA_COUNT] = {
    { 0.00f, 0.55f, 0.45f },
    { 0.65f, 0.00f, 0.35f },
    { 0.70f, 0.30f, 0.00f },
};
static const PacingTable CA_PACING = { CA_COUNT, CA_COLONNADE, CA_RUN_MIN, CA_RUN_MAX, &CA_TRANSITION[0][0] };
#define ARCH_CACHE 24   // chunks around the one being built (cells reach ~5 chunks away)   // chunk batches

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
    bool  showRings;
    float ringTopChance, ringLaneChance, ringRadiusMin, ringRadiusMax, ringIntensity, ringWidth, ringSpin, ringTilt;
    StreakParams rain;
    float rainYMin, rainYMax;
    bool  showStreaks;
    bool  showProps;
    int   propsMax;
    float propSizeMin, propSizeMax, propHeightMin, propHeightMax, propEdgeIntensity, propFaceIntensity;
    bool  pacing;
    float rainPitch, rainEmpty, rainSpeedMin, rainSpeedMax, rainResidual, rainHead, rainTrail, rainGlyphRate;
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
    int            shBody, shRain, shTrace, shLines, shBeam, shRing, shFace;
    StreakSystem   streaks;
    PillarDesc     near[2][COR_MAX_PILLARS];   // neighbouring chunks' pillars (prop keep-out)
    int            archK0, archCache[ARCH_CACHE];   // archetypes of chunks archK0.. (build scratch)
    bool           archValid;
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
    params_float(reg, g, "corRainIntensity", &dp->rainIntensity, 2.2f, 0.0f, 10.0f);
    params_float(reg, g, "corRainBack", &dp->backRain, 0.35f, 0.0f, 2.0f);
    params_float(reg, g, "corRainLane", &dp->laneRain, 0.6f, 0.0f, 2.0f);
    params_float(reg, g, "corRainOffset", &dp->rainOffset, 0.05f, 0.005f, 0.5f);
    params_float(reg, g, "corRainCharWidth", &p->rainPitch, 0.5f, 0.1f, 2.0f);
    params_float(reg, g, "corRainEmpty", &p->rainEmpty, 0.35f, 0.0f, 1.0f);
    params_float(reg, g, "corRainSpeedMin", &p->rainSpeedMin, 2.0f, 0.0f, 10.0f);
    params_float(reg, g, "corRainSpeedMax", &p->rainSpeedMax, 6.0f, 0.0f, 10.0f);
    params_float(reg, g, "corRainResidual", &p->rainResidual, 0.1f, 0.0f, 1.0f);
    params_float(reg, g, "corRainHead", &p->rainHead, 0.8f, 0.0f, 8.0f);
    params_float(reg, g, "corRainTrail", &p->rainTrail, 6.0f, 0.1f, 20.0f);
    params_float(reg, g, "corRainGlyphRate", &p->rainGlyphRate, 1.0f, 0.0f, 10.0f);

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

    g = "Rings";
    params_bool(reg, g, "corShowRings", &p->showRings, true);
    params_float(reg, g, "corRingTopChance", &p->ringTopChance, 0.12f, 0.0f, 1.0f);
    params_float(reg, g, "corRingLaneChance", &p->ringLaneChance, 0.35f, 0.0f, 1.0f);
    params_float(reg, g, "corRingRadiusMin", &p->ringRadiusMin, 2.0f, 0.5f, 8.0f);
    params_float(reg, g, "corRingRadiusMax", &p->ringRadiusMax, 4.5f, 0.5f, 8.0f);
    params_float(reg, g, "corRingIntensity", &p->ringIntensity, 3.0f, 0.0f, 10.0f);
    params_float(reg, g, "corRingWidth", &p->ringWidth, 0.08f, 0.005f, 0.5f);
    params_float(reg, g, "corRingSpin", &p->ringSpin, 0.35f, 0.0f, 3.0f);
    params_float(reg, g, "corRingTilt", &p->ringTilt, 6.0f, 0.0f, 30.0f);

    g = "Rain";
    StreakParams *rn = &p->rain;
    params_bool(reg, g, "corShowStreaks", &p->showStreaks, true);
    params_float(reg, g, "corStreakDensity", &rn->density, 0.4f, 0.0f, 1.0f);
    params_float(reg, g, "corStreakLenMul", &rn->lenMul, 2.4f, 0.1f, 10.0f);
    params_float(reg, g, "corStreakWMin", &rn->wMin, 0.04f, 0.005f, 0.5f);
    params_float(reg, g, "corStreakWMax", &rn->wMax, 0.12f, 0.005f, 0.5f);
    params_float(reg, g, "corStreakIntensity", &rn->intensity, 2.4f, 0.0f, 10.0f);
    params_float(reg, g, "corStreakCometFrac", &rn->cometFrac, 0.05f, 0.0f, 0.5f);
    params_float(reg, g, "corStreakZRange", &rn->zRange, 150.0f, 40.0f, 400.0f);
    params_float(reg, g, "corStreakHalfWidth", &rn->slabHalfWidth, 30.0f, 5.0f, 200.0f);
    params_float(reg, g, "corStreakYMin", &p->rainYMin, 9.0f, 0.0f, 50.0f);
    params_float(reg, g, "corStreakYMax", &p->rainYMax, 26.0f, 0.0f, 100.0f);
    params_float(reg, g, "corStreakFallTilt", &rn->fallTilt, 0.12f, 0.0f, 1.0f);
    params_float(reg, g, "corStreakCenterGap", &rn->slabGap, 9.0f, 0.0f, 100.0f);
    params_float(reg, g, "corStreakCenterFrac", &rn->slabGapFrac, 0.04f, 0.0f, 1.0f);
    params_float(reg, g, "corStreakSpeedMul", &rn->speedMul, 1.0f, 0.0f, 4.0f);
    rn->zBehind = 12.0f;
    rn->volume = 1;
    rn->colMain = (Color){ 0xD8, 0xF6, 0xFF, 255 };
    rn->colAccent = COL_CYAN;

    g = "Props";
    params_bool(reg, g, "corShowProps", &p->showProps, true);
    params_int(reg, g, "corPropsMax", &p->propsMax, 2, 0, MAX_PROPS);
    params_float(reg, g, "corPropSizeMin", &p->propSizeMin, 0.5f, 0.1f, 4.0f);
    params_float(reg, g, "corPropSizeMax", &p->propSizeMax, 1.3f, 0.1f, 4.0f);
    params_float(reg, g, "corPropHeightMin", &p->propHeightMin, 1.0f, 0.4f, 30.0f);
    params_float(reg, g, "corPropHeightMax", &p->propHeightMax, 7.0f, 0.5f, 30.0f);
    params_float(reg, g, "corPropEdgeIntensity", &p->propEdgeIntensity, 2.4f, 0.0f, 10.0f);
    params_float(reg, g, "corPropFaceIntensity", &p->propFaceIntensity, 0.2f, 0.0f, 3.0f);

    g = "Pacing";
    params_bool(reg, g, "corPacing", &p->pacing, true);

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
        st->shRing = sl_load(sl, "ring.vs", "ring.fs");
        st->shFace = sl_load(sl, "face.vs", "face.fs");
        st->mat = LoadMaterialDefault();
    }
    streaks_init(&st->streaks, sl, RAIN_COUNT);
}

static void cor_reseed(Environment *env, uint64_t seed)
{
    CorridorState *st = (CorridorState *)env->state;
    st->seed = seed;
    st->archValid = false;
}

static uint64_t pacing_base(const CorridorState *st) { return ct_hash64_3(st->seed, ENV_CORRIDOR, F_PACING); }

static int archetype_of_chunk(const CorridorState *st, int k)
{
    if (!st->p.pacing) return CA_DEFAULT;
    if (k < 0) k = 0;
    if (st->archValid && k >= st->archK0 && k < st->archK0 + ARCH_CACHE) return st->archCache[k - st->archK0];
    return pacing_archetype(&CA_PACING, pacing_base(st), k);   // every flight starts in a colonnade
}

// Blended archetype at s: the previous chunk's archetype fades into this one across the chunk.
// Depends only on (seed, s), never on the chunk being built.
static CorArch arch_at(const CorridorState *st, double s)
{
    int k = chunk_index_at(s);
    int a = archetype_of_chunk(st, k - 1), b = archetype_of_chunk(st, k);
    if (a == b) return CA_TABLE[b];
    CorArch r;
    pacing_lerp(&r.pillar.wallDensity, &CA_TABLE[a].pillar.wallDensity, &CA_TABLE[b].pillar.wallDensity,
                (int)(sizeof(CorArch) / sizeof(float)), pacing_blend_t(s, k * CHUNK_LEN, CHUNK_LEN));
    return r;
}

static PillarArch pillar_arch_cb(const void *user, double s) { return arch_at((const CorridorState *)user, s).pillar; }
static float beam_density_cb(const void *user, double s) { return arch_at((const CorridorState *)user, s).beams; }

static void cache_archetypes(CorridorState *st, int k)
{
    st->archValid = false;
    if (!st->p.pacing) return;
    st->archK0 = k - ARCH_CACHE / 2;
    pacing_archetypes(&CA_PACING, pacing_base(st), st->archK0, ARCH_CACHE, st->archCache);
    st->archValid = true;
}

// Pillar params that follow other settings (floor grid, lane, floor height).
static PillarParams pillar_params(const CorridorState *st, const PathSystem *path)
{
    PillarParams pp = st->p.pillars;
    pp.cell = st->p.floor.cell;
    pp.laneHalfWidth = path->style.baseRadius;
    pp.floorY = (float)(path_sample(path, 0.0).pos.y - path->style.floorDepth);   // level path: y is constant
    return pp;
}

static const Color COL_RING = { 0x7C, 0xEE, 0xFF, 255 };
static const Color COL_PALE = { 0xE6, 0xF8, 0xFF, 255 };

static void set_u(ChunkCpu *c, int slot, Vector3 v, float w)
{
    c->uniforms[slot][0] = v.x; c->uniforms[slot][1] = v.y; c->uniforms[slot][2] = v.z; c->uniforms[slot][3] = w;
}

// A cluster of 2-3 horizontal (slightly tilted) holo rings sharing one basis slot: concentric
// or stacked, each split into 1-3 arcs with gaps, spinning at its own rate.
static void ring_cluster(const CorridorParams *p, ChunkCpu *c, int slot, DVec3 centre, float radius, Pcg32 *r)
{
    float tilt = pcg32_range(r, -p->ringTilt, p->ringTilt) * DEG2RAD, yaw = pcg32_float(r) * 6.2831853f;
    Vector3 right = { cosf(yaw), 0.0f, sinf(yaw) };
    Vector3 flat = { -sinf(yaw), 0.0f, cosf(yaw) };          // horizontal, perpendicular to right
    Vector3 up = Vector3Add(Vector3Scale(flat, cosf(tilt)), (Vector3){ 0.0f, sinf(tilt), 0.0f });
    set_u(c, U_RING_A + slot, env_local(centre, c->origin), 0.0f);
    set_u(c, U_RING_B + slot, right, 0.0f);
    set_u(c, U_RING_C + slot, up, 0.0f);
    int count = pcg32_int(r, 2, 3);
    int stacked = pcg32_chance(r, 0.5f);
    for (int k = 0; k < count; k++) {
        float rad = stacked ? radius * pcg32_range(r, 0.9f, 1.05f) : radius * (1.0f - 0.22f * (float)k);
        float z = stacked ? (float)k * pcg32_range(r, 0.5f, 0.9f) : 0.0f;
        float spin = p->ringSpin * pcg32_range(r, 0.5f, 1.5f) * (pcg32_chance(r, 0.5f) ? 1.0f : -1.0f);
        LineAttr a = { COL_RING, p->ringIntensity * pcg32_range(r, 0.7f, 1.1f), pcg32_float(r), pcg32_chance(r, 0.5f) ? 0.15f : 0.0f, (float)slot, spin };
        int arcs = pcg32_int(r, 1, 3);
        float start = pcg32_float(r) * 6.2831853f, span = 6.2831853f / (float)arcs;
        for (int i = 0; i < arcs; i++) {
            float gap = arcs == 1 ? 0.0f : pcg32_range(r, 0.15f, 0.6f);
            float a0 = start + span * (float)i, a1 = a0 + span - gap;
            lg_planar_arc(&c->geo[KB_RING], rad, a0, a1, z, p->ringWidth, 0.08f, a);
        }
    }
}

static void build_rings(const CorridorState *st, const PathSystem *path, const PillarDesc *pl, int n, const CorArch *ca, ChunkCpu *c)
{
    const CorridorParams *p = &st->p;
    Pcg32 r = pcg32_seed(ct_hash64_3(st->seed, ((uint64_t)ENV_CORRIDOR << 8) | F_RINGS, (uint64_t)(int64_t)c->index));
    int slot = 0;
    // Above the lane, higher than the ship can fly (keeps the play band clear): 0-1 per chunk.
    float fd = path->style.floorDepth, ceil = path->style.ceilingHeight;
    float beamY = p->beams.heightMin - 1.5f;   // underside of the lowest beam level, above the floor
    if (pcg32_chance(&r, p->ringLaneChance * ca->ringLane)) {
        double s = c->s0 + pcg32_range(&r, 0.2f, 0.8f) * CHUNK_LEN;
        PathSample ps = path_sample(path, s);
        float rad = pcg32_range(&r, p->ringRadiusMin, p->ringRadiusMax) * ca->ringSize;
        float y = pcg32_range(&r, ceil + fd + 2.0f, fmaxf(beamY - 1.5f, ceil + fd + 2.0f));
        DVec3 ctr = dv_add(ps.pos, dv_add(dv_scale(ps.B, pcg32_range(&r, -2.0f, 2.0f)), dv_scale(ps.N, y - fd)));
        ring_cluster(p, c, slot++, ctr, rad, &r);
    }
    // On top of wall pillars (owned with the pillar, so this chunk builds them).
    for (int i = 0; i < n && slot < MAX_CLUSTERS; i++) {
        const PillarDesc *d = &pl[i];
        Pcg32 pr = pcg32_seed(ct_hash64_2(d->hash, F_RINGS));
        if (!d->wall || !pcg32_chance(&pr, p->ringTopChance * ca->ringTop)) continue;
        int t = d->tiers - 1;
        double cl = d->cLane[t], depth = fabs(d->cOut - cl);
        float rad = fminf(pcg32_range(&pr, p->ringRadiusMin, p->ringRadiusMax) * ca->ringSize, (float)(depth * 0.5 + 0.9));   // edge stays outside the lane
        DVec3 ctr = pillar_point(d, (d->a0 + d->a1) * 0.5, (cl + d->cOut) * 0.5, d->yTop[t] + pcg32_range(&pr, 0.8f, 2.0f));
        ring_cluster(p, c, slot++, ctr, rad, &pr);
    }
}

static int prop_hits(const PillarDesc *pl, int n, DVec3 ctr, double bound)
{
    for (int i = 0; i < n; i++) {
        double x0, z0, x1, z1;
        pillar_bounds(&pl[i], &x0, &z0, &x1, &z1);
        if (ctr.x > x0 - bound && ctr.x < x1 + bound && ctr.z > z0 - bound && ctr.z < z1 + bound) return 1;
    }
    return 0;
}

// Small slowly turning wireframe cubes low beside the lane, in the gaps between pillars. Checked
// against this chunk's and both neighbours' pillars (a pillar can reach across a chunk boundary).
static void build_props(CorridorState *st, const PathSystem *path, const PillarParams *pp, const PillarArchFn *af,
                        const PillarDesc *own, int nOwn, int propsMax, ChunkCpu *c)
{
    const CorridorParams *p = &st->p;
    if (propsMax <= 0) return;
    Pcg32 r = pcg32_seed(ct_hash64_3(st->seed, ((uint64_t)ENV_CORRIDOR << 8) | F_PROPS, (uint64_t)(int64_t)c->index));
    int count = pcg32_int(&r, 0, propsMax > MAX_PROPS ? MAX_PROPS : propsMax);
    if (count == 0) return;
    int nn[2] = { -1, -1 };   // neighbours' pillars, planned only when a prop comes near that end
    float lane = path->style.baseRadius;
    for (int g = 0; g < count; g++) {
        double s = c->s0 + pcg32_float(&r) * CHUNK_LEN;
        PathSample ps = path_sample(path, s);
        float size = pcg32_range(&r, p->propSizeMin, p->propSizeMax);
        float h = size * 0.5f, bound = h * 1.7320508f;
        float side = pcg32_chance(&r, 0.5f) ? 1.0f : -1.0f;
        float lat = pcg32_range(&r, lane + 0.5f + bound, lane + 14.0f);
        float y = pcg32_range(&r, p->propHeightMin + bound, fmaxf(p->propHeightMax, p->propHeightMin + bound)) - path->style.floorDepth;
        DVec3 ctr = dv_add(ps.pos, dv_add(dv_scale(ps.B, side * lat), dv_scale(ps.N, y)));
        Vector3 axis = Vector3Normalize((Vector3){ pcg32_range(&r, -1, 1), pcg32_range(&r, 0.3f, 1), pcg32_range(&r, -1, 1) });
        float spin = pcg32_range(&r, 0.15f, 0.6f) * (pcg32_chance(&r, 0.5f) ? 1.0f : -1.0f), phase = pcg32_float(&r) * 6.2831853f;
        float he = pcg32_float(&r), hf = pcg32_float(&r);
        int hit = prop_hits(own, nOwn, ctr, bound + 0.3);
        for (int j = 0; j < 2 && !hit; j++) {
            if ((j ? c->s1 - s : s - c->s0) > 24.0) continue;   // a neighbour's pillar reaches < 24 into this chunk
            if (nn[j] < 0) nn[j] = pillars_plan(pp, af, st->seed, path, c->index + (j ? 1 : -1), st->near[j], COR_MAX_PILLARS, NULL, 0, NULL);
            hit = prop_hits(st->near[j], nn[j], ctr, bound + 0.3);
        }
        if (hit) continue;
        int slot = c->debrisCount < MAX_PROPS ? c->debrisCount : MAX_PROPS - 1;
        set_u(c, U_OBJ_A + slot, env_local(ctr, c->origin), size);
        set_u(c, U_OBJ_B + slot, axis, spin);
        set_u(c, U_OBJ_C + slot, (Vector3){ phase, 0, 0 }, 0.0f);
        LineAttr e = { COL_PALE, p->propEdgeIntensity, he, 0.0f, (float)slot, 0.0f };
        LineAttr f = { COL_CYAN, p->propFaceIntensity, hf, 0.0f, (float)slot, 0.0f };
        shape_cube(&c->geo[KB_PROP_EDGES], &c->geo[KB_PROP_FACES], h, e, f, 0.035f);
        if (c->debrisCount < CHUNK_MAX_DEBRIS) {
            DebrisInfo *d = &c->debris[c->debrisCount++];
            d->center = ctr; d->radius = bound; d->s = s;
        }
    }
}

static void cor_build_chunk(Environment *env, const PathSystem *path, int chunkIndex, ChunkCpu *out)
{
    CorridorState *st = (CorridorState *)env->state;
    cache_archetypes(st, chunkIndex);
    out->archetype = archetype_of_chunk(st, chunkIndex);
    PillarParams pp = pillar_params(st, path);
    PillarArchFn af = { pillar_arch_cb, st };
    int n = pillars_plan(&pp, &af, st->seed, path, chunkIndex, st->pillars, COR_MAX_PILLARS, NULL, 0, NULL);
    pillars_build(st->pillars, n, out->origin, &out->geo[KB_BODY]);
    PillarDecorParams dp = st->p.decor;
    dp.floorY = pp.floorY;
    pillars_build_decor(&dp, st->pillars, n, out->origin, &out->geo[KB_RAIN], &out->geo[KB_OUTLINE], &out->geo[KB_KERB]);
    BeamParams bp = st->p.beams;
    bp.floorY = pp.floorY;
    bp.densityAt = beam_density_cb;
    bp.densityUser = st;
    int nb = beams_plan(&bp, st->seed, path, chunkIndex, st->beams, COR_MAX_BEAMS, NULL, 0, NULL);
    beams_build(st->beams, nb, out->origin, &out->geo[KB_BEAM]);
    CorArch ca = arch_at(st, out->s0 + CHUNK_LEN * 0.5);
    build_rings(st, path, st->pillars, n, &ca, out);
    build_props(st, path, &pp, &af, st->pillars, n, (int)lroundf((float)st->p.propsMax * ca.props), out);
}
static void cor_upload(Environment *env, ChunkCpu *in, ChunkGpu *out) { chunk_upload_default(in, out); }
static void cor_release(Environment *env, ChunkGpu *gpu) { chunk_release_default(gpu); }
static float cor_energy(const Environment *env, const PathSystem *path, double s)
{
    return arch_at((const CorridorState *)env->state, s).energy;
}

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
    sl_f1(sl, sh, "uSkyline", 0.0f);   // shared program: the city's game look may have left it on
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
            if (batch != KB_BODY && batch != KB_BEAM) sl_f1(st->sl, sh, "uChunkFade", env_chunk_fade(c, ctx->time));
            if (batch == KB_RING) {
                sl_vec4v(st->sl, sh, "uRingA", c->uniforms[U_RING_A], MAX_CLUSTERS);
                sl_vec4v(st->sl, sh, "uRingB", c->uniforms[U_RING_B], MAX_CLUSTERS);
                sl_vec4v(st->sl, sh, "uRingC", c->uniforms[U_RING_C], MAX_CLUSTERS);
            } else if (batch == KB_PROP_EDGES || batch == KB_PROP_FACES) {
                sl_vec4v(st->sl, sh, "uObjA", c->uniforms[U_OBJ_A], 16);
                sl_vec4v(st->sl, sh, "uObjB", c->uniforms[U_OBJ_B], 16);
                sl_vec4v(st->sl, sh, "uObjC", c->uniforms[U_OBJ_C], 16);
            }
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
    sl_f1(sl, sh, "uWindows", 0.0f);   // shared program: the city's game look may have left it on
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
        sl_f4(sl, sh, "uRain2", p->rainResidual, p->rainHead, p->rainTrail, p->rainGlyphRate);
        draw_batch(st, ctx, sh, chunks, n, KB_RAIN);
    }
    if (p->showPillars && p->showOutlines) {
        int t = st->shTrace;
        env_set_common_uniforms(sl, t, ctx);
        sl_f1(sl, t, "uGain", ctx->reflectionPass ? p->outlineReflect : 1.0f);
        draw_batch(st, ctx, t, chunks, n, KB_OUTLINE);
    }
    // The blurred reflection cannot show the thin kerbs, rings or faint prop faces: skip them
    // there (keeps the draw-call budget).
    bool refl = ctx->reflectionPass;
    int l = st->shLines;
    env_set_common_uniforms(sl, l, ctx);
    sl_f1(sl, l, "uEdgeMix", 0.0f);
    sl_f1(sl, l, "uFlicker", 0.0f);
    if (p->showPillars && p->showOutlines && !refl) draw_batch(st, ctx, l, chunks, n, KB_KERB);
    if (p->showProps) {
        sl_f1(sl, l, "uFlicker", 0.25f);
        draw_batch(st, ctx, l, chunks, n, KB_PROP_EDGES);
        if (!refl) {
            env_set_common_uniforms(sl, st->shFace, ctx);
            sl_f1(sl, st->shFace, "uDataRate", 1.2f);
            draw_batch(st, ctx, st->shFace, chunks, n, KB_PROP_FACES);
        }
    }
    if (p->showRings && !refl) {
        int rs = st->shRing;
        env_set_common_uniforms(sl, rs, ctx);
        sl_f1(sl, rs, "uPulseAmount", 2.0f);
        sl_f1(sl, rs, "uPulseSpeed", 0.6f);
        sl_f2(sl, rs, "uDetailRange", 0.0f, 0.0f);   // shared program: the vortex leaves its fade range set
        draw_batch(st, ctx, rs, chunks, n, KB_RING);
    }
    if (p->showStreaks) {
        StreakParams rp = p->rain;
        float fd = ctx->path->style.floorDepth;
        rp.slabYMin = p->rainYMin - fd;
        rp.slabYMax = p->rainYMax - fd;
        streaks_draw(&st->streaks, &rp, ctx);
    }
}

static const char *cor_label(const Environment *env, int archetype)
{
    return (archetype >= 0 && archetype <= CA_COUNT) ? CA_NAMES[archetype] : "?";
}

static void cor_unload(Environment *env)
{
    CorridorState *st = (CorridorState *)env->state;
    streaks_unload(&st->streaks);
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
