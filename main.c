#include "raylib.h"
#include "palette.h"
#include "environment.h"
#include "audio_synth.h"
#include "gameplay.h"
#include "demoscene.h"
#include "shader_sources.h"
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
    Shader craftShader;
    int craftTimeLoc;
    int craftIntensityLoc;
    int craftObjectClassLoc;
    int craftEnemyTypeLoc;
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

static void InitPostProcess(PostProcessSystem *post, int width, int height,
                            uint32_t seed, const DemosceneSystem *demo) {
    post->target = LoadRenderTexture(width, height);
    SetTextureFilter(post->target.texture, TEXTURE_FILTER_BILINEAR);
    post->shader = LoadShaderFromMemory(NULL, shader_post_fs);
    post->timeLoc = GetShaderLocation(post->shader, "uTime");
    post->intensityLoc = GetShaderLocation(post->shader, "uIntensity");
    post->beatLoc = GetShaderLocation(post->shader, "uBeatPulse");
    post->bossTransitionLoc = GetShaderLocation(post->shader, "uBossTransition");
    int resolutionLoc = GetShaderLocation(post->shader, "uResolution");
    post->seedLoc = GetShaderLocation(post->shader, "uRunSeed");
    post->primaryLoc = GetShaderLocation(post->shader, "uPrimaryColor");
    post->secondaryLoc = GetShaderLocation(post->shader, "uSecondaryColor");
    Vector2 resolution = { (float)width, (float)height };
    SetShaderValue(post->shader, resolutionLoc, &resolution, SHADER_UNIFORM_VEC2);

    post->craftShader = LoadShaderFromMemory(shader_craft_vs, shader_craft_fs);
    post->craftTimeLoc = GetShaderLocation(post->craftShader, "uTime");
    post->craftIntensityLoc = GetShaderLocation(post->craftShader, "uIntensity");
    post->craftObjectClassLoc = GetShaderLocation(post->craftShader, "uObjectClass");
    post->craftEnemyTypeLoc = GetShaderLocation(post->craftShader, "uEnemyType");
    post->craftPrimaryLoc = GetShaderLocation(post->craftShader, "uPrimaryColor");
    post->craftSecondaryLoc = GetShaderLocation(post->craftShader, "uSecondaryColor");
    post->craftThreatLoc = GetShaderLocation(post->craftShader, "uThreatColor");

    post->backdropShader = LoadShaderFromMemory(NULL, shader_backdrop_fs);
    post->backdropTimeLoc = GetShaderLocation(post->backdropShader, "uTime");
    post->backdropIntensityLoc = GetShaderLocation(post->backdropShader, "uIntensity");
    int backdropResolutionLoc = GetShaderLocation(post->backdropShader, "uResolution");
    post->backdropSeedLoc = GetShaderLocation(post->backdropShader, "uRunSeed");
    post->backdropPrimaryLoc = GetShaderLocation(post->backdropShader, "uPrimaryColor");
    post->backdropSecondaryLoc = GetShaderLocation(post->backdropShader, "uSecondaryColor");
    post->backdropZenithLoc = GetShaderLocation(post->backdropShader, "uSkyZenith");
    post->backdropHorizonLoc = GetShaderLocation(post->backdropShader, "uSkyHorizon");
    post->backdropBodyLoc = GetShaderLocation(post->backdropShader, "uBodyColor");
    post->backdropEncounterLoc = GetShaderLocation(post->backdropShader, "uEncounterIndex");
    SetShaderValue(post->backdropShader, backdropResolutionLoc, &resolution, SHADER_UNIFORM_VEC2);
    SetPostProcessTheme(post, seed, demo);

    post->ready = post->target.texture.id != 0 && post->shader.id != 0 &&
                  post->craftShader.id != 0 && post->backdropShader.id != 0;
}

static void BeginCraftPass(PostProcessSystem *post, float time, float intensity) {
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
    BeginShaderMode(post->backdropShader);
        DrawRectangle(0, 0, width, height, WHITE);
    EndShaderMode();
}

