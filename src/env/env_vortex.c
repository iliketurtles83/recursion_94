// Environment #1: "Data Vortex" — segmented neon rings, spiral ribbons, speed
// streaks, tumbling wireframe debris, holographic rings and a blazing core.
#include "env_common.h"
#include "core/rng.h"
#include "core/pacing.h"
#include "render/rmath.h"
#include "render/pipeline.h"
#include "render/shapes.h"
#include "render/streaks.h"
#include "render/tunneldetail.h"
#include "rlgl.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PI_F  3.14159265f
#define TAU_F 6.28318531f
#define DEG   (PI_F / 180.0f)

// Feature sub-streams (adding a feature must not reshuffle the others).
enum { F_RINGS = 1, F_RIBBONS, F_DEBRIS, F_HOLO, F_PACING, F_RAILS };

// Chunk batches (material slots).
enum { B_RINGS = 0, B_RIBBONS, B_EDGES, B_FACES, B_HOLO, B_RAILS, B_STRUCTURE };

// Per-chunk uniform layout (vec4 slots).
#define MAX_RINGS   16
#define MAX_OBJECTS 16
#define MAX_DEBRIS  8
#define MAX_HOLO    4
enum { U_RING_A = 0, U_RING_B = 16, U_RING_C = 32, U_OBJ_A = 48, U_OBJ_B = 64, U_OBJ_C = 80 };

#define RIBBON_PERIOD  960.0   // multiple of CHUNK_LEN; ribbon u wraps here
#define RAIL_PIECE     12.0    // rails switch on/off on this grid (divides CHUNK_LEN)
#define MAX_RAILS      48
#define STREAK_COUNT   6000

enum { STYLE_BAND, STYLE_CASTELLATED, STYLE_TICKS, STYLE_DASHED, STYLE_DOUBLE, STYLE_COUNT };
enum { ARCH_CALM, ARCH_GATES, ARCH_DENSE, ARCH_CORRIDOR, ARCH_COUNT, ARCH_DEFAULT = ARCH_COUNT };

static const char *ARCH_NAMES[ARCH_COUNT + 1] = { "CALM", "GATES", "DENSE", "CORRIDOR", "DEFAULT" };

typedef struct ArchParams {
    float spacingMul, gateChance, debrisMin, debrisMax;
    float castMul, gapMul, ringMul, ribbonMul, railDensity, energy;
} ArchParams;

typedef struct VortexParams {
    PathStyle path;
    // rings
    float ringSpacingMin, ringSpacingMax, gateChance, gateSpacing;
    int   gateMin, gateMax;
    int   layersMin, layersMax;
    float layerRMin, layerRMax;
    int   segMin, segMax;
    float gapMin, gapMax;
    float bandWMin, bandWMax;
    float castStepMin, castStepMax, castCellMin, castCellMax;
    float tickLenMin, tickLenMax;
    float wBand, wCast, wTicks, wDashed, wDouble;
    int   spokesMax;
    float angVelMax, ringIntensity, pulseChance, pulseAmount, pulseSpeed;
    float railIntensity, railDensityMul;
    // ribbons
    int   ribbonCount;
    float ribbonRMin, ribbonRMax, ribbonWMin, ribbonWMax, ribbonOmegaMin, ribbonOmegaMax;
    float ribbonIntensity, ribbonScroll;
    // streaks
    StreakParams streaks;
    // debris
    int   debrisMax;
    float debrisSizeMin, debrisSizeMax, octaChance, debrisRMin, debrisRMax, keepout;
    float tumbleMin, tumbleMax, edgeWidth, edgeIntensity, faceIntensity, dataRate;
    // holo rings
    float holoChance, holoIntensity;
    // core / background
    float coreIntensity, coreSize, coreStreak, coreRays, bgGlow, dust;
    // pacing
    bool  pacing;
    float curveDetail, themeHue, detailNear, detailFar;
    bool showPanels;
    // layer toggles
    bool  showRings, showRibbons, showStreaks, showDebris, showHolo, showCore, showBackground;
} VortexParams;

typedef struct VortexState {
    uint64_t     seed;
    VortexParams p;
    ArchParams   arch[ARCH_COUNT + 1];
    ShaderLib   *sl;
    int          shRing, shRibbon, shObject, shFace, shCore, shBg, shPanel;
    Material     mat;
    StreakSystem streaks;
    Vector2      coreUV;         // smoothed screen position of the core
    float        coreVisible;
    bool         coreInit;
} VortexState;

static const Color COL_CYAN = { 0x4F, 0xEA, 0xF5, 255 };
static const Color COL_PALE = { 0xC8, 0xF6, 0xFF, 255 };
static const Color COL_ICE  = { 0x8A, 0xD8, 0xFF, 255 };
static const Color COL_PURPLE = { 0x7A, 0x5C, 0xFF, 255 };
static const Color COL_BLUE = { 0x3A, 0x7B, 0xFF, 255 };

static Color theme_color(const VortexParams *p,Color color)
{
    if(p->themeHue==0)return color;
    Vector3 hsv=ColorToHSV(color);
    float primary=fmodf(188+p->themeHue*360,360);
    float hue=hsv.x+p->themeHue*360;
    // Warm tunnels keep cool ribbons and a dark blue background. Rotating
    // every source hue together made the gold theme's backdrop muddy green.
    if(primary>25&&primary<80&&hsv.x>195)hue=primary+180+(hsv.x-220)*.3f;
    return ColorFromHSV(fmodf(hue,360),hsv.y,hsv.z);
}

// ---------------------------------------------------------------------------
// Parameters

