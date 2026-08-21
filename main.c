#include "raylib.h"
#include "environment.h"
#include "audio_synth.h"
#include "gameplay.h"
#include "demoscene.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef RECURSION_VALIDATE_GENERATOR
typedef struct {
    RenderTexture2D target;
    Shader shader;
    int timeLoc;
    int intensityLoc;
    int beatLoc;
    int bossTransitionLoc;
    Shader craftShader;
    int craftTimeLoc;
    int craftIntensityLoc;
    int craftObjectClassLoc;
    int craftEnemyTypeLoc;
    Shader backdropShader;
    int backdropTimeLoc;
    bool ready;
} PostProcessSystem;

static void InitPostProcess(PostProcessSystem *post, int width, int height,
                            uint32_t seed, Color primary, Color secondary) {
    post->target = LoadRenderTexture(width, height);
    SetTextureFilter(post->target.texture, TEXTURE_FILTER_BILINEAR);
    post->shader = LoadShader(NULL, "shaders/post.fs");
    post->timeLoc = GetShaderLocation(post->shader, "uTime");
    post->intensityLoc = GetShaderLocation(post->shader, "uIntensity");
    post->beatLoc = GetShaderLocation(post->shader, "uBeatPulse");
    post->bossTransitionLoc = GetShaderLocation(post->shader, "uBossTransition");
    int resolutionLoc = GetShaderLocation(post->shader, "uResolution");
    int seedLoc = GetShaderLocation(post->shader, "uRunSeed");
    int primaryLoc = GetShaderLocation(post->shader, "uPrimaryColor");
    int secondaryLoc = GetShaderLocation(post->shader, "uSecondaryColor");
    Vector2 resolution = { (float)width, (float)height };
    int shaderSeed = (int)seed;
    Vector3 primaryValue = { primary.r / 255.0f, primary.g / 255.0f, primary.b / 255.0f };
    Vector3 secondaryValue = { secondary.r / 255.0f, secondary.g / 255.0f, secondary.b / 255.0f };
    SetShaderValue(post->shader, resolutionLoc, &resolution, SHADER_UNIFORM_VEC2);
    SetShaderValue(post->shader, seedLoc, &shaderSeed, SHADER_UNIFORM_INT);
    SetShaderValue(post->shader, primaryLoc, &primaryValue, SHADER_UNIFORM_VEC3);
    SetShaderValue(post->shader, secondaryLoc, &secondaryValue, SHADER_UNIFORM_VEC3);

    post->craftShader = LoadShader("shaders/craft.vs", "shaders/craft.fs");
    post->craftTimeLoc = GetShaderLocation(post->craftShader, "uTime");
    post->craftIntensityLoc = GetShaderLocation(post->craftShader, "uIntensity");
    post->craftObjectClassLoc = GetShaderLocation(post->craftShader, "uObjectClass");
    post->craftEnemyTypeLoc = GetShaderLocation(post->craftShader, "uEnemyType");
    int craftPrimaryLoc = GetShaderLocation(post->craftShader, "uPrimaryColor");
    int craftSecondaryLoc = GetShaderLocation(post->craftShader, "uSecondaryColor");
    SetShaderValue(post->craftShader, craftPrimaryLoc, &primaryValue, SHADER_UNIFORM_VEC3);
    SetShaderValue(post->craftShader, craftSecondaryLoc, &secondaryValue, SHADER_UNIFORM_VEC3);

    post->backdropShader = LoadShader(NULL, "shaders/backdrop.fs");
    post->backdropTimeLoc = GetShaderLocation(post->backdropShader, "uTime");
    int backdropResolutionLoc = GetShaderLocation(post->backdropShader, "uResolution");
    int backdropSeedLoc = GetShaderLocation(post->backdropShader, "uRunSeed");
    int backdropPrimaryLoc = GetShaderLocation(post->backdropShader, "uPrimaryColor");
    int backdropSecondaryLoc = GetShaderLocation(post->backdropShader, "uSecondaryColor");
    SetShaderValue(post->backdropShader, backdropResolutionLoc, &resolution, SHADER_UNIFORM_VEC2);
    SetShaderValue(post->backdropShader, backdropSeedLoc, &shaderSeed, SHADER_UNIFORM_INT);
    SetShaderValue(post->backdropShader, backdropPrimaryLoc, &primaryValue, SHADER_UNIFORM_VEC3);
    SetShaderValue(post->backdropShader, backdropSecondaryLoc, &secondaryValue, SHADER_UNIFORM_VEC3);

    post->ready = post->target.texture.id != 0 && post->shader.id != 0 &&
                  post->craftShader.id != 0 && post->backdropShader.id != 0;
}

