// Environment #2: "Neon Grid City" — open flight low over an infinite neon grid floor
// between circuit-traced monolith towers. See PLAN_ENV2_GRID_CITY.md.
//
// Phases done: A (path style: level, no twist, floor below the centerline; atmosphere),
// B (grid floor, sky), C (tower placement and bodies, city_towers.c), D (circuit traces
// and code-text panels, city_decor.c; light rain; floating props), E (floor reflection) and
// F (pacing archetypes AVENUE / DOWNTOWN / OPEN / PLAZA).
#include "env_common.h"
#include "city_towers.h"
#include "city_decor.h"
#include "city_landmarks.h"
#include "core/rng.h"
#include "core/pacing.h"
#include "render/gridfloor.h"
#include "render/shapes.h"
#include "render/streaks.h"
#include "render/rmath.h"
#include "render/pipeline.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { CB_BODY = 0, CB_EDGES, CB_TRACES, CB_GLYPHS, CB_PROP_EDGES, CB_PROP_FACES };   // chunk batches

// Per-chunk uniform layout for props (same convention as the vortex debris: object.vs / face.vs).
enum { U_OBJ_A = 0, U_OBJ_B = 16, U_OBJ_C = 32 };
#define MAX_PROPS   8
#define RAIN_COUNT  1200

enum { F_PROPS = 3, F_PACING = 4 };   // city feature sub-streams (towers 1, traces 2)

// Pacing archetypes: a hash-driven Markov chain over runs of chunks, as in the vortex.
enum { CA_AVENUE, CA_DOWNTOWN, CA_OPEN, CA_PLAZA, CA_COUNT, CA_DEFAULT = CA_COUNT };
static const char *CA_NAMES[CA_COUNT + 1] = { "AVENUE", "DOWNTOWN", "OPEN", "PLAZA", "DEFAULT" };
//                                densN  densF  clear  height hFar  landm traces glyphs props energy
static const CityArch CA_TABLE[CA_COUNT + 1] = {
    [CA_AVENUE]   = { 1.60f, 0.80f,  0.0f, 1.00f, 0.8f, 0.6f, 1.00f, 1.10f, 0.7f, 0.45f },   // two tidy rows lining the lane
    [CA_DOWNTOWN] = { 1.50f, 2.00f,  0.0f, 1.35f, 1.5f, 2.5f, 1.30f, 1.60f, 1.0f, 0.85f },   // dense, tall, glyph-heavy
    [CA_OPEN]     = { 0.30f, 0.70f,  8.0f, 0.55f, 0.5f, 0.5f, 0.70f, 0.60f, 0.7f, 0.25f },   // sparse and low, long grid vistas
    [CA_PLAZA]    = { 0.00f, 1.20f, 26.0f, 0.90f, 1.0f, 1.0f, 1.00f, 1.00f, 2.0f, 0.35f },   // open square, props, a breather
    [CA_DEFAULT]  = { 1.00f, 1.00f,  0.0f, 1.00f, 1.0f, 1.0f, 1.00f, 1.00f, 1.0f, 0.50f },
};
static const int CA_RUN_MIN[CA_COUNT] = { 4, 3, 3, 2 };
static const int CA_RUN_MAX[CA_COUNT] = { 9, 6, 6, 3 };
static const float CA_TRANSITION[CA_COUNT][CA_COUNT] = {
    { 0.00f, 0.45f, 0.30f, 0.25f },
    { 0.45f, 0.00f, 0.20f, 0.35f },
    { 0.55f, 0.30f, 0.00f, 0.15f },
    { 0.50f, 0.35f, 0.15f, 0.00f },
};

typedef struct CityParams {
    PathStyle path;
    GridFloorParams floor;
    CityTowerParams towers;
    float edgeIntensity, edgeWidth;
    float bodyBrightness, rimIntensity, underglow, panelW, panelH, seamDark, recessed;
    bool  showTowers;
    CityDecorParams decor;
    bool  showTraces, showGlyphs;
    float pulseSpeed, pulseAmount;
    StreakParams rain;
    float rainYMin, rainYMax;        // above the floor
    bool  showRain;
    int   propsMax;
    float propSizeMin, propSizeMax, propHeightMax, propEdgeIntensity, propFaceIntensity;
    bool  showProps;
    bool  pacing;
    float fogStart, fogEnd;          // fog for chunk content (towers etc.)
    float hazeWidth, hazeGlow, dust; // sky
    bool  showFloor, showSky, landmarks;
} CityParams;