static void vortex_register(Environment *env, ParamRegistry *reg)
{
    VortexState *st = (VortexState *)env->state;
    VortexParams *p = &st->p;
    const char *g = "Path";
    params_float(reg, g, "baseRadius", &p->path.baseRadius, 12.0f, 3.0f, 40.0f);
    params_float(reg, g, "radiusNoiseAmp", &p->path.radiusNoiseAmp, 0.12f, 0.0f, 0.5f);
    params_float(reg, g, "radiusNoiseScale", &p->path.radiusNoiseScale, 60.0f, 5.0f, 400.0f);
    params_float(reg, g, "maxYawStepDeg", &p->path.maxYawStepDeg, 14.0f, 0.0f, 40.0f);
    params_float(reg, g, "maxPitchDeg", &p->path.maxPitchDeg, 20.0f, 0.0f, 60.0f);
    params_float(reg, g, "pitchSpring", &p->path.pitchSpring, 0.15f, 0.0f, 1.0f);
    params_float(reg, g, "twistRateMax", &p->path.twistRateMax, 0.015f, 0.0f, 0.1f);

    g = "Rings";
    params_bool(reg, g, "showRings", &p->showRings, true);
    params_float(reg, g, "ringSpacingMin", &p->ringSpacingMin, 5.0f, 1.0f, 30.0f);
    params_float(reg, g, "ringSpacingMax", &p->ringSpacingMax, 10.0f, 1.0f, 40.0f);
    params_float(reg, g, "gateChance", &p->gateChance, 0.12f, 0.0f, 1.0f);
    params_int(reg, g, "gateMin", &p->gateMin, 3, 2, 8);
    params_int(reg, g, "gateMax", &p->gateMax, 5, 2, 8);
    params_float(reg, g, "gateSpacing", &p->gateSpacing, 1.5f, 0.5f, 5.0f);
    params_int(reg, g, "layersMin", &p->layersMin, 2, 1, 6);
    params_int(reg, g, "layersMax", &p->layersMax, 3, 1, 6);
    params_float(reg, g, "layerRMin", &p->layerRMin, 0.95f, 0.5f, 2.0f);
    params_float(reg, g, "layerRMax", &p->layerRMax, 1.4f, 0.5f, 2.5f);
    params_int(reg, g, "segMin", &p->segMin, 3, 1, 24);
    params_int(reg, g, "segMax", &p->segMax, 12, 1, 24);
    params_float(reg, g, "gapMin", &p->gapMin, 0.12f, 0.0f, 0.9f);
    params_float(reg, g, "gapMax", &p->gapMax, 0.45f, 0.0f, 0.9f);
    params_float(reg, g, "bandWMin", &p->bandWMin, 0.10f, 0.02f, 2.0f);
    params_float(reg, g, "bandWMax", &p->bandWMax, 0.28f, 0.02f, 2.0f);
    params_float(reg, g, "castStepMin", &p->castStepMin, 0.5f, 0.05f, 3.0f);
    params_float(reg, g, "castStepMax", &p->castStepMax, 1.4f, 0.05f, 3.0f);
    params_float(reg, g, "castCellMinDeg", &p->castCellMin, 5.0f, 1.0f, 30.0f);
    params_float(reg, g, "castCellMaxDeg", &p->castCellMax, 12.0f, 1.0f, 30.0f);
    params_float(reg, g, "tickLenMin", &p->tickLenMin, 0.3f, 0.05f, 3.0f);
    params_float(reg, g, "tickLenMax", &p->tickLenMax, 1.2f, 0.05f, 3.0f);
    params_float(reg, g, "wBand", &p->wBand, 1.0f, 0.0f, 5.0f);
    params_float(reg, g, "wCastellated", &p->wCast, 2.2f, 0.0f, 5.0f);
    params_float(reg, g, "wTicks", &p->wTicks, 0.5f, 0.0f, 5.0f);
    params_float(reg, g, "wDashed", &p->wDashed, 0.4f, 0.0f, 5.0f);
    params_float(reg, g, "wDouble", &p->wDouble, 0.6f, 0.0f, 5.0f);
    params_int(reg, g, "spokesMax", &p->spokesMax, 6, 0, 16);
    params_float(reg, g, "angVelMax", &p->angVelMax, 0.4f, 0.0f, 2.0f);
    params_float(reg, g, "ringIntensity", &p->ringIntensity, 2.6f, 0.0f, 10.0f);
    params_float(reg, g, "pulseChance", &p->pulseChance, 0.25f, 0.0f, 1.0f);
    params_float(reg, g, "pulseAmount", &p->pulseAmount, 2.0f, 0.0f, 8.0f);
    params_float(reg, g, "pulseSpeed", &p->pulseSpeed, 0.6f, 0.0f, 4.0f);
    params_float(reg, g, "railIntensity", &p->railIntensity, 2.6f, 0.0f, 8.0f);
    params_float(reg, g, "railDensityMul", &p->railDensityMul, 1.0f, 0.0f, 3.0f);

    g = "Ribbons";
    params_bool(reg, g, "showRibbons", &p->showRibbons, true);
    params_int(reg, g, "ribbonCount", &p->ribbonCount, 3, 0, 6);
    params_float(reg, g, "ribbonRMin", &p->ribbonRMin, 1.05f, 0.5f, 2.0f);
    params_float(reg, g, "ribbonRMax", &p->ribbonRMax, 1.3f, 0.5f, 2.0f);
    params_float(reg, g, "ribbonWMin", &p->ribbonWMin, 2.0f, 0.2f, 10.0f);
    params_float(reg, g, "ribbonWMax", &p->ribbonWMax, 5.0f, 0.2f, 10.0f);
    params_float(reg, g, "ribbonOmegaMin", &p->ribbonOmegaMin, 0.03f, 0.0f, 0.3f);
    params_float(reg, g, "ribbonOmegaMax", &p->ribbonOmegaMax, 0.08f, 0.0f, 0.3f);
    params_float(reg, g, "ribbonIntensity", &p->ribbonIntensity, 0.4f, 0.0f, 3.0f);
    params_float(reg, g, "ribbonScroll", &p->ribbonScroll, 0.15f, 0.0f, 2.0f);

    g = "Streaks";
    params_bool(reg, g, "showStreaks", &p->showStreaks, true);
    params_float(reg, g, "streakDensity", &p->streaks.density, 1.0f, 0.0f, 1.0f);
    params_float(reg, g, "streakLenMul", &p->streaks.lenMul, 1.0f, 0.1f, 5.0f);
    params_float(reg, g, "streakWMin", &p->streaks.wMin, 0.03f, 0.005f, 0.3f);
    params_float(reg, g, "streakWMax", &p->streaks.wMax, 0.08f, 0.005f, 0.3f);
    params_float(reg, g, "streakIntensity", &p->streaks.intensity, 1.6f, 0.0f, 10.0f);
    params_float(reg, g, "cometFrac", &p->streaks.cometFrac, 0.01f, 0.0f, 0.2f);
    params_float(reg, g, "purpleFrac", &p->streaks.accentFrac, 0.15f, 0.0f, 1.0f);
    params_float(reg, g, "helixFrac", &p->streaks.helixFrac, 0.2f, 0.0f, 1.0f);
    params_float(reg, g, "helixRate", &p->streaks.helixRate, 0.05f, -0.3f, 0.3f);
    params_float(reg, g, "streakZRange", &p->streaks.zRange, 160.0f, 40.0f, 400.0f);
    params_float(reg, g, "streakZBehind", &p->streaks.zBehind, 12.0f, -50.0f, 50.0f);
    params_float(reg, g, "streakSpeedMul", &p->streaks.speedMul, 1.0f, 0.0f, 4.0f);
    params_float(reg, g, "streakInnerRadius", &p->streaks.cylinderInner, 0.2f, 0.0f, 1.2f);
    params_float(reg, g, "streakCenterClear", &p->streaks.centerClear, 0.0f, 0.0f, 1.0f);
    p->streaks.colMain = (Color){ 0xD8, 0xF8, 0xFF, 255 };
    p->streaks.colAccent = theme_color(&st->p,COL_PURPLE);

    g = "Debris";
    params_bool(reg, g, "showDebris", &p->showDebris, true);
    params_int(reg, g, "debrisMax", &p->debrisMax, 6, 0, MAX_DEBRIS);
    params_float(reg, g, "debrisSizeMin", &p->debrisSizeMin, 0.6f, 0.1f, 5.0f);
    params_float(reg, g, "debrisSizeMax", &p->debrisSizeMax, 2.5f, 0.1f, 5.0f);
    params_float(reg, g, "octaChance", &p->octaChance, 0.2f, 0.0f, 1.0f);
    params_float(reg, g, "debrisRMin", &p->debrisRMin, 0.45f, 0.0f, 1.2f);
    params_float(reg, g, "debrisRMax", &p->debrisRMax, 0.95f, 0.0f, 1.2f);
    params_float(reg, g, "keepout", &p->keepout, 0.35f, 0.0f, 0.9f);
    params_float(reg, g, "tumbleMin", &p->tumbleMin, 0.2f, 0.0f, 3.0f);
    params_float(reg, g, "tumbleMax", &p->tumbleMax, 1.2f, 0.0f, 3.0f);
    params_float(reg, g, "edgeWidth", &p->edgeWidth, 0.06f, 0.01f, 0.3f);
    params_float(reg, g, "edgeIntensity", &p->edgeIntensity, 3.0f, 0.0f, 10.0f);
    params_float(reg, g, "faceIntensity", &p->faceIntensity, 0.35f, 0.0f, 3.0f);
    params_float(reg, g, "dataRate", &p->dataRate, 1.5f, 0.0f, 10.0f);
    params_bool(reg, g, "showHolo", &p->showHolo, true);
    params_float(reg, g, "holoChance", &p->holoChance, 0.45f, 0.0f, 1.0f);
    params_float(reg, g, "holoIntensity", &p->holoIntensity, 1.6f, 0.0f, 8.0f);

    g = "Core";
    params_bool(reg, g, "showCore", &p->showCore, true);
    params_bool(reg, g, "showBackground", &p->showBackground, true);
    params_float(reg, g, "coreIntensity", &p->coreIntensity, 14.0f, 0.0f, 40.0f);
    params_float(reg, g, "coreSize", &p->coreSize, 0.07f, 0.01f, 0.6f);
    params_float(reg, g, "coreStreak", &p->coreStreak, 1.0f, 0.0f, 4.0f);
    params_float(reg, g, "coreRays", &p->coreRays, 0.5f, 0.0f, 4.0f);
    params_float(reg, g, "bgGlow", &p->bgGlow, 0.3f, 0.0f, 1.0f);
    params_float(reg, g, "dust", &p->dust, 0.03f, 0.0f, 0.3f);

    g = "Pacing";
    params_bool(reg, g, "pacing", &p->pacing, true);
    g="Tunnel detail";
    params_float(reg,g,"curveDetail",&p->curveDetail,1.0f,1.0f,4.0f);
    params_bool(reg,g,"showPanels",&p->showPanels,false);
    params_float(reg,g,"themeHue",&p->themeHue,0,0,1);
    params_float(reg,g,"detailNear",&p->detailNear,45,20,120);
    params_float(reg,g,"detailFar",&p->detailFar,125,80,300);
}