static void BeginCraftPass(PostProcessSystem *post, float time, float intensity) {
    SetShaderValue(post->craftShader, post->craftTimeLoc, &time, SHADER_UNIFORM_FLOAT);
    SetShaderValue(post->craftShader, post->craftIntensityLoc, &intensity,
                   SHADER_UNIFORM_FLOAT);
    BeginShaderMode(post->craftShader);
}

static void DrawRaymarchBackdrop(PostProcessSystem *post, float time, int width, int height) {
    SetShaderValue(post->backdropShader, post->backdropTimeLoc, &time, SHADER_UNIFORM_FLOAT);
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
             "GENERATOR: zones=%d repeats=%d landmark-spacing=%d peak=%d dropped=%d",
             generationReport.zonesChecked,
             generationReport.adjacentRepeatViolations,
             generationReport.landmarkSpacingViolations,
             generationReport.peakStructureCount,
             generationReport.droppedStructures);
    return generationValid ? 0 : 1;
#else
    const int screenWidth = 1280;
    const int screenHeight = 720;
    SetConfigFlags(FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT);
    InitWindow(screenWidth, screenHeight, "RECURSION_94 - Procedural Cyberspace Trench");

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
            bossTestStart = strcmp(argv[1], "--boss") == 0;
            InitEnvironment(&env, selectedSeed);
            InitAudioSynth(&synth, selectedSeed);
            InitGameplay(&game, selectedSeed);
            InitDemoscene(&demo, selectedSeed);
            InitPostProcess(&post, screenWidth, screenHeight, selectedSeed,
                            demo.primary, demo.secondary);
            runStarted = true;
        }
    }

    float virtualPlayerZ = bossTestStart ? game.boss.nextSpawnDistance + 1.0f : 0.0f;
    float baseSpeed = 20.0f;
    float glitchAmount = 0.0f;
    bool debugOverlay = false;

    SetTargetFPS(60);

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();

        if (!runStarted) {
            selectorTime += dt;
            if (IsKeyPressed(KEY_LEFT)) selectedSeed -= 1u;
            if (IsKeyPressed(KEY_RIGHT)) selectedSeed += 1u;
            if (IsKeyPressed(KEY_UP)) selectedSeed += 100u;
            if (IsKeyPressed(KEY_DOWN)) selectedSeed -= 100u;

            if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) {
                InitEnvironment(&env, selectedSeed);
                InitAudioSynth(&synth, selectedSeed);
                InitGameplay(&game, selectedSeed);
                InitDemoscene(&demo, selectedSeed);
                InitPostProcess(&post, screenWidth, screenHeight, selectedSeed,
                                demo.primary, demo.secondary);
                virtualPlayerZ = 0.0f;
                glitchAmount = 0.0f;
                runStarted = true;
            }

            BeginDrawing();
                ClearBackground((Color){ 2, 4, 12, 255 });
                DrawSeedSelector(selectedSeed, selectorTime,
                                 GetSynthSeedBpm(selectedSeed),
                                 GetSynthSeedKeyName(selectedSeed),
                                 screenWidth, screenHeight);
            EndDrawing();
            continue;
        }

        if (game.gameOver && IsKeyPressed(KEY_R)) {
            InitGameplay(&game, selectedSeed);
            virtualPlayerZ = 0.0f;
            glitchAmount = 0.0f;
        }
        if (IsKeyPressed(KEY_F3)) debugOverlay = !debugOverlay;

        bool boosting = IsKeyDown(KEY_SPACE) && CanGameplayBoost(&game);
        float speedMultiplier = game.gameOver ? 0.0f : (boosting ? 2.2f : 1.0f);
        float currentSpeed = baseSpeed * speedMultiplier;
        virtualPlayerZ += currentSpeed * dt;

        GameplayEvents gameplayEvents = UpdateGameplay(
            &game, dt, virtualPlayerZ, currentSpeed, boosting);
        if (gameplayEvents.tapShots > 0) {
            TriggerSynthSFX(&synth, SFX_LASER_TAP);
        }
        if (gameplayEvents.chargeShots > 0) {
            TriggerSynthSFX(&synth, SFX_LASER_CHARGE);
        }
        if (gameplayEvents.enemiesDestroyed > 0) {
            TriggerSynthSFX(&synth, SFX_EXPLOSION);
        }
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
            TriggerSynthSFX(&synth, SFX_POWER_UP);
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
            float distanceToBoss = game.boss.nextSpawnDistance - virtualPlayerZ;
            if (distanceToBoss > 0.0f && distanceToBoss < bossHushWindow) {
                preBossHush = 1.0f - distanceToBoss / bossHushWindow;
            }
        }
        musicIntensity *= (1.0f - preBossHush * 0.5f);

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
        UpdateAudioSynth(&synth, musicIntensity, glitchAmount, bossMusicIntensity,
                        virtualPlayerZ, preBossHush);
        UpdateGameplayCamera(&game, &camera, dt, boosting);

        float beatPulse = GetSynthBeatPulse(&synth);
        float bossTransition = 0.0f;
        if (game.boss.active && game.boss.phase == BOSS_APPROACH) {
            bossTransition = fmaxf(0.001f, fminf(game.boss.phaseTime / 2.6f, 1.0f));
        }
        BeginDrawing();
            BeginTextureMode(post.target);
                ClearBackground((Color){ 2, 3, 10, 255 });
                DrawRaymarchBackdrop(&post, game.runTime, screenWidth, screenHeight);
                DrawDemosceneBackdrop(&demo, game.runTime, musicIntensity,
                                      screenWidth, screenHeight);
                BeginMode3D(camera);
                    DrawEnvironment(&env, camera, virtualPlayerZ);
                    BeginCraftPass(&post, game.runTime, musicIntensity);
                        DrawGameplay3D(&game, post.craftShader,
                                       post.craftObjectClassLoc, post.craftEnemyTypeLoc);
                    EndShaderMode();
                EndMode3D();
                DrawDemosceneOverlay(&demo, game.runTime, musicIntensity,
                                     beatPulse, screenWidth, screenHeight);
            EndTextureMode();

            ClearBackground((Color){ 2, 3, 9, 255 });
            DrawPostProcess(&post, game.runTime, musicIntensity, beatPulse, bossTransition,
                            screenWidth, screenHeight);
            DrawGameplayHUD(&game, camera, screenWidth, screenHeight);
            if (debugOverlay) {
                DrawFPS(10, 82);
                DrawText(TextFormat("Z %.1f  SPEED %.1fx  BPM %.1f  INT %.2f  GATE %.2f",
                                    virtualPlayerZ, speedMultiplier, synth.currentBpm,
                                    musicIntensity, GetSynthDrumGate(&synth)),
                         10, 104, 16, (Color){ 90, 190, 215, 220 });
                DrawText(TextFormat("STRUCTURES %d  DROPPED %d", env.structureCount,
                                    env.droppedStructures),
                         10, 124, 16, (Color){ 90, 170, 220, 220 });
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
