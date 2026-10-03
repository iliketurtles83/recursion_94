// Module internals shared between cybertunnel.c and debug_ui.c. Not public API.
#ifndef CT_INTERNAL_H
#define CT_INTERNAL_H

#include "cybertunnel.h"
#include "core/path.h"
#include "core/chunks.h"
#include "core/flight.h"
#include "core/params.h"
#include "render/pipeline.h"
#include "render/shaderlib.h"
#include "env/env.h"

#define CT_VIEW_DIST     400.0
#define CT_REBASE_DIST   1024.0

typedef struct CoreParams {
    float timeScale;
    float intensityOverride;     // < 0 = automatic
    float intensityResponse;
    float whiteoutDuration;
    int   buildBudget, uploadBudget;
    bool  showShip, showPathDebug;
    float engineIntensity, trailIntensity, trailWidth;
    float coreR, coreG, coreB;   // core tint (sRGB 0..1)
} CoreParams;

struct CtTunnel {
    CtConfig       cfg;
    char           shaderDir[512], presetDir[512];
    uint64_t       seed;

    ParamRegistry  reg;
    int            envParamStart;
    CoreParams     core;
    Atmosphere     atmo;
    PostSettings   post;
    FlightParams   fp;

    PathSystem     path;
    ChunkManager   chunks;
    Environment    envs[ENV_COUNT];
    int            envId;
    Environment   *env;

    FlightState    prev, cur;
    FlightInput    input;
    FlightView     view;
    double         acc, simTime;
    bool           paused;

    ShaderLib      sl;
    Pipeline       pipe;
    int            shShip, shLine;
    Material       mat;
    Mesh           shipHull, shipEdges, trailMesh;
    LineGeo        shipHullGeo, shipEdgesGeo, trailGeo;

    DVec3          renderOrigin;
    float          maxRenderCoord;
    FrameContext   ctx;
    RenderStats    rstats;
    Vector2        vanishUV;
    float          intensity;
    float          whiteoutT;    // < 0 idle
    float          whiteoutManual;
    float          whiteout, flare;

    // external camera
    bool           extCamValid;
    DVec3          extCamPos, extCamTarget;
    Vector3        extCamUp;
    float          extCamFov;

    // debug
    bool           freeCam;
    Camera3D       freeCamera;   // render space
    bool           showPanel, showOverlay;
    bool           wantScreenshot;
    bool           rebuildPending, rebuildPathPending;
    Vector2        panelScroll;
    bool           groupOpen[32];
    float          jumpTarget;
    double         msUpdate;
    float          frameMs;
};

void ct_debug_panel(CtTunnel *ct, int width, int height);
void ct_debug_overlay(CtTunnel *ct, int width, int height);

#endif