static PathStyle vortex_path_style(const Environment *env)
{
    const VortexState *st = (const VortexState *)env->state;
    PathStyle s = st->p.path;
    s.hasWalls = true;
    return s;
}

// ---------------------------------------------------------------------------
// Pacing: hash-driven Markov chain of archetype runs.

static void init_archetypes(VortexState *st)
{
    //                         spacing gate  dMin dMax cast  gap   ring  ribbon rails energy
    st->arch[ARCH_CALM]     = (ArchParams){ 1.45f, 0.00f, 0, 2, 0.8f, 1.3f, 0.85f, 1.6f, 0.25f, 0.25f };
    st->arch[ARCH_GATES]    = (ArchParams){ 1.10f, 0.55f, 1, 3, 1.0f, 1.0f, 1.00f, 1.0f, 0.45f, 0.55f };
    st->arch[ARCH_DENSE]    = (ArchParams){ 0.65f, 0.10f, 3, 6, 1.3f, 0.9f, 1.00f, 0.8f, 0.65f, 0.90f };
    st->arch[ARCH_CORRIDOR] = (ArchParams){ 0.70f, 0.00f, 1, 3, 3.0f, 0.40f, 0.95f, 0.7f, 0.80f, 0.70f };
    st->arch[ARCH_DEFAULT]  = (ArchParams){ 1.00f, -1.0f, 0, 6, 1.0f, 1.0f, 1.00f, 1.0f, 0.50f, 0.50f };
}

static const int RUN_MIN[ARCH_COUNT] = { 5, 3, 3, 3 };
static const int RUN_MAX[ARCH_COUNT] = { 10, 7, 6, 6 };
static const float TRANSITION[ARCH_COUNT][ARCH_COUNT] = {
    { 0.00f, 0.45f, 0.25f, 0.30f },
    { 0.35f, 0.00f, 0.40f, 0.25f },
    { 0.45f, 0.25f, 0.00f, 0.30f },
    { 0.50f, 0.30f, 0.20f, 0.00f },
};

static const PacingTable PACING = { ARCH_COUNT, ARCH_CALM, RUN_MIN, RUN_MAX, &TRANSITION[0][0] };

static int archetype_of_chunk(const VortexState *st, int k)
{
    if (!st->p.pacing) return ARCH_DEFAULT;
    return pacing_archetype(&PACING, ct_hash64_3(st->seed, ENV_VORTEX, F_PACING), k);
}

typedef struct ChunkArch { int prev, cur; double s0; } ChunkArch;

static ArchParams arch_at(const VortexState *st, const ChunkArch *ca, double s)
{
    ArchParams cur = st->arch[ca->cur];
    if (ca->prev == ca->cur) return cur;
    ArchParams r;
    pacing_lerp(&r.spacingMul, &st->arch[ca->prev].spacingMul, &cur.spacingMul, (int)(sizeof(ArchParams) / sizeof(float)),
                pacing_blend_t(s, ca->s0, CHUNK_LEN));
    return r;
}

static float vortex_energy_at(const Environment *env, const PathSystem *path, double s)
{
    const VortexState *st = (const VortexState *)env->state;
    int k = chunk_index_at(s);
    ChunkArch ca = { archetype_of_chunk(st, k - 1), archetype_of_chunk(st, k), k * CHUNK_LEN };
    return arch_at(st, &ca, s).energy;
}

static const char *vortex_chunk_label(const Environment *env, int archetype)
{
    return (archetype >= 0 && archetype <= ARCH_COUNT) ? ARCH_NAMES[archetype] : "?";
}

// ---------------------------------------------------------------------------
// Chunk building (pure CPU)

static void set_u(ChunkCpu *c, int slot, Vector3 v, float w)
{
    c->uniforms[slot][0] = v.x; c->uniforms[slot][1] = v.y; c->uniforms[slot][2] = v.z; c->uniforms[slot][3] = w;
}

static Color pick_ring_color(const VortexState *st,Pcg32 *r)
{
    float x = pcg32_float(r);
    if (x < 0.40f) return theme_color(&st->p,COL_CYAN);
    if (x < 0.90f) return theme_color(&st->p,COL_PALE);
    return theme_color(&st->p,COL_ICE);
}