static void DrawPostProcess(PostProcessSystem *post, float time, float intensity,
                            float beatPulse, float bossTransition,
                            int width, int height) {
    SetShaderValue(post->shader, post->timeLoc, &time, SHADER_UNIFORM_FLOAT);
    SetShaderValue(post->shader, post->intensityLoc, &intensity, SHADER_UNIFORM_FLOAT);
    SetShaderValue(post->shader, post->beatLoc, &beatPulse, SHADER_UNIFORM_FLOAT);
    SetShaderValue(post->shader, post->bossTransitionLoc, &bossTransition,
                   SHADER_UNIFORM_FLOAT);
    BeginShaderMode(post->shader);
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
#endif

int main(int argc, char **argv) {
#ifdef RECURSION_VALIDATE_GENERATOR
    (void)argc;
    (void)argv;
    EnvironmentValidationReport generationReport;
    bool generationValid = ValidateEnvironmentGenerator(&generationReport);
    TraceLog(generationValid ? LOG_INFO : LOG_ERROR,
             "GENERATOR: zones=%d repeats=%d landmark-spacing=%d field=%d clearance=%d conduit=%d peak=%d terrain=%d far=%d unsupported=%d(flank=%d ravine=%d far=%d) dropped=%d",
             generationReport.zonesChecked,
             generationReport.adjacentRepeatViolations,
             generationReport.landmarkSpacingViolations,
             generationReport.fieldCoverageViolations,
             generationReport.crossingClearanceViolations,
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
    return ValidateGameplaySpecials() ? 0 : 1;
#else
    const int screenWidth = 1280;
    const int screenHeight = 720;
    SetConfigFlags(FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT);
    InitWindow(screenWidth, screenHeight, "RECURSION_94 - Procedural Cyberspace Trench");
    int renderWidth = GetScreenWidth();
    int renderHeight = GetScreenHeight();
    if (!IsWindowReady()) {
        TraceLog(LOG_ERROR, "Unable to initialize the graphics window");
        return 1;
    }

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

    SetTargetFPS(60);

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();

        if (IsKeyPressed(KEY_F)) ToggleFullscreen();

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

        if (!runStarted) {
            selectorTime += dt;
            if (IsKeyPressed(KEY_LEFT)) selectedSeed -= 1u;
            if (IsKeyPressed(KEY_RIGHT)) selectedSeed += 1u;
            if (IsKeyPressed(KEY_UP)) selectedSeed += 100u;
            if (IsKeyPressed(KEY_DOWN)) selectedSeed -= 100u;

            if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
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
            }

            BeginDrawing();
                ClearBackground((Color){ 2, 4, 12, 255 });
                DrawSeedSelector(selectedSeed, selectorTime,
                                 GetSynthSeedBpm(selectedSeed),
                                 GetSynthSeedKeyName(selectedSeed),
                                 renderWidth, renderHeight);
            EndDrawing();
            continue;
        }

        if (game.gameOver && IsKeyPressed(KEY_R)) {
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
        }
        if (IsKeyPressed(KEY_F3)) debugOverlay = !debugOverlay;

        bool boosting = IsKeyDown(KEY_SPACE) && CanGameplayBoost(&game);
        float currentSpeed = game.gameOver || seedTransitionPending
            ? 0.0f : GetGameplayTravelSpeed(&game, virtualPlayerZ, boosting);
        virtualPlayerZ += currentSpeed * dt;

        GameplayEvents gameplayEvents = UpdateGameplay(
            &game, dt, virtualPlayerZ, boosting);
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
        if (gameplayEvents.bossDefeated) {
            pendingSeed = DeriveLoopSeed(selectedSeed, game.boss.encounterIndex);
            transitionFromDemo = demo;
            InitDemoscene(&transitionToDemo, pendingSeed);
            coreTransitionTimer = CORE_TRANSITION_DURATION;
            seedTransitionPending = true;
            seedTransitionCommitted = false;
            TriggerSynthSFX(&synth, SFX_CORE_COLLAPSE);
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
        float musicIntensity = 0.24f + game.difficulty * 0.20f;
        musicIntensity += fminf((float)game.combo * 0.018f, 0.12f);
        if (boosting) musicIntensity += 0.48f;
        if (game.boss.active) musicIntensity += game.boss.phase == BOSS_ENRAGED ? 0.24f : 0.14f;

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
            }
        }

        // Update Subsystems
        UpdateEnvironment(&env, virtualPlayerZ, game.runTime, musicIntensity);
        float musicArrangementDistance = (float)fmax(
            0.0, virtualPlayerZ - musicArrangementOriginZ);
        UpdateAudioSynth(&synth, musicIntensity, glitchAmount, bossMusicIntensity,
                        musicArrangementDistance, preBossHush);
        UpdateGameplayCamera(&game, &camera, dt, boosting);

        SynthTelemetry audioTelemetry = GetSynthTelemetry(&synth);
        float beatPulse = audioTelemetry.beatPulse;
        float bossTransition = 0.0f;
        if (game.boss.active && game.boss.phase == BOSS_APPROACH) {
            bossTransition = fmaxf(0.001f, fminf(game.boss.phaseTime / 2.6f, 1.0f));
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
                    BeginCraftPass(&post, game.runTime, musicIntensity);
                        DrawGameplay3D(&game, post.craftShader,
                                       post.craftObjectClassLoc, post.craftEnemyTypeLoc);
                    EndShaderMode();
                EndMode3D();
                DrawDemosceneOverlay(&demo, game.runTime, musicIntensity,
                                     beatPulse, renderWidth, renderHeight);
            EndTextureMode();

            ClearBackground((Color){ 2, 3, 9, 255 });
            DrawPostProcess(&post, game.runTime, musicIntensity, beatPulse, bossTransition,
                            renderWidth, renderHeight);
            DrawGameplayHUD(&game, camera, renderWidth, renderHeight);
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
                DrawText(TextFormat("RECURSION %d  SEED %u  DIFFICULTY %.2f",
                                    game.boss.encounterIndex + 1, activeSeed, game.difficulty),
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
        EndDrawing();
    }

    if (runStarted) {
        UnloadAudioSynth(&synth);
        UnloadEnvironment(&env);
        UnloadPostProcess(&post);
    }
    CloseWindow();
    return 0;
#endif
}
