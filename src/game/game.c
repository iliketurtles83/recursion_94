// RECURSION_94 game shell: seed selector -> run. The run flies the engine's
// current environment with the procedural synth reacting to boost and progress;
// the synth's beat pulse feeds back into the picture through ct_set_beat.
#include "game.h"
#include "audio_synth.h"
#include "demoscene.h"
#include "raylib.h"
#include "rlgl.h"
#include <math.h>
#include <stdio.h>

// The synth's song structure (intro ramp, buildup) was tuned in the original
// game's distance units at roughly 30 units/s. Feed it elapsed run time in those
// units so the arrangement keeps its pacing regardless of the engine's speed.
#define ARRANGEMENT_UNITS_PER_SECOND 30.0f
#define LEGEND_SECONDS 8.0f

typedef enum { STATE_SELECT, STATE_RUN } GameState;

typedef struct Game {
    CtTunnel   *ct;
    SynthSystem synth;
    bool        audioOn;
    GameState   state;
    uint32_t    selectedSeed, runSeed;
    float       selectorTime, runTime, musicIntensity, beatPulse;
    bool        paused;
} Game;

static void stop_audio(Game *g)
{
    if (g->audioOn) UnloadAudioSynth(&g->synth);
    g->audioOn = false;
}

static void start_run(Game *g, uint32_t seed, bool withAudio)
{
    stop_audio(g);
    g->runSeed = seed;
    g->runTime = 0.0f;
    g->musicIntensity = 0.24f;
    g->beatPulse = 0.0f;
    g->paused = false;
    ct_set_paused(g->ct, false);
    ct_reset_route(g->ct, (uint64_t)seed);
    if (withAudio) {
        InitAudioSynth(&g->synth, seed);
        g->audioOn = IsAudioDeviceReady();
    }
    g->state = STATE_RUN;
}

static CtInput read_input(void)
{
    CtInput in = { 0 };
    in.steerX = (float)((IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) - (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A)));
    in.steerY = (float)((IsKeyDown(KEY_UP) || IsKeyDown(KEY_W)) - (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S)));
    in.boost = IsKeyDown(KEY_SPACE) || IsKeyDown(KEY_LEFT_SHIFT);
    in.rollLeft = IsKeyPressed(KEY_Q);
    in.rollRight = IsKeyPressed(KEY_E);
    return in;
}

// Progress and boost reveal more musical layers; tempo stays fixed per seed.
static void update_music(Game *g, bool boosting)
{
    float intensity = 0.24f + 0.20f * fminf(g->runTime / 180.0f, 1.0f);
    if (boosting) intensity += 0.48f;
    g->musicIntensity = fmaxf(0.0f, fminf(intensity, 1.0f));
    if (!g->audioOn) { g->beatPulse = 0.0f; return; }
    UpdateAudioSynth(&g->synth, g->musicIntensity, 0.0f, 0.0f,
                     g->runTime * ARRANGEMENT_UNITS_PER_SECOND, 0.0f);
    g->beatPulse = GetSynthTelemetry(&g->synth).beatPulse;
}

static void draw_hud(const Game *g, int w, int h)
{
    DemosceneSystem demo;
    InitDemoscene(&demo, g->runSeed);
    Color dim = { demo.primary.r, demo.primary.g, demo.primary.b, 170 };
    DrawText("RECURSION_94", 24, 20, 20, dim);
    const char *id = TextFormat("SEED %08X  //  %s  //  %.0f BPM", (unsigned int)g->runSeed,
                                GetSynthSeedKeyName(g->runSeed), GetSynthSeedBpm(g->runSeed));
    DrawText(id, w - MeasureText(id, 14) - 24, h - 30, 14, dim);
    if (g->runTime < LEGEND_SECONDS) {
        float a = fminf(1.0f, (LEGEND_SECONDS - g->runTime) / 1.5f);
        const char *legend = "ARROWS / WASD STEER    SPACE BOOST    Q / E ROLL    P PAUSE    ESC SEED SELECT";
        DrawText(legend, w / 2 - MeasureText(legend, 16) / 2, h - 64, 16,
                 (Color){ 200, 230, 240, (unsigned char)(a * 200.0f) });
    }
    if (g->paused) {
        const char *t = "PAUSED";
        DrawText(t, w / 2 - MeasureText(t, 40) / 2, h / 2 - 20, 40, demo.hot);
    }
}

static void save_shot(const char *path)
{
    rlDrawRenderBatchActive();   // flush queued HUD draws before reading the framebuffer
    Image img = LoadImageFromScreen();
    ExportImage(img, path);
    UnloadImage(img);
    printf("screenshot: %s\n", path);
}

int game_run(CtTunnel *ct, const GameOptions *o)
{
    Game g = { .ct = ct, .state = STATE_SELECT, .selectedSeed = o->seed, .runSeed = o->seed };
    bool shotMode = o->shotSeconds >= 0.0;
    // Shot mode is deterministic: no audio thread, fixed step, beat pulse held at zero.
    if (o->skipSelector || shotMode) start_run(&g, o->seed, !shotMode);
    int frames = 0;

    while (!WindowShouldClose()) {
        float dt = shotMode ? 1.0f / 60.0f : GetFrameTime();
        int w = GetRenderWidth(), h = GetRenderHeight();

        if (g.state == STATE_SELECT) {
            g.selectorTime += dt;
            if (IsKeyPressed(KEY_ESCAPE)) break;
            if (IsKeyPressed(KEY_LEFT)) g.selectedSeed -= 1u;
            if (IsKeyPressed(KEY_RIGHT)) g.selectedSeed += 1u;
            if (IsKeyPressed(KEY_UP)) g.selectedSeed += 100u;
            if (IsKeyPressed(KEY_DOWN)) g.selectedSeed -= 100u;
            if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) start_run(&g, g.selectedSeed, true);
            BeginDrawing();
            ClearBackground(BLACK);
            DrawSeedSelector(g.selectedSeed, g.selectorTime, GetSynthSeedBpm(g.selectedSeed),
                             GetSynthSeedKeyName(g.selectedSeed), w, h);
            EndDrawing();
            continue;
        }

        if (!shotMode) {
            if (IsKeyPressed(KEY_ESCAPE)) {
                stop_audio(&g);
                g.selectedSeed = g.runSeed;
                g.state = STATE_SELECT;
                continue;
            }
            if (IsKeyPressed(KEY_P)) {
                g.paused = !g.paused;
                ct_set_paused(ct, g.paused);
                if (g.audioOn) { if (g.paused) PauseAudioStream(g.synth.stream); else ResumeAudioStream(g.synth.stream); }
            }
        }

        if (!g.paused) {
            CtInput in = shotMode ? (CtInput){ 0 } : read_input();
            ct_set_input(ct, &in);
            g.runTime += dt;
            update_music(&g, in.boost);
            ct_set_beat(ct, g.beatPulse);
        }
        ct_update(ct, g.paused ? 0.0f : dt);

        BeginDrawing();
        ct_render(ct, NULL, w, h);
        draw_hud(&g, w, h);
        bool shoot = shotMode && ++frames >= (int)(o->shotSeconds * 60.0 + 0.5);
        if (shoot) save_shot(o->shotOut ? o->shotOut : "shots/game.png");
        EndDrawing();
        if (shoot) break;
    }

    stop_audio(&g);
    return 0;
}