static void build_layer(const VortexState *st, LineGeo *g, Pcg32 *r, float radius, float R, int style,
                        float gapMul, LineAttr base)
{
    const VortexParams *p = &st->p;
    int segs = pcg32_int(r, p->segMin, p->segMax);
    float gap = pcg32_range(r, p->gapMin, p->gapMax) * gapMul;
    if (gap < 0.02f) gap = 0.02f;
    if (gap > 0.7f) gap = 0.7f;
    float phase = pcg32_float(r) * TAU_F;
    float scale = R / 12.0f;
    float step3 = 3.0f * DEG / p->curveDetail;

    // Style-wide dimensions so a layer reads as one consistent trace.
    float bandW = pcg32_range(r, p->bandWMin, p->bandWMax) * scale;
    float castStep = pcg32_range(r, p->castStepMin, p->castStepMax) * scale;
    float castCell = pcg32_range(r, p->castCellMin, p->castCellMax) * DEG;
    float tickLen = pcg32_range(r, p->tickLenMin, p->tickLenMax) * scale;
    float tickEvery = pcg32_range(r, 2.0f, 5.0f) * DEG;
    float dashLen = pcg32_range(r, 3.0f, 8.0f) * DEG;
    float dashGap = pcg32_range(r, 1.5f, 3.0f) * DEG;
    float dblSep = pcg32_range(r, 0.2f, 0.4f) * scale;

    for (int j = 0; j < segs; j++) {
        float a0 = phase + (float)j * TAU_F / (float)segs;
        float span = TAU_F / (float)segs * (1.0f - gap);
        float a1 = a0 + span;
        LineAttr at = base;
        at.hash = pcg32_float(r);
        at.intensity = base.intensity * pcg32_range(r, 0.6f, 1.2f);
        at.param = pcg32_chance(r, p->pulseChance) ? pcg32_range(r, 0.04f, 0.15f) : 0.0f;
        switch (style) {
        case STYLE_BAND:
            lg_planar_arc(g, radius, a0, a1, 0.0f, bandW * 0.5f, step3, at);
            break;
        case STYLE_CASTELLATED: {
            float w = bandW * 0.5f;
            if (w < 0.07f * scale) w = 0.07f * scale;
            int cells = (int)ceilf(span / castCell);
            if (cells < 1) cells = 1;
            float cw = span / (float)cells;
            int prevUp = -1;
            for (int c = 0; c < cells; c++) {
                int up = pcg32_chance(r, 0.5f);
                float rr = radius + (up ? castStep : 0.0f);
                float ca0 = a0 + cw * (float)c, ca1 = ca0 + cw;
                lg_planar_arc(g, rr, ca0, ca1, 0.0f, w * 0.5f, step3, at);
                if (prevUp >= 0 && prevUp != up)
                    lg_planar_radial(g, radius - w * 0.5f, radius + castStep + w * 0.5f, ca0, 0.0f, w * 0.5f, at);
                prevUp = up;
            }
            break;
        }
        case STYLE_TICKS: {
            LineAttr tb = at;
            tb.intensity *= 0.8f;
            lg_planar_arc(g, radius, a0, a1, 0.0f, 0.05f * scale, step3, tb);
            for (float a = a0; a <= a1; a += tickEvery) {
                float len = tickLen * pcg32_range(r, 0.5f, 1.0f);
                lg_planar_radial(g, radius, radius + len, a, 0.0f, 0.05f * scale, at);
            }
            break;
        }
        case STYLE_DASHED: {
            for (float a = a0; a < a1; a += dashLen + dashGap) {
                float e = a + dashLen;
                if (e > a1) e = a1;
                lg_planar_arc(g, radius, a, e, 0.0f, bandW * 0.3f, step3, at);
            }
            break;
        }
        case STYLE_DOUBLE: {
            float w = bandW * 0.35f;
            lg_planar_arc(g, radius, a0, a1, 0.0f, w * 0.5f, step3, at);
            lg_planar_arc(g, radius + dblSep, a0, a1, 0.0f, w * 0.5f, step3, at);
            break;
        }
        }
    }
}

static void build_ring(const VortexState *st, ChunkCpu *c, const PathSystem *path, int slot, double s,
                       bool gate, const ArchParams *ap, Pcg32 *r)
{
    const VortexParams *p = &st->p;
    PathSample ps = path_sample(path, s);
    float R = (float)ps.radius;
    set_u(c, U_RING_A + slot, env_local(ps.pos, c->origin), 0.0f);
    set_u(c, U_RING_B + slot, dv_to_v3(ps.B), 0.0f);
    set_u(c, U_RING_C + slot, dv_to_v3(ps.N), 0.0f);

    LineGeo *g = &c->geo[B_RINGS];
    int layers = pcg32_int(r, p->layersMin, p->layersMax);
    float rMin = p->layerRMin, rMax = gate ? (p->layerRMin + (p->layerRMax - p->layerRMin) * 0.6f) : p->layerRMax;
    float weights[STYLE_COUNT] = { p->wBand, p->wCast * ap->castMul, p->wTicks, p->wDashed, p->wDouble };
    float rInner = 1e9f, rOuter = 0.0f;
    float ringBright = p->ringIntensity * ap->ringMul * (gate ? 1.2f : 1.0f);
    for (int li = 0; li < layers; li++) {
        float t = ((float)li + pcg32_range(r, 0.15f, 0.85f)) / (float)layers;
        float radius = R * (rMin + (rMax - rMin) * t);
        int style = pcg32_weighted(r, weights, STYLE_COUNT);
        float sign = (li & 1) ? -1.0f : 1.0f;
        LineAttr base = { pick_ring_color(st,r), ringBright, 0.0f, 0.0f, (float)slot, sign * pcg32_range(r, 0.0f, p->angVelMax) };
        build_layer(st, g, r, radius, R, style, ap->gapMul, base);
        if (radius < rInner) rInner = radius;
        if (radius > rOuter) rOuter = radius;
    }

    // Spokes and pads (static).
    int spokes = layers > 1 ? pcg32_int(r, 0, p->spokesMax) : 0;
    float scale = R / 12.0f;
    for (int i = 0; i < spokes; i++) {
        float a = pcg32_float(r) * TAU_F;
        LineAttr at = { theme_color(&st->p,COL_CYAN), ringBright * 0.8f, pcg32_float(r), 0.0f, (float)slot, 0.0f };
        lg_planar_radial(g, rInner, rOuter, a, 0.0f, 0.045f * scale, at);
        float pad = 0.16f * scale;
        lg_planar_arc(g, rInner, a - pad / rInner, a + pad / rInner, 0.0f, pad, 3.0f * DEG, at);
        lg_planar_arc(g, rOuter, a - pad / rOuter, a + pad / rOuter, 0.0f, pad, 3.0f * DEG, at);
    }
    // Sparse, smooth silhouettes replace the crowded small details at distance.
    // Use the existing ring basis without advancing the deterministic RNG.
    if(slot%3==0) {
        LineAttr guide={.color=theme_color(p,COL_CYAN),.intensity=ringBright*.22f,.param=-1,.group=(float)slot};
        lg_planar_arc(g,R*1.10f,0,TAU_F,0,.085f,4.5f*DEG,guide);
    }
}