typedef struct CityState {
    uint64_t   seed;
    CityParams p;
    ShaderLib *sl;
    int        shSky, shTower, shLines, shTrace, shGlyph, shFace;
    Material   mat;
    GridFloor  floor;
    StreakSystem rain;
    TowerDesc  towers[CITY_MAX_TOWERS];   // build scratch (builds are single-threaded)
} CityState;

static const Color COL_CYAN    = { 0x42, 0xAD, 0xCC, 255 };
static const Color COL_FLOOR   = { 0x0B, 0x15, 0x20, 255 };
static const Color COL_HORIZON = { 0x21, 0x43, 0x56, 255 };
static const Color COL_ZENITH  = { 0x09, 0x12, 0x21, 255 };
static const Color COL_PINK    = { 0xCB, 0x79, 0xB1, 255 };
static const Color COL_VIOLET  = { 0x78, 0xA3, 0xD6, 255 };
static const Color COL_PALE    = { 0xA8, 0xEE, 0xFF, 255 };

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
    params_float(reg, g, "gridNodeBoost", &f->nodeBoost, 2.0f, 0.0f, 10.0f);
    params_float(reg, g, "gridWhiteCore", &f->whiteCore, 0.25f, 0.0f, 1.0f);
    params_float(reg, g, "floorFogStart", &f->fogStart, 40.0f, 0.0f, 1000.0f);
    params_float(reg, g, "floorFogEnd", &f->fogEnd, 450.0f, 1.0f, 1500.0f);
    params_float(reg, g, "floorSheen", &f->sheen, 0.3f, 0.0f, 3.0f);
    params_bool(reg, g, "gridFilter", &f->filter, true);
    params_float(reg, g, "gridNodeSize", &f->nodeSize, 0.12f, 0.0f, 1.0f);
    params_float(reg, g, "floorReflect", &f->reflectStrength, 1.3f, 0.0f, 3.0f);
    params_float(reg, g, "floorReflectR0", &f->reflectR0, 0.3f, 0.0f, 1.0f);
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

    g = "Decor";
    CityDecorParams *d = &p->decor;
    params_bool(reg, g, "showTraces", &p->showTraces, true);
    params_bool(reg, g, "showGlyphs", &p->showGlyphs, true);
    params_float(reg, g, "tracePitch", &d->tracePitch, 0.55f, 0.2f, 2.0f);
    params_float(reg, g, "traceWidth", &d->traceWidth, 0.045f, 0.005f, 0.3f);
    params_float(reg, g, "traceIntensity", &d->traceIntensity, 1.8f, 0.0f, 10.0f);
    params_float(reg, g, "traceBuses", &d->traceBuses, 2.2f, 0.0f, 6.0f);
    params_float(reg, g, "traceFar", &d->traceFar, 0.35f, 0.0f, 1.0f);
    params_float(reg, g, "traceStop", &d->traceStop, 0.1f, 0.0f, 1.0f);
    params_float(reg, g, "tracePulse", &d->tracePulse, 0.08f, 0.0f, 1.0f);
    params_float(reg, g, "tracePulseSpeed", &p->pulseSpeed, 6.0f, 0.0f, 40.0f);
    params_float(reg, g, "tracePulseAmount", &p->pulseAmount, 2.5f, 0.0f, 10.0f);
    params_float(reg, g, "viaRadius", &d->viaRadius, 0.11f, 0.02f, 0.5f);
    params_float(reg, g, "glyphStripChance", &d->glyphStripChance, 0.55f, 0.0f, 1.0f);
    params_float(reg, g, "glyphBlockChance", &d->glyphBlockChance, 0.25f, 0.0f, 1.0f);
    params_float(reg, g, "glyphIntensity", &d->glyphIntensity, 1.2f, 0.0f, 10.0f);
    params_float(reg, g, "glyphScroll", &d->glyphScroll, 0.6f, 0.0f, 10.0f);
    params_float(reg, g, "decorOffset", &d->faceOffset, 0.08f, 0.01f, 0.5f);

    g = "Rain";
    StreakParams *rn = &p->rain;
    params_bool(reg, g, "showRain", &p->showRain, true);
    params_float(reg, g, "rainDensity", &rn->density, 0.3f, 0.0f, 1.0f);
    params_float(reg, g, "rainLenMul", &rn->lenMul, 2.8f, 0.1f, 10.0f);
    params_float(reg, g, "rainWMin", &rn->wMin, 0.05f, 0.005f, 0.5f);
    params_float(reg, g, "rainWMax", &rn->wMax, 0.15f, 0.005f, 0.5f);
    params_float(reg, g, "rainIntensity", &rn->intensity, 2.6f, 0.0f, 10.0f);
    params_float(reg, g, "rainCometFrac", &rn->cometFrac, 0.05f, 0.0f, 0.5f);
    params_float(reg, g, "rainZRange", &rn->zRange, 170.0f, 40.0f, 400.0f);
    params_float(reg, g, "rainHalfWidth", &rn->slabHalfWidth, 45.0f, 5.0f, 200.0f);
    params_float(reg, g, "rainYMin", &p->rainYMin, 0.8f, 0.0f, 50.0f);
    params_float(reg, g, "rainYMax", &p->rainYMax, 25.0f, 0.0f, 100.0f);
    params_float(reg, g, "rainFallTilt", &rn->fallTilt, 0.12f, 0.0f, 1.0f);
    params_float(reg, g, "rainCenterGap", &rn->slabGap, 12.0f, 0.0f, 100.0f);
    params_float(reg, g, "rainCenterFrac", &rn->slabGapFrac, 0.05f, 0.0f, 1.0f);
    params_float(reg, g, "rainSpeedMul", &rn->speedMul, 1.0f, 0.0f, 4.0f);
    rn->zBehind = 12.0f;
    rn->volume = 1;
    rn->colMain = (Color){ 0x8A, 0xF2, 0xFF, 255 };
    rn->colAccent = COL_VIOLET;

    g = "Props";
    params_bool(reg, g, "showProps", &p->showProps, true);
    params_int(reg, g, "propsMax", &p->propsMax, 3, 0, MAX_PROPS);
    params_float(reg, g, "propSizeMin", &p->propSizeMin, 0.4f, 0.1f, 4.0f);
    params_float(reg, g, "propSizeMax", &p->propSizeMax, 1.2f, 0.1f, 4.0f);
    params_float(reg, g, "propHeightMax", &p->propHeightMax, 4.0f, 0.5f, 30.0f);
    params_float(reg, g, "propEdgeIntensity", &p->propEdgeIntensity, 2.2f, 0.0f, 10.0f);
    params_float(reg, g, "propFaceIntensity", &p->propFaceIntensity, 0.25f, 0.0f, 3.0f);

    g = "Pacing";
    params_bool(reg, g, "cityPacing", &p->pacing, true);

    g = "City";
    params_bool(reg, g, "showSky", &p->showSky, true);
    params_bool(reg, g, "cityLandmarks", &p->landmarks, false);
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
        st->shTrace = sl_load(sl, "trace.vs", "trace.fs");
        st->shGlyph = sl_load(sl, "glyph.vs", "glyph.fs");
        st->shFace = sl_load(sl, "face.vs", "face.fs");
        st->mat = LoadMaterialDefault();
    }
    streaks_init(&st->rain, sl, RAIN_COUNT);
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

