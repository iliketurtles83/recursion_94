// RECURSION_94 game: seed selector -> run. The engine flies the ship through the
// corridor; the ported combat runs in arena units that ride the path ahead of it
// (see combat_draw.h). The synth follows the game state, and its beat pulse feeds
// back into the picture through ct_set_beat.
#include "game.h"
#include "audio_synth.h"
#include "combat_draw.h"
#include "demoscene.h"
#include "gameplay.h"
#include "raylib.h"
#include "rlgl.h"
#include <math.h>
#include <stdio.h>

#define LEGEND_SECONDS 8.0f
// World units per arena unit. The corridor's steering box (about 10.4 x 6.8)
// becomes an arena of about 26 x 17: as wide as the original game's play field,
// and taller. Combat timing stays exactly as tuned, because it runs in arena units.
#define ARENA_SCALE 0.4f
#define ARENA_FLOOR_Y 0.8f

typedef enum { STATE_SELECT, STATE_RUN } GameState;

typedef struct Game {
    CtTunnel      *ct;
    SynthSystem    synth;
    bool           audioOn;
    GameState      state;
    uint32_t       selectedSeed, runSeed;
    float          selectorTime, musicIntensity, beatPulse, glitch;
    bool           paused;
    GameplaySystem play;
    CombatDraw     draw;
    ArenaMap       map;
    double         virtualZ;     // original-game distance: drives difficulty, spawns and song structure
    float          lastPlayerX, lastPlayerY;
    bool           havePlayer;
} Game;

static void stop_audio(Game *g)
{
    if (g->audioOn) UnloadAudioSynth(&g->synth);
    g->audioOn = false;
}

// Steering box -> arena bounds and the arena<->world mapping at the ship.
static void update_arena(Game *g)
{
    double s = ct_ship_s(g->ct);
    float half = ct_param_get(g->ct, "steerLimit") * ct_tunnel_radius_at(g->ct, s);
    float vMin = -half, vMax = half;
    if (ct_get_environment(g->ct) == CT_ENV_CORRIDOR) {
        float floorDepth = ct_param_get(g->ct, "corFloorDepth");
        float ceiling = ct_param_get(g->ct, "corCeilingHeight");
        if (floorDepth > 0.0f) vMin = fmaxf(vMin, -floorDepth + 0.6f);
        if (ceiling > 0.0f) vMax = fminf(vMax, ceiling - 0.6f);
    }
    g->map.shipS = s;
    g->map.scale = ARENA_SCALE;
    g->map.yZero = ARENA_FLOOR_Y - vMin / ARENA_SCALE;
    SetGameplayArena(&g->play, (GameplayArena){ -half / ARENA_SCALE, half / ARENA_SCALE,
                                                ARENA_FLOOR_Y, vMax / ARENA_SCALE + g->map.yZero });
}

// Ship offset from the path, in arena units.
static void ship_arena(const Game *g, float *x, float *y)
{
    double ship[3];
    ct_ship_world(g->ct, ship);
    CtPathSample p = ct_path_sample(g->ct, g->map.shipS);
    double d[3] = { ship[0] - p.pos[0], ship[1] - p.pos[1], ship[2] - p.pos[2] };
    double lat = d[0] * p.binormal.x + d[1] * p.binormal.y + d[2] * p.binormal.z;
    double up = d[0] * p.normal.x + d[1] * p.normal.y + d[2] * p.normal.z;
    *x = (float)(lat / ARENA_SCALE);
    *y = (float)(up / ARENA_SCALE + g->map.yZero);
}

static void scene_draw(void *user, const CtSceneView *view, bool emissive)
{
    Game *g = (Game *)user;
    if (g->state == STATE_RUN) combat_draw_scene(&g->draw, g->ct, &g->map, &g->play, view, emissive);
}