static void build_rings(const VortexState *st, ChunkCpu *c, const PathSystem *path, uint64_t chunkSeed, const ChunkArch *ca)
{
    const VortexParams *p = &st->p;
    Pcg32 r = pcg32_seed(ct_hash64_2(chunkSeed, F_RINGS));
    ArchParams ap = arch_at(st, ca, c->s0);
    double s = c->s0 + pcg32_float(&r) * p->ringSpacingMin * ap.spacingMul;
    int n = 0;
    while (s < c->s1 && n < MAX_RINGS) {
        ap = arch_at(st, ca, s);
        float gateChance = ap.gateChance >= 0.0f ? ap.gateChance : p->gateChance;
        bool gate = pcg32_chance(&r, gateChance);
        int count = gate ? pcg32_int(&r, p->gateMin, p->gateMax) : 1;
        for (int i = 0; i < count && s < c->s1 && n < MAX_RINGS; i++) {
            build_ring(st, c, path, n, s, gate, &ap, &r);
            n++;
            if (i + 1 < count) s += p->gateSpacing;
        }
        s += pcg32_range(&r, p->ringSpacingMin, p->ringSpacingMax) * ap.spacingMul;
    }
}

// Long axial rails along the walls; on/off decided on a global grid so they join across chunks.
static void build_rails(const VortexState *st, ChunkCpu *c, const PathSystem *path, const ChunkArch *ca)
{
    const VortexParams *p = &st->p;
    LineGeo *g = &c->geo[B_RAILS];
    uint64_t base = ct_hash64_3(st->seed, ENV_VORTEX, F_RAILS);
    for (int i = 0; i < MAX_RAILS; i++) {
        uint64_t rh = ct_hash64_2(base, (uint64_t)i);
        float theta = ct_hash01(rh) * TAU_F;
        float rf = 0.98f + ct_hash01(rh + 1) * 0.45f;
        float bright = p->railIntensity * (0.5f + ct_hash01(rh + 2));
        for (double q0 = c->s0; q0 < c->s1 - 1e-6; q0 += RAIL_PIECE) {
            int64_t qi = (int64_t)floor(q0 / RAIL_PIECE + 0.5);
            ArchParams ap = arch_at(st, ca, q0);
            if (ct_hash01(ct_hash64_2(rh, (uint64_t)qi)) >= ap.railDensity * p->railDensityMul * 0.5f) continue;
            const int steps = (int)(4*p->curveDetail);
            Vector3 prev = { 0 };
            for (int k = 0; k <= steps; k++) {
                double s = q0 + RAIL_PIECE * k / steps;
                PathSample ps = path_sample(path, s);
                double rr = ps.radius * rf;
                DVec3 w = dv_add(ps.pos, dv_add(dv_scale(ps.B, cos(theta) * rr), dv_scale(ps.N, sin(theta) * rr)));
                Vector3 l = env_local(w, c->origin);
                if (k > 0) {
                    float ia = bright * (k == 1 ? 0.0f : 1.0f), ib = bright * (k == steps ? 0.0f : 1.0f);
                    LineAttr at = { theme_color(&st->p,COL_PALE), 1.0f, ct_hash01(rh + 3), 0.0f, -1.0f, 0.0f };
                    lg_facing_segment(g, prev, l, 0.045f, 0.045f, ia, ib, at);
                }
                prev = l;
            }
        }
    }
}

static void build_ribbons(const VortexState *st, ChunkCpu *c, const PathSystem *path, const ChunkArch *ca)
{
    const VortexParams *p = &st->p;
    LineGeo *g = &c->geo[B_RIBBONS];
    uint64_t base = ct_hash64_3(st->seed, ENV_VORTEX, F_RIBBONS);
    const double ds = 1.5/p->curveDetail;
    int n = (int)ceil(CHUNK_LEN / ds);
    double uBase = floor(c->s0 / RIBBON_PERIOD) * RIBBON_PERIOD;
    for (int i = 0; i < p->ribbonCount; i++) {
        uint64_t h = ct_hash64_2(base, (uint64_t)i);
        double theta0 = ct_hash01(h) * TAU_F;
        double omega = (p->ribbonOmegaMin + (p->ribbonOmegaMax - p->ribbonOmegaMin) * ct_hash01(h + 1)) * ((i & 1) ? -1.0 : 1.0);
        double rf = p->ribbonRMin + (p->ribbonRMax - p->ribbonRMin) * ct_hash01(h + 2);
        float hw = 0.5f * (p->ribbonWMin + (p->ribbonWMax - p->ribbonWMin) * ct_hash01(h + 3));
        Color col = (i & 1) ? theme_color(&st->p,COL_BLUE) : theme_color(&st->p,COL_PURPLE);
        int b = lg_reserve(g, (n + 1) * 2, n * 6);
        for (int k = 0; k <= n; k++) {
            double s = c->s0 + CHUNK_LEN * k / n;
            PathSample ps = path_sample(path, s);
            ArchParams ap = arch_at(st, ca, s);
            double th = theta0 + omega * s;
            double rr = ps.radius * rf;
            DVec3 radial = dv_add(dv_scale(ps.B, cos(th)), dv_scale(ps.N, sin(th)));
            DVec3 tang = dv_add(dv_scale(ps.B, -sin(th)), dv_scale(ps.N, cos(th)));
            DVec3 ctr = dv_madd(ps.pos, radial, rr);
            Vector3 acr = dv_to_v3(tang);
            Vector3 l = env_local(ctr, c->origin);
            float u = (float)(s - uBase);
            Vector4 t = { hw, p->ribbonIntensity * ap.ribbonMul, ct_hash01(h + 4), 0.0f };
            Vector2 uv2 = { (float)i + 0.37f, 0.0f };
            lg_vertex(g, (Vector3){ l.x - acr.x * hw, l.y - acr.y * hw, l.z - acr.z * hw }, (Vector2){ u, -1.0f }, uv2, acr, t, col);
            lg_vertex(g, (Vector3){ l.x + acr.x * hw, l.y + acr.y * hw, l.z + acr.z * hw }, (Vector2){ u, 1.0f }, uv2, acr, t, col);
        }
        for (int k = 0; k < n; k++) lg_quad(g, b, k * 2, k * 2 + 1, k * 2 + 3, k * 2 + 2);
    }
}

static Vector3 rand_unit(Pcg32 *r)
{
    float z = pcg32_range(r, -1.0f, 1.0f);
    float a = pcg32_float(r) * TAU_F;
    float s = sqrtf(1.0f - z * z);
    return (Vector3){ s * cosf(a), s * sinf(a), z };
}

