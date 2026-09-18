#include "raylib.h"
#include "rlgl.h"
#include "palette.h"
#include "environment.h"
#include "audio_synth.h"
#include "gameplay.h"
#include "demoscene.h"
#include "shader_sources.h"
#include "player_ui.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if !defined(RECURSION_VALIDATE_GENERATOR) && !defined(RECURSION_VALIDATE_GAMEPLAY)
typedef struct {
    RenderTexture2D target;
    Shader shader;
    int timeLoc;
    int intensityLoc;
    int beatLoc;
    int bossTransitionLoc;
    int seedLoc;
    int primaryLoc;
    int secondaryLoc;
    int depthLoc;
    int contactEnabledLoc;
    int clipLoc;
    int projectionLoc;
    bool sampleableDepth;
    Shader craftShader;
    int craftTimeLoc;
    int craftIntensityLoc;
    int craftObjectClassLoc;
    int craftEnemyTypeLoc;
    int craftOriginLoc;
    int craftViewLoc;
    int craftPrimaryLoc;
    int craftSecondaryLoc;
    int craftThreatLoc;
    Shader backdropShader;
    int backdropTimeLoc;
    int backdropIntensityLoc;
    int backdropSeedLoc;
    int backdropPrimaryLoc;
    int backdropSecondaryLoc;
    int backdropZenithLoc;
    int backdropHorizonLoc;
    int backdropBodyLoc;
    int backdropEncounterLoc;
    int backdropResolutionLoc;
    bool ready;
} PostProcessSystem;

static uint32_t DeriveLoopSeed(uint32_t initialSeed, int completedLoops) {
    uint32_t value = initialSeed ^ (uint32_t)completedLoops * UINT32_C(0x9e3779b9);
    value ^= value >> 16;
    value *= UINT32_C(0x7feb352d);
    value ^= value >> 15;
    value *= UINT32_C(0x846ca68b);
    value ^= value >> 16;
    return value == initialSeed ? value ^ UINT32_C(0xa5a5a5a5) : value;
}

static void SetPostProcessTheme(PostProcessSystem *post, uint32_t seed,
                                const DemosceneSystem *demo) {
    int shaderSeed = (int)seed;
    Vector3 primaryValue = { demo->primary.r / 255.0f, demo->primary.g / 255.0f, demo->primary.b / 255.0f };
    Vector3 secondaryValue = { demo->secondary.r / 255.0f, demo->secondary.g / 255.0f,
                               demo->secondary.b / 255.0f };
    Vector3 threatValue = { demo->threat.r / 255.0f, demo->threat.g / 255.0f, demo->threat.b / 255.0f };
    Vector3 bodyValue = { demo->shadowBody.r / 255.0f, demo->shadowBody.g / 255.0f, demo->shadowBody.b / 255.0f };
    Vector3 zenithValue = { demo->skyZenith.r / 255.0f, demo->skyZenith.g / 255.0f, demo->skyZenith.b / 255.0f };
    Vector3 horizonValue = { demo->skyHorizon.r / 255.0f, demo->skyHorizon.g / 255.0f, demo->skyHorizon.b / 255.0f };

    SetShaderValue(post->shader, post->seedLoc, &shaderSeed, SHADER_UNIFORM_INT);
    SetShaderValue(post->shader, post->primaryLoc, &primaryValue, SHADER_UNIFORM_VEC3);
    SetShaderValue(post->shader, post->secondaryLoc, &secondaryValue, SHADER_UNIFORM_VEC3);

    SetShaderValue(post->craftShader, post->craftPrimaryLoc, &primaryValue, SHADER_UNIFORM_VEC3);
    SetShaderValue(post->craftShader, post->craftSecondaryLoc, &secondaryValue, SHADER_UNIFORM_VEC3);
    if (post->craftThreatLoc >= 0) {
        SetShaderValue(post->craftShader, post->craftThreatLoc, &threatValue, SHADER_UNIFORM_VEC3);
    }

    SetShaderValue(post->backdropShader, post->backdropSeedLoc, &shaderSeed, SHADER_UNIFORM_INT);
    SetShaderValue(post->backdropShader, post->backdropPrimaryLoc, &primaryValue, SHADER_UNIFORM_VEC3);
    SetShaderValue(post->backdropShader, post->backdropSecondaryLoc, &secondaryValue, SHADER_UNIFORM_VEC3);
    if (post->backdropZenithLoc >= 0) {
        SetShaderValue(post->backdropShader, post->backdropZenithLoc, &zenithValue, SHADER_UNIFORM_VEC3);
    }
    if (post->backdropHorizonLoc >= 0) {
        SetShaderValue(post->backdropShader, post->backdropHorizonLoc, &horizonValue, SHADER_UNIFORM_VEC3);
    }
    if (post->backdropBodyLoc >= 0) {
        SetShaderValue(post->backdropShader, post->backdropBodyLoc, &bodyValue, SHADER_UNIFORM_VEC3);
    }
}