static void start_run(Game *g, uint32_t seed, bool withAudio)
{
    stop_audio(g);
    g->runSeed = seed;
    g->musicIntensity = 0.24f;
    g->beatPulse = 0.0f;
    g->glitch = 0.0f;
    g->paused = false;
    g->virtualZ = 0.0;
    g->havePlayer = false;
    InitGameplay(&g->play, seed);
    g->play.bossesEnabled = false;   // bosses arrive with their presentation (slice step 3)
    ct_set_paused(g->ct, false);
    ct_set_ship_damage(g->ct, 0.0f);
    ct_reset_route(g->ct, (uint64_t)seed);
    update_arena(g);
    if (withAudio) {
        InitAudioSynth(&g->synth, seed);
        g->audioOn = IsAudioDeviceReady();
    }
    g->state = STATE_RUN;
}

typedef struct Controls { CtInput flight; bool fire, beam, bomb; } Controls;

static Controls read_controls(void)
{
    Controls c = { 0 };
    c.flight.steerX = (float)((IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) - (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A)));
    c.flight.steerY = (float)((IsKeyDown(KEY_UP) || IsKeyDown(KEY_W)) - (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S)));
    c.flight.boost = IsKeyDown(KEY_SPACE) || IsKeyDown(KEY_LEFT_SHIFT);
    c.flight.rollLeft = IsKeyPressed(KEY_Q);
    c.flight.rollRight = IsKeyPressed(KEY_E);
    c.fire = IsKeyDown(KEY_J) || IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    c.beam = IsKeyPressed(KEY_K);
    c.bomb = IsKeyPressed(KEY_L);
    return c;
}

// Deterministic pilot for --game-shot: weaves, taps and charges on a fixed script.
static Controls scripted_controls(float t)
{
    Controls c = { 0 };
    c.flight.steerX = sinf(t * 0.9f) * 0.8f;
    c.flight.steerY = sinf(t * 0.55f + 1.0f) * 0.6f;
    float cycle = fmodf(t, 2.0f);
    c.fire = cycle < 0.06f || (cycle > 0.4f && cycle < 0.46f) || (cycle > 1.0f && cycle < 1.8f);
    return c;
}

static void play_events(Game *g, const GameplayEvents *e)
{
    if (e->playerHit) g->glitch = 1.0f;
    if (!g->audioOn) return;
    for (int i = 0; i < e->tapShots; i++) TriggerSynthSFX(&g->synth, SFX_LASER_TAP);
    for (int i = 0; i < e->chargeShots; i++) TriggerSynthSFX(&g->synth, SFX_LASER_CHARGE);
    for (int i = 0; i < e->enemiesDestroyed; i++) TriggerSynthSFX(&g->synth, SFX_EXPLOSION);
    if (e->beamFired) TriggerSynthSFX(&g->synth, SFX_LASER_CHARGE);
    if (e->bombFired || e->weaponTierAdvanced) TriggerSynthSFX(&g->synth, SFX_POWER_UP);
    if (e->playerHit) TriggerSynthSFX(&g->synth, SFX_GLITCH_HIT);
    if (e->bossStarted) TriggerSynthSFX(&g->synth, SFX_BOSS_RISER);
    if (e->bossDefeated) TriggerSynthSFX(&g->synth, SFX_CORE_COLLAPSE);
}

// Music follows the original game's mix: difficulty, chain and boost reveal layers.
static void update_music(Game *g, bool boosting, float dt)
{
    const GameplaySystem *p = &g->play;
    float intensity = 0.24f + p->difficulty * 0.20f + fminf((float)p->combo * 0.018f, 0.12f);
    if (boosting) intensity += 0.48f;
    if (p->gameOver) intensity = 0.10f;
    g->musicIntensity = fmaxf(0.0f, fminf(intensity, 1.0f));
    g->glitch = fmaxf(0.0f, g->glitch - dt * 2.5f);
    if (!g->audioOn) { g->beatPulse = 0.0f; return; }
    UpdateAudioSynth(&g->synth, g->musicIntensity, g->glitch, 0.0f, (float)g->virtualZ, 0.0f);
    g->beatPulse = GetSynthTelemetry(&g->synth).beatPulse;
}