static void build_debris(const VortexState *st, ChunkCpu *c, const PathSystem *path, uint64_t chunkSeed,
                         const ChunkArch *ca, int *objSlot)
{
    const VortexParams *p = &st->p;
    Pcg32 r = pcg32_seed(ct_hash64_2(chunkSeed, F_DEBRIS));
    ArchParams ap = arch_at(st, ca, (c->s0 + c->s1) * 0.5);
    int lo = (int)ap.debrisMin, hi = (int)(ap.debrisMax + 0.5f);
    if (hi > p->debrisMax) hi = p->debrisMax;
    if (lo > hi) lo = hi;
    int count = pcg32_int(&r, lo, hi);
    for (int i = 0; i < count && *objSlot < MAX_DEBRIS; i++) {
        double s = c->s0 + pcg32_float(&r) * CHUNK_LEN;
        PathSample ps = path_sample(path, s);
        float R = (float)ps.radius;
        float size = pcg32_range(&r, p->debrisSizeMin, p->debrisSizeMax);
        bool octa = pcg32_chance(&r, p->octaChance);
        float h = size * 0.5f * (octa ? 1.3f : 1.0f);
        float bound = octa ? h : h * 1.7320508f;
        float rMin = p->debrisRMin * R, rMax = p->debrisRMax * R;
        float minAllowed = p->keepout * R + bound;
        if (rMin < minAllowed) rMin = minAllowed;
        if (rMax < rMin) rMax = rMin;
        float rr = pcg32_range(&r, rMin, rMax);
        float phi = pcg32_float(&r) * TAU_F;
        DVec3 ctr = dv_add(ps.pos, dv_add(dv_scale(ps.B, cos(phi) * rr), dv_scale(ps.N, sin(phi) * rr)));
        int g = (*objSlot)++;
        Vector3 axis = rand_unit(&r);
        float spd = pcg32_range(&r, p->tumbleMin, p->tumbleMax) * (pcg32_chance(&r, 0.5f) ? 1.0f : -1.0f);
        set_u(c, U_OBJ_A + g, env_local(ctr, c->origin), size);
        set_u(c, U_OBJ_B + g, axis, spd);
        set_u(c, U_OBJ_C + g, (Vector3){ pcg32_float(&r) * TAU_F, 0, 0 }, 0.0f);
        LineAttr e = { theme_color(&st->p,COL_PALE), p->edgeIntensity * pcg32_range(&r, 0.8f, 1.2f), pcg32_float(&r), 0.0f, (float)g, 0.0f };
        LineAttr f = { theme_color(&st->p,COL_CYAN), p->faceIntensity, pcg32_float(&r), 0.0f, (float)g, 0.0f };
        float w = p->edgeWidth * (0.7f + 0.3f * size / p->debrisSizeMax);
        if (octa) shape_octa(&c->geo[B_EDGES], &c->geo[B_FACES], h, e, f, w);
        else shape_cube(&c->geo[B_EDGES], &c->geo[B_FACES], h, e, f, w);
        if (c->debrisCount < CHUNK_MAX_DEBRIS) {
            DebrisInfo *d = &c->debris[c->debrisCount++];
            d->center = ctr; d->radius = bound; d->s = s;
        }
    }
}

static void build_holo(const VortexState *st, ChunkCpu *c, const PathSystem *path, uint64_t chunkSeed, int *objSlot)
{
    const VortexParams *p = &st->p;
    Pcg32 r = pcg32_seed(ct_hash64_2(chunkSeed, F_HOLO));
    int sets = 0;
    while (sets < 2 && pcg32_chance(&r, sets == 0 ? p->holoChance : p->holoChance * 0.4f)) {
        if (*objSlot >= MAX_OBJECTS) break;
        sets++;
        double s = c->s0 + pcg32_float(&r) * CHUNK_LEN;
        PathSample ps = path_sample(path, s);
        float R = (float)ps.radius;
        float rr = pcg32_range(&r, 0.75f, 1.05f) * R;
        float phi = pcg32_float(&r) * TAU_F;
        DVec3 ctr = dv_add(ps.pos, dv_add(dv_scale(ps.B, cos(phi) * rr), dv_scale(ps.N, sin(phi) * rr)));
        int g = (*objSlot)++;
        set_u(c, U_OBJ_A + g, env_local(ctr, c->origin), 0.0f);
        set_u(c, U_OBJ_B + g, rand_unit(&r), pcg32_range(&r, -0.25f, 0.25f));
        set_u(c, U_OBJ_C + g, (Vector3){ pcg32_float(&r) * TAU_F, 0, 0 }, 0.0f);
        Vector3 ax = rand_unit(&r);
        Vector3 u = Vector3Normalize(Vector3CrossProduct(ax, fabsf(ax.y) < 0.9f ? (Vector3){ 0, 1, 0 } : (Vector3){ 1, 0, 0 }));
        Vector3 v = Vector3CrossProduct(ax, u);
        float rad = pcg32_range(&r, 1.5f, 4.0f) * R / 12.0f;
        float ratio = pcg32_range(&r, 0.75f, 1.0f);
        int rings = pcg32_int(&r, 1, 3);
        for (int k = 0; k < rings; k++) {
            float rk = rad * (1.0f - 0.14f * (float)k);
            LineAttr at = { k == 0 ? theme_color(&st->p,COL_CYAN) : theme_color(&st->p,COL_ICE), p->holoIntensity * (k == 0 ? 1.0f : 0.7f), pcg32_float(&r), 0.0f, (float)g, 0.0f };
            const int segs = (int)(48*p->curveDetail);
            Vector3 prev = { 0 };
            float gapStart = pcg32_float(&r) * TAU_F, gapLen = pcg32_chance(&r, 0.5f) ? pcg32_range(&r, 0.3f, 1.2f) : 0.0f;
            for (int i = 0; i <= segs; i++) {
                float t = TAU_F * (float)i / (float)segs;
                Vector3 q = Vector3Add(Vector3Scale(u, cosf(t) * rk), Vector3Scale(v, sinf(t) * rk * ratio));
                float rel = fmodf(t - gapStart + TAU_F, TAU_F);
                if (i > 0 && rel > gapLen) lg_facing_segment(&c->geo[B_HOLO], prev, q, 0.025f, 0.025f, at.intensity, at.intensity, at);
                prev = q;
            }
        }
    }
}