static const PacingTable CA_PACING = { CA_COUNT, CA_AVENUE, CA_RUN_MIN, CA_RUN_MAX, &CA_TRANSITION[0][0] };

static int city_archetype_of_chunk(const CityState *st, int k)
{
    if (!st->p.pacing) return CA_DEFAULT;
    // Every flight starts on an avenue (the reference opening).
    return pacing_archetype(&CA_PACING, ct_hash64_3(st->seed, ENV_CITY, F_PACING), k);
}

// Blend for chunk k: the previous chunk's archetype fades into this one across chunk k.
static CityArchBlend city_blend(const CityState *st, int k)
{
    CityArchBlend b;
    b.prev = CA_TABLE[city_archetype_of_chunk(st, k - 1)];
    b.cur = CA_TABLE[city_archetype_of_chunk(st, k)];
    b.s0 = k * CHUNK_LEN;
    b.len = CHUNK_LEN;
    return b;
}

static void set_u(ChunkCpu *c, int slot, Vector3 v, float w)
{
    c->uniforms[slot][0] = v.x; c->uniforms[slot][1] = v.y; c->uniforms[slot][2] = v.z; c->uniforms[slot][3] = w;
}

// Small slowly turning wireframe props floating low between the lane edge and the towers.
static void build_props(const CityState *st, const PathSystem *path, int chunkIndex, int propsMax, ChunkCpu *c)
{
    const CityParams *p = &st->p;
    Pcg32 r = pcg32_seed(ct_hash64_3(st->seed, ((uint64_t)ENV_CITY << 8) | F_PROPS, (uint64_t)(int64_t)chunkIndex));
    if (propsMax > MAX_PROPS) propsMax = MAX_PROPS;
    int count = pcg32_int(&r, 0, propsMax);
    float lane = path->style.baseRadius, gapEnd = lane + p->towers.clearance;
    for (int g = 0; g < count && g < MAX_PROPS; g++) {
        double s = c->s0 + pcg32_float(&r) * CHUNK_LEN;
        PathSample ps = path_sample(path, s);
        float size = pcg32_range(&r, p->propSizeMin, p->propSizeMax);
        float h = size * 0.5f, bound = h * 1.7320508f;
        float side = pcg32_chance(&r, 0.5f) ? 1.0f : -1.0f;
        float lat = pcg32_range(&r, lane + 0.5f + bound, fmaxf(gapEnd - bound, lane + 0.5f + bound));
        float y = pcg32_range(&r, 0.4f + bound, fmaxf(p->propHeightMax, 0.4f + bound)) - path->style.floorDepth;
        DVec3 ctr = dv_add(ps.pos, dv_add(dv_scale(ps.B, side * lat), dv_scale(ps.N, y)));
        Vector3 axis = Vector3Normalize((Vector3){ pcg32_range(&r, -1, 1), pcg32_range(&r, 0.3f, 1), pcg32_range(&r, -1, 1) });
        set_u(c, U_OBJ_A + g, env_local(ctr, c->origin), size);
        set_u(c, U_OBJ_B + g, axis, pcg32_range(&r, 0.15f, 0.6f) * (pcg32_chance(&r, 0.5f) ? 1.0f : -1.0f));
        set_u(c, U_OBJ_C + g, (Vector3){ pcg32_float(&r) * 6.2831853f, 0, 0 }, 0.0f);
        LineAttr e = { COL_PALE, p->propEdgeIntensity, pcg32_float(&r), 0.0f, (float)g, 0.0f };
        LineAttr f = { COL_CYAN, p->propFaceIntensity, pcg32_float(&r), 0.0f, (float)g, 0.0f };
        float w = 0.035f;
        float kind = pcg32_float(&r);
        if (kind < 0.5f) shape_cube(&c->geo[CB_PROP_EDGES], &c->geo[CB_PROP_FACES], h, e, f, w);
        else if (kind < 0.75f) shape_octa(&c->geo[CB_PROP_EDGES], &c->geo[CB_PROP_FACES], h * 1.3f, e, f, w);
        else shape_sphere(&c->geo[CB_PROP_EDGES], h * 1.2f, e, w);
        if (c->debrisCount < CHUNK_MAX_DEBRIS) {
            DebrisInfo *d = &c->debris[c->debrisCount++];
            d->center = ctr; d->radius = bound; d->s = s;
        }
    }
}