static void update_run(Game *g, float dt, const Controls *c)
{
    update_arena(g);
    float px, py;
    ship_arena(g, &px, &py);
    float vx = 0.0f, vy = 0.0f;
    if (g->havePlayer && dt > 0.0f) { vx = (px - g->lastPlayerX) / dt; vy = (py - g->lastPlayerY) / dt; }
    g->lastPlayerX = px; g->lastPlayerY = py; g->havePlayer = true;

    bool boosting = c->flight.boost && CanGameplayBoost(&g->play);
    CtInput flight = c->flight;
    flight.boost = boosting;
    if (g->play.gameOver) flight = (CtInput){ 0 };
    ct_set_input(g->ct, &flight);

    if (!g->play.gameOver) g->virtualZ += GetGameplayTravelSpeed(&g->play, g->virtualZ, boosting) * dt;
    GameplayInput in = { px, py, vx, vy, c->fire && !g->play.gameOver, c->beam, c->bomb };
    GameplayEvents events = UpdateGameplay(&g->play, dt, g->virtualZ, boosting, &in);
    play_events(g, &events);
    ct_set_ship_damage(g->ct, 1.0f - g->play.player.energy / 100.0f);
    update_music(g, boosting, dt);
    ct_set_beat(g->ct, g->beatPulse);
}

static void draw_overlay(const Game *g, int w, int h)
{
    DemosceneSystem demo;
    InitDemoscene(&demo, g->runSeed);
    Color dim = { demo.primary.r, demo.primary.g, demo.primary.b, 170 };
    DrawText("RECURSION_94", 24, 20, 20, dim);
    const char *id = TextFormat("SEED %08X  //  %s  //  %.0f BPM", (unsigned int)g->runSeed,
                                GetSynthSeedKeyName(g->runSeed), GetSynthSeedBpm(g->runSeed));
    DrawText(id, w - MeasureText(id, 14) - 24, h - 30, 14, dim);
    combat_draw_hud(&g->draw, &g->play, w, h);
    if (g->play.runTime < LEGEND_SECONDS && !g->play.gameOver) {
        float a = fminf(1.0f, (LEGEND_SECONDS - g->play.runTime) / 1.5f);
        const char *legend = "WASD STEER   J / MOUSE FIRE (HOLD TO LOCK)   K BEAM   L BOMB   SPACE BOOST   Q / E ROLL   P PAUSE";
        DrawText(legend, w / 2 - MeasureText(legend, 16) / 2, h - 150, 16,
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
    static Game g;   // large (combat pools); keep it off the stack
    g = (Game){ .ct = ct, .state = STATE_SELECT, .selectedSeed = o->seed, .runSeed = o->seed };
    combat_draw_init(&g.draw);
    ct_set_scene_draw(ct, scene_draw, &g);
    bool shotMode = o->shotSeconds >= 0.0;
    // Shot mode is deterministic: no audio thread, fixed step, scripted pilot.
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
            if (g.play.gameOver && IsKeyPressed(KEY_R)) start_run(&g, g.runSeed, true);
            if (IsKeyPressed(KEY_P) && !g.play.gameOver) {
                g.paused = !g.paused;
                ct_set_paused(ct, g.paused);
                if (g.audioOn) { if (g.paused) PauseAudioStream(g.synth.stream); else ResumeAudioStream(g.synth.stream); }
            }
        }

        if (!g.paused) {
            Controls c = shotMode ? scripted_controls(g.play.runTime) : read_controls();
            update_run(&g, dt, &c);
        }
        ct_update(ct, g.paused ? 0.0f : dt);

        BeginDrawing();
        ct_render(ct, NULL, w, h);
        draw_overlay(&g, w, h);
        bool shoot = shotMode && ++frames >= (int)(o->shotSeconds * 60.0 + 0.5);
        if (shoot) save_shot(o->shotOut ? o->shotOut : "shots/game.png");
        EndDrawing();
        if (shoot) break;
    }

    stop_audio(&g);
    ct_set_scene_draw(ct, NULL, NULL);
    combat_draw_unload(&g.draw);
    return 0;
}