static Vector3 panel_point(const PathSystem *path,ChunkCpu *c,double s,float a)
{
    PathSample p=path_sample(path,s);
    DVec3 r=dv_add(dv_scale(p.B,cosf(a)),dv_scale(p.N,sinf(a)));
    return env_local(dv_madd(p.pos,r,p.radius*1.25),c->origin);
}
static void build_panels(const VortexState *st,ChunkCpu *c,const PathSystem *path)
{
    if(!st->p.showPanels)return;
    LineGeo *g=&c->geo[B_STRUCTURE];
    // Sparse solid segments leave the original rings and ribbons visible.
    for(double start=c->s0+3;start<c->s1;start+=24)for(int panel=0;panel<8;panel++) {
        for(int z=0;z<4;z++)for(int a=0;a<12;a++) {
            double s0=start+z*4,s1=s0+4;
            float a0=panel*TAU_F/8+(.10f+a*.032f)*TAU_F/8,a1=a0+.032f*TAU_F/8;
            Vector3 q[4]={panel_point(path,c,s0,a0),panel_point(path,c,s0,a1),panel_point(path,c,s1,a1),panel_point(path,c,s1,a0)};
            PathSample p=path_sample(path,(s0+s1)*.5);
            Vector3 normal=dv_to_v3(dv_scale(dv_add(dv_scale(p.B,cosf((a0+a1)*.5f)),dv_scale(p.N,sinf((a0+a1)*.5f))),-1));
            int b=lg_reserve(g,4,6);
            const Vector2 uv[4]={{a/12.0f,z/4.0f},{(a+1)/12.0f,z/4.0f},{(a+1)/12.0f,(z+1)/4.0f},{a/12.0f,(z+1)/4.0f}};
            for(int k=0;k<4;k++)lg_vertex(g,q[k],uv[k],(Vector2){panel,0},normal,(Vector4){0},WHITE);
            lg_quad(g,b,0,1,2,3);
        }
    }
    tunnel_detail_build(g,path,c->origin,c->s0);
}

static void vortex_build_chunk(Environment *env, const PathSystem *path, int chunkIndex, ChunkCpu *out)
{
    VortexState *st = (VortexState *)env->state;
    uint64_t chunkSeed = ct_hash64_3(st->seed, ENV_VORTEX, (uint64_t)(int64_t)chunkIndex);
    ChunkArch ca = { archetype_of_chunk(st, chunkIndex - 1), archetype_of_chunk(st, chunkIndex), out->s0 };
    out->archetype = ca.cur;
    int objSlot = 0;
    build_rings(st, out, path, chunkSeed, &ca);
    build_rails(st, out, path, &ca);
    build_ribbons(st, out, path, &ca);
    build_debris(st, out, path, chunkSeed, &ca, &objSlot);
    build_holo(st, out, path, chunkSeed, &objSlot);
    build_panels(st,out,path);
}

// ---------------------------------------------------------------------------
// GPU side

static void vortex_init(Environment *env, uint64_t worldSeed, ShaderLib *sl)
{
    VortexState *st = (VortexState *)env->state;
    st->seed = worldSeed;
    st->sl = sl;
    init_archetypes(st);
    streaks_init(&st->streaks, sl, STREAK_COUNT);
    if (sl) {
        st->shRing = sl_load(sl, "ring.vs", "ring.fs");
        st->shRibbon = sl_load(sl, "ribbon.vs", "ribbon.fs");
        st->shObject = sl_load(sl, "object.vs", "line_facing.fs");
        st->shFace = sl_load(sl, "face.vs", "face.fs");
        st->shCore = sl_load(sl, "fullscreen.vs", "core.fs");
        st->shBg = sl_load(sl, "fullscreen.vs", "background.fs");
        st->shPanel=sl_load(sl,"tunnel_panel.vs","tunnel_panel.fs");
        st->mat = LoadMaterialDefault();
    }
}

static void vortex_reseed(Environment *env, uint64_t worldSeed)
{
    ((VortexState *)env->state)->seed = worldSeed;
}

static void vortex_upload(Environment *env, ChunkCpu *in, ChunkGpu *out) { chunk_upload_default(in, out); }
static void vortex_release(Environment *env, ChunkGpu *gpu) { chunk_release_default(gpu); }

// The core sits at path_sample(sShip + 0.9 * viewDist) when the tunnel is visible that far.
// On curves that point leaves the screen, so the core stops at the visible end of the tunnel.
static void vortex_update(Environment *env, const FrameContext *ctx)
{
    VortexState *st = (VortexState *)env->state;
    Matrix vp = MatrixMultiply(ctx->view, ctx->proj);
    Vector2 best = ctx->vanishingUV;
    float vis = 0.0f;
    // Walk outward and stop where the tunnel leaves the screen: the core then sits at the
    // visible end of the continuous tunnel instead of a far segment that re-enters the view.
    for (double off = 40.0; off <= 0.9 * 400.0; off += 15.0) {
        PathSample q = path_sample(ctx->path, ctx->sShip + off);
        float w;
        Vector2 uv = ct_project_uv(vp, dv_to_v3(dv_sub(q.pos, ctx->renderOrigin)), &w);
        if (w <= 0.0f || fabsf(uv.x - 0.5f) > 0.45f || fabsf(uv.y - 0.5f) > 0.45f) break;
        best = uv;
        vis = 1.0f;
    }
    if (!st->coreInit) { st->coreUV = best; st->coreVisible = vis; st->coreInit = true; }
    float k = 1.0f - expf(-5.0f * fmaxf(ctx->dt, 1.0f / 240.0f));
    st->coreUV.x += (best.x - st->coreUV.x) * k;
    st->coreUV.y += (best.y - st->coreUV.y) * k;
    st->coreVisible += (vis - st->coreVisible) * k;
}

static void draw_mesh(VortexState *st, const FrameContext *ctx, int sh, Mesh m, Matrix xf)
{
    st->mat.shader = sl_shader(st->sl, sh);
    DrawMesh(m, st->mat, xf);
    ctx->stats->drawCalls++;
    ctx->stats->vertices += m.vertexCount;
}

static void vortex_draw_background(Environment *env, const FrameContext *ctx)
{
    VortexState *st = (VortexState *)env->state;
    if (!st->p.showBackground) return;
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
    sl_v3(sl, sh, "uBgColor", ct_srgb_to_linear(theme_color(&st->p,(Color){ 0x03, 0x06, 0x0C, 255 })));
    sl_v3(sl, sh, "uGlowColor", ct_srgb_to_linear(theme_color(&st->p,(Color){ 0x2A, 0x7A, 0xB0, 255 })));
    sl_f1(sl, sh, "uGlow", st->p.bgGlow * (0.6f + 0.8f * ctx->intensity));
    sl_f1(sl, sh, "uDust", st->p.dust);
    sl_f1(sl, sh, "uGlowFocus", 0.0f);
    pipe_fullscreen_quad();
    EndShaderMode();
    ctx->stats->drawCalls++;
    rlEnableDepthTest();
    rlEnableDepthMask();
}

static void vortex_draw_opaque(Environment *env, const FrameContext *ctx, const ChunkGpu *const *chunks, int n)
{
    VortexState *st=env->state;
    if(!st->p.showPanels)return;
    sl_v3(st->sl,st->shPanel,"uCamPos",ctx->camera.position);
    sl_v3(st->sl,st->shPanel,"uTheme",ct_srgb_to_linear(theme_color(&st->p,COL_CYAN)));
    for(int i=0;i<n;i++)for(int j=0;j<chunks[i]->meshCount;j++)if(chunks[i]->meshBatch[j]==B_STRUCTURE)
        draw_mesh(st,ctx,st->shPanel,chunks[i]->meshes[j],env_chunk_transform(chunks[i],ctx->renderOrigin));
}