static void city_build_chunk(Environment *env, const PathSystem *path, int chunkIndex, ChunkCpu *out)
{
    CityState *st = (CityState *)env->state;
    CityTowerParams tp = tower_params(st, path);
    CityArchBlend blend = city_blend(st, chunkIndex);
    out->archetype = city_archetype_of_chunk(st, chunkIndex);
    int n = city_towers_plan(&tp, &blend, st->seed, path, chunkIndex, st->towers, CITY_MAX_TOWERS, NULL, 0, NULL);
    city_towers_build(st->towers, n, out->origin, &out->geo[CB_BODY], &out->geo[CB_EDGES], st->p.edgeIntensity, st->p.edgeWidth);
    CityDecorParams dp = st->p.decor;
    dp.inner = tp.laneHalfWidth + tp.clearance;
    dp.bandMax = tp.bandMax;
    city_decor_build(&dp, st->towers, n, path, out->origin, &out->geo[CB_TRACES], &out->geo[CB_GLYPHS]);
    build_props(st, path, chunkIndex, (int)lroundf((float)st->p.propsMax * blend.cur.props), out);
    if(st->p.landmarks) city_landmarks_build(path,chunkIndex,st->p.path.floorDepth,out);
}

static void city_upload(Environment *env, ChunkCpu *in, ChunkGpu *out) { chunk_upload_default(in, out); }
static void city_release(Environment *env, ChunkGpu *gpu) { chunk_release_default(gpu); }
static float city_energy(const Environment *env, const PathSystem *path, double s)
{
    const CityState *st = (const CityState *)env->state;
    CityArchBlend b = city_blend(st, chunk_index_at(s));
    return city_arch_at(&b, s).energy;
}

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
            if (batch != CB_BODY) sl_f1(st->sl, sh, "uChunkFade", env_chunk_fade(c, ctx->time));
            if (batch == CB_PROP_EDGES || batch == CB_PROP_FACES) {
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

static void draw_towers(CityState *st, const FrameContext *ctx, const ChunkGpu *const *chunks, int n)
{
    const CityParams *p = &st->p;
    ShaderLib *sl = st->sl;
    int sh = st->shTower;
    env_set_common_uniforms(sl, sh, ctx);
    Vector3 body = ct_srgb_to_linear((Color){ 0x35, 0x49, 0x5B, 255 });
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
    if (!st->p.showFloor || ctx->reflectionPass) return;
    // The floor fogs into exactly the sky's horizon colour.
    GridFloorParams fp = st->p.floor;
    fp.horizonGlow = st->p.hazeGlow;
    double floorY = path_sample(ctx->path, ctx->sShip).pos.y - ctx->path->style.floorDepth;
    gridfloor_draw(&st->floor, &fp, ctx, floorY);
}

static void city_draw_emissive(Environment *env, const FrameContext *ctx, const ChunkGpu *const *chunks, int n)
{
    CityState *st = (CityState *)env->state;
    const CityParams *p = &st->p;
    ShaderLib *sl = st->sl;
    int sh = st->shLines;
    env_set_common_uniforms(sl, sh, ctx);
    sl_f1(sl, sh, "uEdgeMix", 0.0f);
    sl_f1(sl, sh, "uFlicker", 0.0f);
    // The blurred reflection cannot show the dim tower outlines or the faint prop faces:
    // skip them there (saves ~2 draw calls per chunk).
    bool refl = ctx->reflectionPass;
    if (p->showTowers && !refl) draw_batch(st, ctx, sh, chunks, n, CB_EDGES);
    if (p->showProps) {
        sl_f1(sl, sh, "uFlicker", 0.25f);
        draw_batch(st, ctx, sh, chunks, n, CB_PROP_EDGES);
        if (!refl) {
            env_set_common_uniforms(sl, st->shFace, ctx);
            sl_f1(sl, st->shFace, "uDataRate", 1.2f);
            draw_batch(st, ctx, st->shFace, chunks, n, CB_PROP_FACES);
        }
    }
    if (p->showTowers && p->showGlyphs) {
        int g = st->shGlyph;
        env_set_common_uniforms(sl, g, ctx);
        sl_v3(sl, g, "uPink", ct_srgb_to_linear(COL_PINK));
        sl_v3(sl, g, "uViolet", ct_srgb_to_linear(COL_VIOLET));
        sl_v3(sl, g, "uCyan", ct_srgb_to_linear(COL_PALE));
        draw_batch(st, ctx, g, chunks, n, CB_GLYPHS);
    }
    if (p->showTowers && p->showTraces) {
        int t = st->shTrace;
        env_set_common_uniforms(sl, t, ctx);
        sl_f1(sl, t, "uPulseSpeed", p->pulseSpeed);
        sl_f1(sl, t, "uPulseAmount", p->pulseAmount);
        draw_batch(st, ctx, t, chunks, n, CB_TRACES);
    }
    if (p->showRain) {
        StreakParams rp = p->rain;
        float fd = ctx->path->style.floorDepth;
        rp.slabYMin = p->rainYMin - fd;
        rp.slabYMax = p->rainYMax - fd;
        streaks_draw(&st->rain, &rp, ctx);
    }
}

static const char *city_label(const Environment *env, int archetype)
{
    return (archetype >= 0 && archetype <= CA_COUNT) ? CA_NAMES[archetype] : "?";
}

static void city_unload(Environment *env)
{
    CityState *st = (CityState *)env->state;
    streaks_unload(&st->rain);
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
    e.reflectiveFloor = true;
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