static RenderTexture2D LoadSceneTarget(int width, int height, bool *sampleable) {
    RenderTexture2D target = {0};
    *sampleable = false;
    target.id = rlLoadFramebuffer();
    if (target.id) {
        target.texture = (Texture2D){rlLoadTexture(NULL,width,height,PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,1),
                                    width,height,1,PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
        target.depth = (Texture2D){rlLoadTextureDepth(width,height,false),width,height,1,19};
        rlFramebufferAttach(target.id,target.texture.id,RL_ATTACHMENT_COLOR_CHANNEL0,RL_ATTACHMENT_TEXTURE2D,0);
        rlFramebufferAttach(target.id,target.depth.id,RL_ATTACHMENT_DEPTH,RL_ATTACHMENT_TEXTURE2D,0);
        if (rlFramebufferComplete(target.id)) {
            *sampleable = true;
            SetTextureFilter(target.depth,TEXTURE_FILTER_POINT);
            SetTextureWrap(target.depth,TEXTURE_WRAP_CLAMP);
            TraceLog(LOG_INFO,"RENDER: sampleable scene depth ready (%dx%d)",width,height);
            return target;
        }
        UnloadRenderTexture(target); // Raylib owns and deletes the attached depth texture.
    }
    TraceLog(LOG_WARNING,"RENDER: contact shading unavailable; using standard scene target");
    return LoadRenderTexture(width,height);
}

static void InitPostProcess(PostProcessSystem *post, int width, int height,
                            uint32_t seed, const DemosceneSystem *demo) {
    post->target = LoadSceneTarget(width, height, &post->sampleableDepth);
    SetTextureFilter(post->target.texture, TEXTURE_FILTER_BILINEAR);
    post->shader = LoadShaderFromMemory(NULL, (const char *)shader_post_fs);
    post->timeLoc = GetShaderLocation(post->shader, "uTime");
    post->intensityLoc = GetShaderLocation(post->shader, "uIntensity");
    post->beatLoc = GetShaderLocation(post->shader, "uBeatPulse");
    post->bossTransitionLoc = GetShaderLocation(post->shader, "uBossTransition");
    int resolutionLoc = GetShaderLocation(post->shader, "uResolution");
    post->seedLoc = GetShaderLocation(post->shader, "uRunSeed");
    post->primaryLoc = GetShaderLocation(post->shader, "uPrimaryColor");
    post->secondaryLoc = GetShaderLocation(post->shader, "uSecondaryColor");
    post->depthLoc = GetShaderLocation(post->shader, "uSceneDepth");
    post->contactEnabledLoc = GetShaderLocation(post->shader, "uContactEnabled");
    post->clipLoc = GetShaderLocation(post->shader, "uClipPlanes");
    post->projectionLoc = GetShaderLocation(post->shader, "uProjectionScale");
    int contactEnabled = post->sampleableDepth ? 1 : 0;
    SetShaderValue(post->shader,post->contactEnabledLoc,&contactEnabled,SHADER_UNIFORM_INT);
    Vector2 clip = {(float)rlGetCullDistanceNear(),(float)rlGetCullDistanceFar()};
    SetShaderValue(post->shader,post->clipLoc,&clip,SHADER_UNIFORM_VEC2);
    Vector2 resolution = { (float)width, (float)height };
    SetShaderValue(post->shader, resolutionLoc, &resolution, SHADER_UNIFORM_VEC2);

    post->craftShader = LoadShaderFromMemory((const char *)shader_craft_vs, (const char *)shader_craft_fs);
    post->craftTimeLoc = GetShaderLocation(post->craftShader, "uTime");
    post->craftIntensityLoc = GetShaderLocation(post->craftShader, "uIntensity");
    post->craftObjectClassLoc = GetShaderLocation(post->craftShader, "uObjectClass");
    post->craftEnemyTypeLoc = GetShaderLocation(post->craftShader, "uEnemyType");
    post->craftOriginLoc = GetShaderLocation(post->craftShader, "uObjectOrigin");
    post->craftViewLoc = GetShaderLocation(post->craftShader, "uViewPosition");
    post->craftPrimaryLoc = GetShaderLocation(post->craftShader, "uPrimaryColor");
    post->craftSecondaryLoc = GetShaderLocation(post->craftShader, "uSecondaryColor");
    post->craftThreatLoc = GetShaderLocation(post->craftShader, "uThreatColor");

    post->backdropShader = LoadShaderFromMemory(NULL, (const char *)shader_backdrop_fs);
    post->backdropTimeLoc = GetShaderLocation(post->backdropShader, "uTime");
    post->backdropIntensityLoc = GetShaderLocation(post->backdropShader, "uIntensity");
    post->backdropResolutionLoc = GetShaderLocation(post->backdropShader, "uResolution");
    post->backdropSeedLoc = GetShaderLocation(post->backdropShader, "uRunSeed");
    post->backdropPrimaryLoc = GetShaderLocation(post->backdropShader, "uPrimaryColor");
    post->backdropSecondaryLoc = GetShaderLocation(post->backdropShader, "uSecondaryColor");
    post->backdropZenithLoc = GetShaderLocation(post->backdropShader, "uSkyZenith");
    post->backdropHorizonLoc = GetShaderLocation(post->backdropShader, "uSkyHorizon");
    post->backdropBodyLoc = GetShaderLocation(post->backdropShader, "uBodyColor");
    post->backdropEncounterLoc = GetShaderLocation(post->backdropShader, "uEncounterIndex");
    SetShaderValue(post->backdropShader, post->backdropResolutionLoc, &resolution, SHADER_UNIFORM_VEC2);
    SetPostProcessTheme(post, seed, demo);

    post->ready = post->target.texture.id != 0 && post->shader.id != 0 &&
                  post->craftShader.id != 0 && post->backdropShader.id != 0;
}

static void BeginCraftPass(PostProcessSystem *post, float time, float intensity, Vector3 eye) {
    SetShaderValue(post->craftShader, post->craftViewLoc, &eye, SHADER_UNIFORM_VEC3);
    SetShaderValue(post->craftShader, post->craftTimeLoc, &time, SHADER_UNIFORM_FLOAT);
    SetShaderValue(post->craftShader, post->craftIntensityLoc, &intensity,
                   SHADER_UNIFORM_FLOAT);
    BeginShaderMode(post->craftShader);
}

static void DrawRaymarchBackdrop(PostProcessSystem *post, float time, float intensity,
                                  int encounterIndex, int width, int height) {
    SetShaderValue(post->backdropShader, post->backdropTimeLoc, &time, SHADER_UNIFORM_FLOAT);
    SetShaderValue(post->backdropShader, post->backdropIntensityLoc, &intensity,
                   SHADER_UNIFORM_FLOAT);
    SetShaderValue(post->backdropShader, post->backdropEncounterLoc, &encounterIndex,
                   SHADER_UNIFORM_INT);
    Vector2 res = { (float)width, (float)height };
    SetShaderValue(post->backdropShader, post->backdropResolutionLoc, &res, SHADER_UNIFORM_VEC2);
    BeginShaderMode(post->backdropShader);
        DrawRectangle(0, 0, width, height, WHITE);
    EndShaderMode();
}

static void DrawPostProcess(PostProcessSystem *post, float time, float intensity,
                            float beatPulse, float bossTransition,
                            int width, int height, float fovy) {
    float tangent = tanf(fovy * 0.00872664626f);
    Vector2 projectionScale = {tangent*(float)width/(float)height,tangent};
    SetShaderValue(post->shader,post->projectionLoc,&projectionScale,SHADER_UNIFORM_VEC2);
    SetShaderValue(post->shader, post->timeLoc, &time, SHADER_UNIFORM_FLOAT);
    SetShaderValue(post->shader, post->intensityLoc, &intensity, SHADER_UNIFORM_FLOAT);
    SetShaderValue(post->shader, post->beatLoc, &beatPulse, SHADER_UNIFORM_FLOAT);
    SetShaderValue(post->shader, post->bossTransitionLoc, &bossTransition,
                   SHADER_UNIFORM_FLOAT);
    BeginShaderMode(post->shader);
        if (post->sampleableDepth) SetShaderValueTexture(post->shader,post->depthLoc,post->target.depth);
        DrawTextureRec(post->target.texture,
                       (Rectangle){ 0.0f, 0.0f, (float)width, (float)-height },
                       (Vector2){ 0.0f, 0.0f }, WHITE);
    EndShaderMode();
}

static void UnloadPostProcess(PostProcessSystem *post) {
    if (!post->ready) return;
    UnloadShader(post->craftShader);
    UnloadShader(post->backdropShader);
    UnloadShader(post->shader);
    UnloadRenderTexture(post->target);
    post->ready = false;
}

typedef enum { MENU_NONE, MENU_PAUSED, MENU_QUIT_CONFIRM, MENU_SETTINGS, MENU_UPGRADE } MenuMode;

static Rectangle PauseControl(int width, int height) {
    return (Rectangle){ (float)(width-128), (float)(height-92), 108.0f, 34.0f };
}

static Rectangle MenuControl(int width, int height, bool right) {
    float x=(float)width*0.5f+(right?11.0f:-173.0f);
    return (Rectangle){x,(float)height*0.5f+49.0f,162.0f,39.0f};
}

static Rectangle SettingsControl(int width,int height) {
    return (Rectangle){width*.5f-95,height*.5f+4,190,32};
}

static void DrawMenuControl(Rectangle bounds, const char *label, Color accent) {
    bool hovered=CheckCollisionPointRec(GetMousePosition(),bounds);
    DrawRectangleRec(bounds,hovered?(Color){37,55,67,245}:(Color){19,31,42,245});
    DrawRectangleLinesEx(bounds,1.0f,accent);
    DrawText(label,(int)(bounds.x+(bounds.width-MeasureText(label,17))*0.5f),
             (int)bounds.y+11,17,hovered?RAYWHITE:accent);
}

static void DrawPauseControl(int width, int height) {
    Rectangle bounds=PauseControl(width,height);
    DrawMenuControl(bounds,"II PAUSE",(Color){136,193,204,235});
}

static void DrawMenuOverlay(MenuMode mode,int width,int height,int selected) {
    DrawRectangle(0,0,width,height,(Color){2,5,11,190});
    int panelX=width/2-223, panelY=height/2-117;
    DrawRectangle(panelX,panelY,446,228,(Color){10,18,28,248});
    DrawRectangleLines(panelX,panelY,446,228,(Color){89,150,164,245});
    DrawRectangle(panelX+16,panelY+16,414,2,(Color){72,121,135,220});
    const char *heading=mode==MENU_PAUSED?"GAME PAUSED":"QUIT THE GAME?";
    const char *subtext=mode==MENU_PAUSED
        ? "THE RUN AND MUSIC ARE FROZEN"
        : "UNSAVED RUN PROGRESS WILL BE LOST";
    DrawText(heading,width/2-MeasureText(heading,27)/2,panelY+38,27,
             (Color){205,232,235,255});
    DrawText(subtext,width/2-MeasureText(subtext,15)/2,panelY+86,15,
             (Color){139,180,190,235});
    if(mode==MENU_PAUSED) {
        DrawMenuControl(SettingsControl(width,height),"SETTINGS / F2",(Color){173,197,182,255});
        DrawMenuControl(MenuControl(width,height,false),"RESUME",
                        (Color){125,211,197,255});
        DrawMenuControl(MenuControl(width,height,true),"QUIT...",
                        (Color){219,155,155,255});
        const char *hint="ARROWS / STICK + ENTER / A  //  ESC / B TO RESUME";
        DrawText(hint,width/2-MeasureText(hint,12)/2,panelY+207,12,
                 (Color){105,151,164,225});
    } else {
        DrawMenuControl(MenuControl(width,height,false),"YES, QUIT",
                        (Color){223,142,142,255});
        DrawMenuControl(MenuControl(width,height,true),"NO, RESUME",
                        (Color){125,211,197,255});
        const char *hint="CHOOSE + ENTER / A  //  Y: QUIT  //  ESC / B: BACK";
        DrawText(hint,width/2-MeasureText(hint,12)/2,panelY+207,12,
                 (Color){105,151,164,225});
    }
    Rectangle focused=mode==MENU_PAUSED&&selected==2?SettingsControl(width,height):MenuControl(width,height,selected==1);
    DrawRectangleLinesEx(focused,2,(Color){211,224,196,255});
}
#endif

int main(int argc, char **argv) {
#ifdef RECURSION_VALIDATE_GENERATOR
    (void)argc;
    (void)argv;
    EnvironmentValidationReport generationReport;
    bool generationValid = ValidateEnvironmentGenerator(&generationReport);
    TraceLog(generationValid ? LOG_INFO : LOG_ERROR,
             "GENERATOR: zones=%d repeats=%d landmark-spacing=%d field=%d clearance=%d middle=%d conduit=%d peak=%d terrain=%d far=%d unsupported=%d(flank=%d ravine=%d far=%d) dropped=%d",
             generationReport.zonesChecked,
             generationReport.adjacentRepeatViolations,
             generationReport.landmarkSpacingViolations,
             generationReport.fieldCoverageViolations,
             generationReport.crossingClearanceViolations,
             generationReport.middleClearanceViolations,
             generationReport.conduitFlowViolations,
             generationReport.peakStructureCount,
             generationReport.peakTerrainCount,
             generationReport.peakFarStructureCount,
             generationReport.unsupportedStructures,
             generationReport.unsupportedFlankStructures,
             generationReport.unsupportedRavineStructures,
             generationReport.unsupportedFarStructures,
             generationReport.droppedStructures);
    return generationValid ? 0 : 1;
#elif defined(RECURSION_VALIDATE_GAMEPLAY)
    (void)argc;
    (void)argv;
    bool classic=ValidateGameplaySpecials();
    bool expanded=ValidatePlayability();
    return classic && expanded ? 0 : 1;
#else
    const int screenWidth = 1280;
    const int screenHeight = 720;
    SetConfigFlags(FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(screenWidth, screenHeight, "RECURSION_94 - Procedural Cyberspace Trench");
    int renderWidth = GetScreenWidth();
    int renderHeight = GetScreenHeight();
    if (!IsWindowReady()) {
        TraceLog(LOG_ERROR, "Unable to initialize the graphics window");
        return 1;
    }
    // Raylib normally treats Escape as an unconditional window-close key.
    // Route it through our confirmation state instead.
    SetExitKey(KEY_NULL);
    SetWindowMinSize(960,600);
    InitPlayerPreferences();

    Camera3D camera = { 0 };
    camera.position = (Vector3){ 0.0f, 5.5f, 8.0f };
    camera.target = (Vector3){ 0.0f, 0.2f, -20.0f };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 72.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    // Heavy subsystems are initialized only after the player compiles a seed.
    EnvironmentSystem env = { 0 };
    SynthSystem synth = { 0 };
    GameplaySystem game = { 0 };
    DemosceneSystem demo = { 0 };
    PostProcessSystem post = { 0 };
    uint32_t selectedSeed = 94u;
    uint32_t activeSeed = selectedSeed;
    bool runStarted = false;
    bool bossTestStart = false;
    float selectorTime = 0.0f;

    // Optional deterministic shortcut for automated tests and exact run
    // sharing. Normal double-click launches still use the visual selector.
    if (argc >= 2 && (strcmp(argv[1], "--seed") == 0 ||
                      strcmp(argv[1], "--boss") == 0)) {
        char *end = NULL;
        unsigned long parsedSeed = argc >= 3 ? strtoul(argv[2], &end, 0) : 94ul;
        if (argc < 3 || end != argv[2]) {
            selectedSeed = (uint32_t)parsedSeed;
            activeSeed = selectedSeed;
            bossTestStart = strcmp(argv[1], "--boss") == 0;
            InitEnvironment(&env, activeSeed);
            InitAudioSynth(&synth, activeSeed);
            InitGameplay(&game, activeSeed);
            game.practice=bossTestStart;
            InitDemoscene(&demo, activeSeed);
            InitPostProcess(&post, renderWidth, renderHeight, activeSeed, &demo);
            runStarted = true;
        }
    }

    double virtualPlayerZ = bossTestStart ? game.boss.nextSpawnDistance + 1.0 : 0.0;
    double musicArrangementOriginZ = 0.0;
    float glitchAmount = 0.0f;
    float coreTransitionTimer = 0.0f;
    uint32_t pendingSeed = activeSeed;
    bool seedTransitionPending = false;
    bool seedTransitionCommitted = false;
    DemosceneSystem transitionFromDemo = { 0 };
    DemosceneSystem transitionToDemo = { 0 };
    bool debugOverlay = false;
    MenuMode menuMode = MENU_NONE;
    MenuMode settingsReturn=MENU_NONE,quitReturn=MENU_NONE;
    int menuSelection=0,upgradeSelection=0;
    bool audioPaused=false,recordedRun=false;
    bool quitConfirmed = false;
    float musicIntensity = 0.24f;
    float beatPulse = 0.0f;
    float currentSpeed = 0.0f;
    float bossTransition = 0.0f;
    SynthTelemetry audioTelemetry = { 0 };

    SetTargetFPS(60);

    while (!WindowShouldClose() && !quitConfirmed) {
        float dt = fminf(GetFrameTime(),.05f);
        PollPlayerControls();

        if (IsKeyPressed(KEY_F) && menuMode!=MENU_SETTINGS) ToggleFullscreen();
        if(IsKeyPressed(KEY_F1) && menuMode!=MENU_SETTINGS) {
            playerPreferences.tutorialDone=!playerPreferences.tutorialDone;
            ResetFlightTutorial(&game);
        }

        int currentWidth = GetScreenWidth();
        int currentHeight = GetScreenHeight();
        if (currentWidth != renderWidth || currentHeight != renderHeight) {
            renderWidth = currentWidth;
            renderHeight = currentHeight;
            if (runStarted) {
                UnloadPostProcess(&post);
                InitPostProcess(&post, renderWidth, renderHeight, activeSeed, &demo);
            }
        }

        bool clicked=IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
        Vector2 mouse=GetMousePosition();
        bool resumedThisFrame=false;
        if(runStarted && !game.gameOver && menuMode==MENU_NONE &&
           !IsWindowState(FLAG_WINDOW_HIDDEN) && !IsWindowFocused()) {
            menuMode=MENU_PAUSED; menuSelection=0;
        }
        if(menuMode==MENU_SETTINGS) {
            if(UpdatePlayerSettings()) { menuMode=settingsReturn; resumedThisFrame=true; SuppressPlayerFire(&game); }
        } else if(menuMode==MENU_QUIT_CONFIRM) {
            int direction=MenuHorizontal();
            if(direction) menuSelection=direction>0?1:0;
            if(IsKeyPressed(KEY_Y)||(MenuAccept()&&menuSelection==0)||
               (clicked&&CheckCollisionPointRec(mouse,MenuControl(renderWidth,renderHeight,false)))) {
                quitConfirmed=true;
                continue;
            }
            if(IsKeyPressed(KEY_N)||MenuBack()||(MenuAccept()&&menuSelection==1)||
               (clicked&&CheckCollisionPointRec(mouse,MenuControl(renderWidth,renderHeight,true)))) {
                menuMode=quitReturn;
                resumedThisFrame=true;
                SuppressPlayerFire(&game);
            }
        } else if(menuMode==MENU_PAUSED) {
            if(MenuHorizontal()) menuSelection=MenuHorizontal()>0?1:0;
            if(MenuVertical()) menuSelection=menuSelection==2?0:2;
            if(IsKeyPressed(KEY_F2)||(MenuAccept()&&menuSelection==2)||
               (clicked&&CheckCollisionPointRec(mouse,SettingsControl(renderWidth,renderHeight)))) {
                settingsReturn=MENU_PAUSED; menuMode=MENU_SETTINGS; OpenPlayerSettings();
            } else if((MenuAccept()&&menuSelection==1)||
               (clicked&&CheckCollisionPointRec(mouse,MenuControl(renderWidth,renderHeight,true)))) {
                quitReturn=MENU_NONE; menuMode=MENU_QUIT_CONFIRM; menuSelection=1;
            } else if(MenuBack()||PlayerActionPressed(ACT_PAUSE)||(MenuAccept()&&menuSelection==0)||
                      (clicked&&CheckCollisionPointRec(mouse,MenuControl(renderWidth,renderHeight,false)))) {
                menuMode=MENU_NONE;
                resumedThisFrame=true;
                SuppressPlayerFire(&game);
            }
        } else if(menuMode==MENU_UPGRADE) {
            upgradeSelection=(upgradeSelection+MenuHorizontal()+3)%3;
            int choice=ClickedUpgrade(renderWidth,renderHeight);
            if(IsKeyPressed(KEY_ONE)) choice=0;
            if(IsKeyPressed(KEY_TWO)) choice=1;
            if(IsKeyPressed(KEY_THREE)) choice=2;
            if(MenuAccept()) choice=upgradeSelection;
            if(choice>=0 && ChooseGameplayUpgrade(&game,choice)) {
                menuMode=MENU_NONE; resumedThisFrame=true; SuppressPlayerFire(&game);
            } else if(MenuBack()) { quitReturn=MENU_UPGRADE; menuMode=MENU_QUIT_CONFIRM; menuSelection=1; }
            else if(IsKeyPressed(KEY_F2)) { settingsReturn=MENU_UPGRADE; menuMode=MENU_SETTINGS; OpenPlayerSettings(); }
        } else if(MenuBack()) {
            quitReturn=MENU_NONE; menuMode=MENU_QUIT_CONFIRM; menuSelection=1;
        } else if(IsKeyPressed(KEY_F2)) {
            settingsReturn=MENU_NONE; menuMode=MENU_SETTINGS; OpenPlayerSettings();
        } else if(runStarted&&(PlayerActionPressed(ACT_PAUSE)||
                  (clicked&&CheckCollisionPointRec(mouse,PauseControl(renderWidth,renderHeight))))) {
            menuMode=MENU_PAUSED; menuSelection=0;
        }
        if(runStarted) {
            SetSynthVolumes(&synth,playerPreferences.musicVolume,playerPreferences.sfxVolume);
            // Salvage freezes the flight simulation, not the music callback.
            // Actual pause/settings/quit screens retain their pause behavior.
            bool shouldPause=menuMode!=MENU_NONE && menuMode!=MENU_UPGRADE;
            if(shouldPause!=audioPaused) {
                if(shouldPause) PauseAudioStream(synth.stream); else ResumeAudioStream(synth.stream);
                audioPaused=shouldPause;
            }
        }

        if (!runStarted) {
            if(menuMode==MENU_NONE && !resumedThisFrame) {
                selectorTime += dt;
                selectedSeed+=(uint32_t)MenuHorizontal();
                selectedSeed-=(uint32_t)(MenuVertical()*100);
            }

            if (menuMode==MENU_NONE && !resumedThisFrame &&
                MenuAccept()) {
                activeSeed = selectedSeed;
                InitEnvironment(&env, activeSeed);
                InitAudioSynth(&synth, activeSeed);
                InitGameplay(&game, activeSeed);
                InitDemoscene(&demo, activeSeed);
                InitPostProcess(&post, renderWidth, renderHeight, activeSeed, &demo);
                virtualPlayerZ = 0.0f;
                musicArrangementOriginZ = 0.0f;
                glitchAmount = 0.0f;
                runStarted = true;
                SuppressPlayerFire(&game);
            }

            BeginDrawing();
                ClearBackground((Color){ 2, 4, 12, 255 });
                DrawSeedSelector(selectedSeed, selectorTime,
                                 GetSynthSeedBpm(selectedSeed),
                                 GetSynthSeedKeyName(selectedSeed),
                                 renderWidth, renderHeight);
                DrawText("F2 SETTINGS / CONTROLS     F1 FLIGHT TIPS",24,renderHeight-26,14,LIGHTGRAY);
                if(menuMode==MENU_SETTINGS) DrawPlayerSettings(renderWidth,renderHeight);
                else if(menuMode!=MENU_NONE) DrawMenuOverlay(menuMode,renderWidth,renderHeight,menuSelection);
            EndDrawing();
            continue;
        }

        if(menuMode==MENU_NONE && !resumedThisFrame) {
        if (game.gameOver && (IsKeyPressed(KEY_R)||MenuAccept())) {
            activeSeed = selectedSeed;
            InitGameplay(&game, activeSeed);
            ReseedEnvironment(&env, activeSeed);
            UnloadAudioSynth(&synth);
            InitAudioSynth(&synth, activeSeed);
            InitDemoscene(&demo, activeSeed);
            SetPostProcessTheme(&post, activeSeed, &demo);
            virtualPlayerZ = 0.0f;
            musicArrangementOriginZ = 0.0f;
            glitchAmount = 0.0f;
            coreTransitionTimer = 0.0f;
            seedTransitionPending = false;
            seedTransitionCommitted = false;
            recordedRun=false;
            SuppressPlayerFire(&game);
        }
        if (IsKeyPressed(KEY_F3)) debugOverlay = !debugOverlay;

        bool boosting = PlayerActionDown(ACT_BOOST) && CanGameplayBoost(&game);
        currentSpeed = game.gameOver || seedTransitionPending
            ? 0.0f : GetGameplayTravelSpeed(&game, virtualPlayerZ, boosting);
        virtualPlayerZ += currentSpeed * dt;

        GameplayEvents gameplayEvents = UpdateGameplay(
            &game, dt, virtualPlayerZ, boosting);
        if(game.gameOver && !recordedRun) { RecordPersonalBest(&game); recordedRun=true; }
        for (int shot = 0; shot < gameplayEvents.tapShots; shot++) {
            TriggerSynthSFX(&synth, SFX_LASER_TAP);
        }
        for (int shot = 0; shot < gameplayEvents.chargeShots; shot++) {
            TriggerSynthSFX(&synth, SFX_LASER_CHARGE);
        }
        for (int enemy = 0; enemy < gameplayEvents.enemiesDestroyed; enemy++) {
            TriggerSynthSFX(&synth, SFX_EXPLOSION);
        }
        if (gameplayEvents.beamFired) TriggerSynthSFX(&synth, SFX_LASER_CHARGE);
        if (gameplayEvents.bombFired) TriggerSynthSFX(&synth, SFX_POWER_UP);
        if (gameplayEvents.playerHit) {
            TriggerSynthSFX(&synth, SFX_GLITCH_HIT);
            glitchAmount = 1.0f;
        }
        if (gameplayEvents.weaponTierAdvanced) {
            TriggerSynthSFX(&synth, SFX_POWER_UP);
        }
        if (gameplayEvents.bossStarted) {
            TriggerSynthSFX(&synth, SFX_BOSS_RISER);
        }
        if(gameplayEvents.attackWarning) TriggerSynthSFX(&synth,SFX_WARNING);
        if(gameplayEvents.dodged) TriggerSynthSFX(&synth,SFX_LASER_TAP);
        if (gameplayEvents.bossDefeated) {
            TriggerSynthSFX(&synth, SFX_CORE_COLLAPSE);
        }
        if (gameplayEvents.recursionCompleted) {
            int completedRecursions = game.boss.encounterIndex / BOSSES_PER_RECURSION;
            pendingSeed = DeriveLoopSeed(selectedSeed, completedRecursions);
            transitionFromDemo = demo;
            InitDemoscene(&transitionToDemo, pendingSeed);
            coreTransitionTimer = CORE_TRANSITION_DURATION;
            seedTransitionPending = true;
            seedTransitionCommitted = false;
        } else if (gameplayEvents.bossDefeated) {
            TriggerSynthSFX(&synth, SFX_POWER_UP);
        }
        if (seedTransitionPending) {
            coreTransitionTimer = fmaxf(0.0f, coreTransitionTimer - dt);
            if (!seedTransitionCommitted && coreTransitionTimer <= 1.82f) {
                activeSeed = pendingSeed;
                AdvanceGameplayLoop(&game, activeSeed, virtualPlayerZ);
                ReseedEnvironment(&env, activeSeed);
                ReseedAudioSynth(&synth, activeSeed);
                musicArrangementOriginZ = virtualPlayerZ - 400.0f;
                demo = transitionToDemo;
                SetPostProcessTheme(&post, activeSeed, &demo);
                TriggerSynthSFX(&synth, SFX_POWER_UP);
                seedTransitionCommitted = true;
            }
            if (coreTransitionTimer <= 0.0f) {
                seedTransitionPending = false;
                seedTransitionCommitted = false;
            }
        }

        // Decay glitch state
        if (glitchAmount > 0.0f) {
            glitchAmount = fmaxf(0.0f, glitchAmount - dt * 2.5f);
        }

        // Tempo is fixed per seed once the intro ramp completes. Progress,
        // combat and boost instead reveal more rhythmic and visual layers.
        musicIntensity = 0.24f + game.difficulty * 0.20f;
        musicIntensity += fminf((float)game.combo * 0.018f, 0.12f);
        if (boosting) musicIntensity += 0.48f;
        if (game.boss.active) musicIntensity += game.boss.phase == BOSS_SINGULARITY ? 0.30f
                                                   : game.boss.phase == BOSS_ENRAGED ? 0.24f : 0.14f;

        // Quiet hush in the stretch before the boss spawns - snaps off the
        // instant the fight starts, giving the drop somewhere to land.
        float preBossHush = 0.0f;
        const float bossHushWindow = 350.0f;
        if (!game.boss.active) {
            double distanceToBoss = game.boss.nextSpawnDistance - virtualPlayerZ;
            if (distanceToBoss > 0.0f && distanceToBoss < bossHushWindow) {
                preBossHush = 1.0f - distanceToBoss / bossHushWindow;
            }
        }
        musicIntensity *= (1.0f - preBossHush * 0.5f);
        if (seedTransitionPending) {
            float transitionProgress = 1.0f - coreTransitionTimer / CORE_TRANSITION_DURATION;
            float transitionHush = sinf(fminf(fmaxf(transitionProgress, 0.0f), 1.0f) *
                                             3.14159265f);
            musicIntensity *= 1.0f - transitionHush * 0.88f;
            preBossHush = fmaxf(preBossHush, transitionHush);
        }

        if (game.gameOver) musicIntensity = 0.10f;
        musicIntensity = fmaxf(0.0f, fminf(musicIntensity, 1.0f));

        float bossMusicIntensity = 0.0f;
        if (game.boss.active) {
            if (game.boss.phase == BOSS_APPROACH) {
                bossMusicIntensity = 0.42f +
                    fminf(game.boss.phaseTime / 2.6f, 1.0f) * 0.25f;
            } else if (game.boss.phase == BOSS_SHIELDED) {
                bossMusicIntensity = 0.70f;
            } else if (game.boss.phase == BOSS_EXPOSED) {
                bossMusicIntensity = 0.84f;
            } else if (game.boss.phase == BOSS_ENRAGED) {
                bossMusicIntensity = 1.0f;
            } else if (game.boss.phase == BOSS_SINGULARITY) {
                bossMusicIntensity = 1.0f;
            }
        }

        // Update Subsystems
        float musicArrangementDistance = (float)fmax(
            0.0, virtualPlayerZ - musicArrangementOriginZ);
        UpdateAudioSynth(&synth, musicIntensity, glitchAmount, bossMusicIntensity,
                        musicArrangementDistance, preBossHush);
        UpdateGameplayCamera(&game, &camera, dt, boosting);

        audioTelemetry = GetSynthTelemetry(&synth);
        beatPulse = audioTelemetry.beatPulse;
        UpdateEnvironment(&env, virtualPlayerZ, game.runTime, musicIntensity, beatPulse);

        bossTransition = 0.0f;
        if (game.boss.active && game.boss.phase == BOSS_APPROACH) {
            bossTransition = fmaxf(0.001f, fminf(game.boss.phaseTime / 2.6f, 1.0f));
        }
        }
        if(game.upgradePending && menuMode==MENU_NONE) {
            menuMode=MENU_UPGRADE; upgradeSelection=0;
            SuppressPlayerFire(&game);
            UpdateAudioSynth(&synth,.30f,0,0,
                (float)fmax(0.0,virtualPlayerZ-musicArrangementOriginZ),0);
        }
        BeginDrawing();
            BeginTextureMode(post.target);
                ClearBackground((Color){ 2, 3, 10, 255 });
                DrawRaymarchBackdrop(&post, game.runTime, musicIntensity,
                                      game.boss.encounterIndex, renderWidth, renderHeight);
                DrawDemosceneBackdrop(&demo, game.runTime, musicIntensity,
                                      renderWidth, renderHeight);
                BeginMode3D(camera);
                    DrawEnvironment(&env, camera, virtualPlayerZ);
                    BeginCraftPass(&post, game.runTime, musicIntensity, camera.position);
                        DrawGameplay3D(&game, post.craftShader,
                                       post.craftObjectClassLoc, post.craftEnemyTypeLoc, post.craftOriginLoc);
                    EndShaderMode();
                EndMode3D();
                DrawDemosceneOverlay(&demo, game.runTime, musicIntensity,
                                     beatPulse, renderWidth, renderHeight);
            EndTextureMode();

            ClearBackground((Color){ 2, 3, 9, 255 });
            DrawPostProcess(&post, game.runTime, musicIntensity, beatPulse, bossTransition,
                            renderWidth, renderHeight, camera.fovy);
            if(menuMode!=MENU_UPGRADE) {
                float hudScale=fminf(1,(float)renderWidth/1280.0f);
                BeginMode2D((Camera2D){.zoom=hudScale});
                DrawGameplayHUD(&game,camera,(int)(renderWidth/hudScale),(int)(renderHeight/hudScale));
                EndMode2D();
            }
            if(menuMode==MENU_NONE) DrawPauseControl(renderWidth,renderHeight);
            if (seedTransitionPending) {
                float transitionProgress = 1.0f - coreTransitionTimer / CORE_TRANSITION_DURATION;
                Vector2 coreScreen = GetWorldToScreen(game.boss.position, camera);
                DrawCoreTransition(&transitionFromDemo, &transitionToDemo, game.runTime,
                                   transitionProgress, coreScreen, renderWidth, renderHeight);
            }
            if (debugOverlay) {
                DrawFPS(10, 82);
                DrawText(TextFormat("Z %.1f  SPEED %.1f  BPM %.1f  INT %.2f  GATE %.2f",
                                    virtualPlayerZ, currentSpeed, audioTelemetry.currentBpm,
                                    musicIntensity, audioTelemetry.drumGate),
                         10, 104, 16, (Color){ 90, 190, 215, 220 });
                DrawText(TextFormat("STRUCTURES %d  DROPPED %d", env.structureCount,
                                    env.droppedStructures),
                         10, 124, 16, (Color){ 90, 170, 220, 220 });
                DrawText(TextFormat("DEPTH %d  ROUND %d/%d  SEED %u  DIFFICULTY %.2f",
                                    GetGameplayRecursionDepth(&game), GetGameplayRound(&game),
                                    BOSSES_PER_RECURSION, activeSeed, game.difficulty),
                         10, 144, 16, (Color){ 110, 205, 190, 220 });
                unsigned int callbackFrames = audioTelemetry.callbackFrames;
                if (callbackFrames > 0u && audioTelemetry.maxCallbackMicros > 0u) {
                    float callbackBudgetMs = (float)callbackFrames * 1000.0f / SAMPLE_RATE;
                    DrawText(TextFormat("AUDIO CB MAX %.2f / %.2f MS  FRAMES %u",
                                        audioTelemetry.maxCallbackMicros / 1000.0f,
                                        callbackBudgetMs, callbackFrames),
                             10, 164, 16, (Color){ 110, 205, 190, 220 });
                }
                if (audioTelemetry.controlDrops > 0u || audioTelemetry.sfxCommandDrops > 0u) {
                    DrawText(TextFormat("AUDIO DROPS CONTROL %u  SFX %u",
                                        audioTelemetry.controlDrops,
                                        audioTelemetry.sfxCommandDrops),
                             10, 184, 16, (Color){ 255, 120, 90, 230 });
                }
            }
            if(menuMode==MENU_SETTINGS) DrawPlayerSettings(renderWidth,renderHeight);
            else if(menuMode==MENU_UPGRADE) DrawUpgradeChoices(&game,upgradeSelection,renderWidth,renderHeight);
            else if(menuMode!=MENU_NONE) DrawMenuOverlay(menuMode,renderWidth,renderHeight,menuSelection);
        EndDrawing();
    }

    SavePlayerPreferences();
    if (runStarted) {
        UnloadAudioSynth(&synth);
        UnloadEnvironment(&env);
        UnloadPostProcess(&post);
    }
    CloseWindow();
    return 0;
#endif
}