static void draw_core(VortexState *st, const FrameContext *ctx)
{
    const VortexParams *p = &st->p;
    ShaderLib *sl = st->sl;
    int sh = st->shCore;
    Vector2 uv = st->coreUV;
    float visible = st->coreVisible;
    float off = sqrtf((uv.x - 0.5f) * (uv.x - 0.5f) + (uv.y - 0.5f) * (uv.y - 0.5f)) * 2.0f;
    float centered = 1.0f - fminf(fmaxf((off - 0.1f) / 0.8f, 0.0f), 1.0f);
    float flare = ctx->flare;
    rlDrawRenderBatchActive();
    rlDisableDepthTest();
    BeginShaderMode(sl_shader(sl, sh));
    sl_f2(sl, sh, "uCorePos", uv.x, uv.y);
    sl_f1(sl, sh, "uAspect", (float)ctx->width / (float)ctx->height);
    sl_f1(sl, sh, "uCoreIntensity", p->coreIntensity * (0.35f + 0.65f * centered) * (0.8f + 0.4f * ctx->intensity) * (1.0f + 5.0f * flare));
    sl_f1(sl, sh, "uCoreSize", p->coreSize * (1.0f + 2.5f * flare));
    sl_f1(sl, sh, "uStreak", p->coreStreak);
    sl_f1(sl, sh, "uRays", p->coreRays);
    sl_f1(sl, sh, "uRayRot", ctx->shaderTime * 0.15f);
    sl_f1(sl, sh, "uVisible", visible);
    sl_v3(sl, sh, "uColor", ctx->atmo.coreColor);
    pipe_fullscreen_quad();
    EndShaderMode();
    ctx->stats->drawCalls++;
    rlEnableDepthTest();
}

static void vortex_draw_emissive(Environment *env, const FrameContext *ctx, const ChunkGpu *const *chunks, int n)
{
    VortexState *st = (VortexState *)env->state;
    const VortexParams *p = &st->p;
    ShaderLib *sl = st->sl;

    // Per-frame uniforms.
    env_set_common_uniforms(sl, st->shRing, ctx);
    sl_f1(sl, st->shRing, "uPulseAmount", p->pulseAmount);
    sl_f1(sl, st->shRing, "uPulseSpeed", p->pulseSpeed);
    sl_f2(sl, st->shRing, "uDetailRange", p->detailNear, fmaxf(p->detailFar,p->detailNear+1));
    env_set_common_uniforms(sl, st->shRibbon, ctx);
    sl_f1(sl, st->shRibbon, "uPeriod", (float)RIBBON_PERIOD);
    sl_v3(sl, st->shRibbon, "uColorA", ct_srgb_to_linear(theme_color(&st->p,COL_PURPLE)));
    sl_v3(sl, st->shRibbon, "uColorB", ct_srgb_to_linear(theme_color(&st->p,COL_BLUE)));
    sl_f1(sl, st->shRibbon, "uScroll", p->ribbonScroll);
    sl_f3(sl, st->shRibbon, "uSceneryFade", p->detailNear, p->detailFar+70, .14f);
    env_set_common_uniforms(sl, st->shObject, ctx);
    sl_f3(sl, st->shObject, "uEdgeColor", 0, 0, 0);
    sl_f1(sl, st->shObject, "uEdgeMix", 0.0f);
    sl_f1(sl, st->shObject, "uFlicker", 0.35f);
    sl_f3(sl, st->shObject, "uSceneryFade", p->detailNear+10, p->detailFar, 0);
    env_set_common_uniforms(sl, st->shFace, ctx);
    sl_f1(sl, st->shFace, "uDataRate", p->dataRate);
    sl_f3(sl, st->shFace, "uSceneryFade", p->detailNear+10, p->detailFar, 0);

    static const int ORDER[6] = { B_RIBBONS, B_RINGS, B_RAILS, B_FACES, B_EDGES, B_HOLO };
    for (int pass = 0; pass < 6; pass++) {
        int batch = ORDER[pass];
        int sh = batch == B_RIBBONS ? st->shRibbon : batch == B_RINGS ? st->shRing : batch == B_FACES ? st->shFace : st->shObject;
        if (batch == B_RIBBONS && !p->showRibbons) continue;
        if ((batch == B_RINGS || batch == B_RAILS) && !p->showRings) continue;
        if ((batch == B_FACES || batch == B_EDGES) && !p->showDebris) continue;
        if (batch == B_HOLO && !p->showHolo) continue;
        for (int i = 0; i < n; i++) {
            const ChunkGpu *c = chunks[i];
            bool any = false;
            for (int m = 0; m < c->meshCount; m++) if (c->meshBatch[m] == batch) { any = true; break; }
            if (!any) continue;
            Matrix xf = env_chunk_transform(c, ctx->renderOrigin);
            sl_f1(sl, sh, "uChunkFade", env_chunk_fade(c, ctx->time));
            if (batch == B_RINGS) {
                sl_vec4v(sl, sh, "uRingA", c->uniforms[U_RING_A], MAX_RINGS);
                sl_vec4v(sl, sh, "uRingB", c->uniforms[U_RING_B], MAX_RINGS);
                sl_vec4v(sl, sh, "uRingC", c->uniforms[U_RING_C], MAX_RINGS);
            } else if (batch == B_EDGES || batch == B_FACES || batch == B_HOLO) {
                sl_vec4v(sl, sh, "uObjA", c->uniforms[U_OBJ_A], MAX_OBJECTS);
                sl_vec4v(sl, sh, "uObjB", c->uniforms[U_OBJ_B], MAX_OBJECTS);
                sl_vec4v(sl, sh, "uObjC", c->uniforms[U_OBJ_C], MAX_OBJECTS);
            }
            for (int m = 0; m < c->meshCount; m++)
                if (c->meshBatch[m] == batch) draw_mesh(st, ctx, sh, c->meshes[m], xf);
        }
    }
    if (p->showStreaks) streaks_draw(&st->streaks, &p->streaks, ctx);
    if (p->showCore) draw_core(st, ctx);
}

static void vortex_unload(Environment *env)
{
    VortexState *st = (VortexState *)env->state;
    streaks_unload(&st->streaks);
    if (st->sl) { st->mat.shader.id = rlGetShaderIdDefault(); UnloadMaterial(st->mat); }
    free(st);
    env->state = NULL;
}

Environment env_vortex_create(void)
{
    Environment e;
    memset(&e, 0, sizeof(e));
    e.name = "Data Vortex";
    e.id = ENV_VORTEX;
    e.state = calloc(1, sizeof(VortexState));
    e.pathStyle = vortex_path_style;
    e.init = vortex_init;
    e.registerParams = vortex_register;
    e.reseed = vortex_reseed;
    e.buildChunk = vortex_build_chunk;
    e.uploadChunk = vortex_upload;
    e.releaseChunk = vortex_release;
    e.energyAt = vortex_energy_at;
    e.update = vortex_update;
    e.drawBackground = vortex_draw_background;
    e.drawOpaque = vortex_draw_opaque;
    e.drawEmissive = vortex_draw_emissive;
    e.chunkLabel = vortex_chunk_label;
    e.unload = vortex_unload;
    return e;
}
