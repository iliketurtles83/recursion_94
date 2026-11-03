// RECURSION_94 game: seed selector -> run. The engine flies the ship through the
// corridor; the ported combat runs in arena units that ride the path ahead of it
// (see combat_draw.h). The synth follows the game state, and its beat pulse feeds
// back into the picture through ct_set_beat, and its sample position drives the
// music clock that gameplay timers and flashes lock to (music_clock.h).
#include "game.h"
#include "audio_synth.h"
#include "combat_draw.h"
#include "demoscene.h"
#include "gameplay.h"
#include "music_clock.h"
#include "story.h"
#include "raylib.h"
#include "rlgl.h"
#include <math.h>
#include <stdio.h>

#define LEGEND_SECONDS 8.0f
// World units per arena unit. The corridor's steering box (about 12.8 x 7.4)
// becomes an arena of about 32 x 18.5: a little wider than the original game's
// play field (30), and taller. Combat timing stays exactly as tuned, because it runs in arena units.
#define ARENA_SCALE 0.4f
#define ARENA_FLOOR_Y 0.8f
// Audio rendered but not yet heard, in device callbacks. Tune by ear: raise it
// if hits land early against the kick, lower it if they land late.
#define MUSIC_LATENCY_CALLBACKS 1.0
// Boss push-in: from the first bar line after the boss spawns, the camera
// tightens one notch per bar (eased over the bar's first beat) for this many
// bars, to fovBase * (1 - BOSS_PUSH_AMOUNT). It eases back after the fight.
#define BOSS_PUSH_BARS 4
#define BOSS_PUSH_AMOUNT 0.20f
// Full-stick ship speed in the original game (arena units/s, x and y). Leviathan
// gravity (player.pull) was added to that target velocity; the engine gets the
// same fraction of its own full-stick speed as a drift, so the pull feels as
// strong against the stick as it did.
#define ORIGINAL_STEER_SPEED_X 14.0f
#define ORIGINAL_STEER_SPEED_Y 10.0f

typedef enum { STATE_SELECT, STATE_RUN } GameState;

// Pause menu entries. Everything but RESUME and FULLSCREEN asks to confirm.
typedef enum { MENU_RESUME, MENU_FULLSCREEN, MENU_RESTART, MENU_SELECT, MENU_QUIT, MENU_COUNT } MenuItem;
static const char *const MENU_LABELS[MENU_COUNT] = { "RESUME", "FULLSCREEN", "RECOMPILE RUN", "SEED SELECT", "QUIT" };

typedef struct Game {
    CtTunnel      *ct;
    SynthSystem    synth;
    bool           audioOn;
    GameState      state;
    uint32_t       selectedSeed, runSeed;
    float          selectorTime, musicIntensity, beatPulse, glitch;
    bool           paused;
    int            menuItem;     // pause menu selection (MenuItem)
    bool           confirming;   // pause menu or selector: waiting for yes/no
    GameplaySystem play;
    CombatDraw     draw;
    ArenaMap       map;
    double         virtualZ;     // original-game distance: drives difficulty, spawns and song structure
    float          lastPlayerX, lastPlayerY;
    bool           havePlayer;
    float          baseSpeed;    // preset engine cruise speed; scaled down during boss fights
    float          baseFov;      // preset fovBase; narrowed by the boss push-in
    float          push;         // current push-in, 0..1
    double         pushStartStep; // music step of the first push bar (-1: no boss)
    bool           bossTest;     // --boss: start each run just short of the first boss
    int            bossRound;    // --boss N: that boss instead (1..5)
    bool           showBox;      // F4: steering box and ship centre overlay (prototyping)
    MusicClock     music;
    Story          story;
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
    CtSteerBox b = ct_steer_box(g->ct, s);
    g->map.shipS = s;
    g->map.scale = ARENA_SCALE;
    g->map.yZero = ARENA_FLOOR_Y - b.yMin / ARENA_SCALE;
    SetGameplayArena(&g->play, (GameplayArena){ b.xMin / ARENA_SCALE, b.xMax / ARENA_SCALE,
                                                ARENA_FLOOR_Y, b.yMax / ARENA_SCALE + g->map.yZero });
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
    music_clock_reset(&g->music, GetSynthSeedBpm(seed));
    story_reset(&g->story, seed, g->music.stepsPerSecond, LEGEND_SECONDS - 1.5f);
    if (g->bossTest) {
        // Earlier bosses count as cleared, so round N spawns next (5 = the Leviathan).
        if (g->bossRound > 1) g->play.boss.encounterIndex = g->bossRound - 1;
        g->virtualZ = g->play.boss.nextSpawnDistance - 40.0;
    }
    ct_param_set(g->ct, "baseSpeed", g->baseSpeed);
    ct_param_set(g->ct, "fovBase", g->baseFov);
    g->push = 0.0f;
    g->pushStartStep = -1.0;
    ct_set_paused(g->ct, false);
    ct_set_ship_damage(g->ct, 0.0f);
    ct_reset_route(g->ct, (uint64_t)seed);
    update_arena(g);
    if (withAudio) {
        InitAudioSynthSong(&g->synth, seed, &SYNTH_SONG_CORRIDOR);   // level 1 song
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
// --steer holds the stick instead of weaving; --roll starts one right barrel roll.
static Controls scripted_controls(const GameOptions *o, float t, float dt)
{
    Controls c = { 0 };
    c.flight.steerX = o->steerHold ? o->steerX : sinf(t * 0.9f) * 0.8f;
    c.flight.steerY = o->steerHold ? o->steerY : sinf(t * 0.55f + 1.0f) * 0.6f;
    c.flight.rollRight = o->rollAt >= 0.0 && t >= o->rollAt && t - dt < o->rollAt;
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

#define BOSS_HUSH_WINDOW 350.0
#define BOSS_ARRIVAL_SECONDS 2.6f

// Music follows the original game's mix: difficulty, chain and boost reveal layers.
// Before a boss the arrangement hushes over the last 350 units; it snaps back
// under the riser the moment the boss arrives, and boss phases drive the boss layer.
static void update_music(Game *g, bool boosting, float dt)
{
    const GameplaySystem *p = &g->play;
    const BossState *b = &p->boss;
    float intensity = 0.24f + p->difficulty * 0.20f + fminf((float)p->combo * 0.018f, 0.12f);
    if (boosting) intensity += 0.48f;
    if (b->active) intensity += b->phase == BOSS_SINGULARITY ? 0.30f : b->phase == BOSS_ENRAGED ? 0.24f : 0.14f;
    float hush = 0.0f;
    if (!b->active && p->bossesEnabled) {
        double toBoss = b->nextSpawnDistance - g->virtualZ;
        if (toBoss > 0.0 && toBoss < BOSS_HUSH_WINDOW) hush = (float)(1.0 - toBoss / BOSS_HUSH_WINDOW);
    }
    intensity *= 1.0f - hush * 0.5f;
    if (p->gameOver) intensity = 0.10f;
    g->musicIntensity = fmaxf(0.0f, fminf(intensity, 1.0f));
    float bossLayer = 0.0f;
    if (b->active) {
        switch (b->phase) {
            case BOSS_APPROACH: bossLayer = 0.42f + fminf(b->phaseTime / BOSS_ARRIVAL_SECONDS, 1.0f) * 0.25f; break;
            case BOSS_SHIELDED: bossLayer = 0.70f; break;
            case BOSS_EXPOSED: bossLayer = 0.84f; break;
            default: bossLayer = 1.0f; break;
        }
    }
    g->glitch = fmaxf(0.0f, g->glitch - dt * 2.5f);
    if (!g->audioOn) { g->beatPulse = 0.0f; return; }
    UpdateAudioSynth(&g->synth, g->musicIntensity, g->glitch, bossLayer, (float)g->virtualZ, hush);
    g->beatPulse = GetSynthTelemetry(&g->synth).beatPulse;
}

// With audio the clock follows what is being heard; without (shot mode) it
// free-runs at the seed's BPM, so shots stay reproducible.
static void update_clock(Game *g, float dt)
{
    if (!g->audioOn) {
        music_clock_advance(&g->music, dt);
    } else {
        SynthTelemetry t = GetSynthTelemetry(&g->synth);
        double buffered = MUSIC_LATENCY_CALLBACKS * t.callbackFrames * (t.currentBpm / 15.0) / SAMPLE_RATE;
        music_clock_follow(&g->music, dt, t.musicStep - buffered, t.currentBpm);
    }
    SetGameplayMusicClock(&g->play, &g->music);
}

static float smooth01(float x)
{
    x = fminf(fmaxf(x, 0.0f), 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

// Boss push-in, stepped to the bars of the music.
static void update_push(Game *g, float dt)
{
    const BossState *b = &g->play.boss;
    if (b->active && g->pushStartStep < 0.0)
        g->pushStartStep = (floor(g->music.step / MUSIC_BAR) + 1.0) * MUSIC_BAR;
    if (b->active) {
        double bars = (g->music.step - g->pushStartStep) / MUSIC_BAR;
        float target = 0.0f;
        if (bars >= 0.0) {
            double whole = floor(bars);
            float onBeat = smooth01((float)(bars - whole) * 4.0f);   // ease over the bar's first beat
            target = fminf(((float)whole + onBeat) / BOSS_PUSH_BARS, 1.0f);
        }
        g->push = fmaxf(g->push, target);
    } else {
        g->pushStartStep = -1.0;
        g->push = fmaxf(0.0f, g->push - dt * 0.4f);              // release over ~2.5 s
    }
    ct_param_set(g->ct, "fovBase", g->baseFov * (1.0f - BOSS_PUSH_AMOUNT * smooth01(g->push)));
}

static void update_run(Game *g, float dt, const Controls *c)
{
    update_clock(g, dt);
    update_arena(g);
    float px, py;
    ship_arena(g, &px, &py);
    float vx = 0.0f, vy = 0.0f;
    if (g->havePlayer && dt > 0.0f) { vx = (px - g->lastPlayerX) / dt; vy = (py - g->lastPlayerY) / dt; }
    g->lastPlayerX = px; g->lastPlayerY = py; g->havePlayer = true;

    bool boosting = c->flight.boost && CanGameplayBoost(&g->play);
    CtInput flight = c->flight;
    flight.boost = boosting;
    // Gravity from the previous gameplay step (one frame behind; it changes slowly).
    float vmax = ct_param_get(g->ct, "steerAccel") / fmaxf(ct_param_get(g->ct, "steerDamping"), 0.1f);
    flight.driftX = g->play.player.pull.x / ORIGINAL_STEER_SPEED_X * vmax;
    flight.driftY = g->play.player.pull.y / ORIGINAL_STEER_SPEED_Y * vmax;
    if (g->play.gameOver) flight = (CtInput){ 0 };
    ct_set_input(g->ct, &flight);

    if (!g->play.gameOver) g->virtualZ += GetGameplayTravelSpeed(&g->play, g->virtualZ, boosting) * dt;
    // The engine slows with the original travel speed during boss fights (about a
    // quarter of cruise), so the corridor drifts while the arena holds.
    float factor = GetGameplayTravelSpeed(&g->play, g->virtualZ, false) / GetGameplayCruiseSpeed(&g->play, g->virtualZ);
    ct_param_set(g->ct, "baseSpeed", g->baseSpeed * factor);
    GameplayInput in = { px, py, vx, vy, c->fire && !g->play.gameOver, c->beam, c->bomb,
                         ct_ship_roll_progress(g->ct) >= 0.0f };
    GameplayEvents events = UpdateGameplay(&g->play, dt, g->virtualZ, boosting, &in);
    play_events(g, &events);
    story_update(&g->story, &g->play, &events, &g->music);
    ct_set_ship_damage(g->ct, 1.0f - g->play.player.energy / 100.0f);
    update_music(g, boosting, dt);
    update_push(g, dt);
    ct_set_beat(g->ct, g->beatPulse);
}

// Prototyping overlay: the steering box at the ship's path distance and the ship's
// centre, which the flight model keeps inside it.
static void draw_steer_box(const Game *g, int w, int h)
{
    Camera3D cam = ct_camera(g->ct);
    double o[3], ship[3];
    ct_render_origin(g->ct, o);
    ct_ship_world(g->ct, ship);
    double s = ct_ship_s(g->ct);
    CtPathSample p = ct_path_sample(g->ct, s);
    CtSteerBox b = ct_steer_box(g->ct, s);
    float xs[4] = { b.xMin, b.xMax, b.xMax, b.xMin }, ys[4] = { b.yMin, b.yMin, b.yMax, b.yMax };
    Vector2 q[4];
    for (int i = 0; i < 4; i++) {
        Vector3 v = { (float)(p.pos[0] - o[0]) + p.binormal.x * xs[i] + p.normal.x * ys[i],
                      (float)(p.pos[1] - o[1]) + p.binormal.y * xs[i] + p.normal.y * ys[i],
                      (float)(p.pos[2] - o[2]) + p.binormal.z * xs[i] + p.normal.z * ys[i] };
        q[i] = GetWorldToScreenEx(v, cam, w, h);
    }
    Color line = { 120, 255, 200, 200 };
    for (int i = 0; i < 4; i++) DrawLineEx(q[i], q[(i + 1) % 4], 2.0f, line);
    Vector2 c = GetWorldToScreenEx((Vector3){ (float)(ship[0] - o[0]), (float)(ship[1] - o[1]), (float)(ship[2] - o[2]) }, cam, w, h);
    DrawCircleV(c, 4.0f, (Color){ 255, 60, 140, 255 });
    DrawText(TextFormat("BOX  x %.2f..%.2f  y %.2f..%.2f   [F4]", b.xMin, b.xMax, b.yMin, b.yMax),
             24, 46, 14, line);
}

static void draw_centered(const char *t, int y, int size, Color c, int w)
{
    DrawText(t, w / 2 - MeasureText(t, size) / 2, y, size, c);
}

static void draw_pause_menu(const Game *g, const DemosceneSystem *demo, int w, int h)
{
    DrawRectangle(0, 0, w, h, (Color){ 0, 0, 0, 140 });
    Color dim = { 150, 190, 210, 210 };
    int y = h / 2 - 130;
    draw_centered("PAUSED", y, 40, demo->hot, w);
    y += 70;
    for (int i = 0; i < MENU_COUNT; i++) {
        bool sel = i == g->menuItem;
        const char *label = i == MENU_FULLSCREEN
            ? TextFormat("FULLSCREEN  %s", IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE) ? "ON" : "OFF")
            : MENU_LABELS[i];
        draw_centered(sel ? TextFormat(">  %s  <", label) : label, y, 22, sel ? demo->primary : dim, w);
        y += 34;
    }
    y += 16;
    if (g->confirming) {
        const char *q = g->menuItem == MENU_QUIT ? "QUIT RECURSION_94?" : "ABANDON THIS RUN?";
        draw_centered(q, y, 20, demo->threat, w);
        draw_centered("ENTER YES   //   ESC NO", y + 28, 16, dim, w);
    } else {
        draw_centered("UP / DOWN SELECT   ENTER CONFIRM   ESC RESUME   F11 FULLSCREEN", y, 14, dim, w);
    }
}

static void draw_overlay(Game *g, int w, int h)
{
    if (g->showBox) draw_steer_box(g, w, h);
    DemosceneSystem demo;
    InitDemoscene(&demo, g->runSeed);
    Color dim = { demo.primary.r, demo.primary.g, demo.primary.b, 170 };
    DrawText("RECURSION_94", 24, 20, 20, dim);
    const char *id = TextFormat("SEED %08X  //  %s  //  %.0f BPM", (unsigned int)g->runSeed,
                                GetSynthSeedKeyName(g->runSeed), GetSynthSeedBpm(g->runSeed));
    DrawText(id, w - MeasureText(id, 14) - 24, h - 30, 14, dim);
    combat_draw_hud(&g->draw, &g->play, w, h);
    story_draw(&g->story, &g->play, &g->music, w, h);
    if (g->play.runTime < LEGEND_SECONDS && !g->play.gameOver) {
        float a = fminf(1.0f, (LEGEND_SECONDS - g->play.runTime) / 1.5f);
        const char *legend = "WASD STEER   J / MOUSE FIRE (HOLD TO LOCK)   K BEAM   L BOMB   SPACE BOOST   Q / E ROLL   ESC PAUSE";
        DrawText(legend, w / 2 - MeasureText(legend, 16) / 2, h - 150, 16,
                 (Color){ 200, 230, 240, (unsigned char)(a * 200.0f) });
    }
    if (g->paused) draw_pause_menu(g, &demo, w, h);
}

static void set_paused(Game *g, bool paused)
{
    g->paused = paused;
    g->confirming = false;
    g->menuItem = MENU_RESUME;
    ct_set_paused(g->ct, paused);
    if (g->audioOn) { if (paused) PauseAudioStream(g->synth.stream); else ResumeAudioStream(g->synth.stream); }
}

// Borderless fullscreen: no video mode change, so alt-tab and the compositor behave.
static void toggle_fullscreen(void)
{
    ToggleBorderlessWindowed();
}

static bool alt_down(void) { return IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT); }

// Enter, but not the Enter of Alt+Enter (fullscreen).
static bool enter_pressed(void)
{
    return !alt_down() && (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER));
}

typedef enum { MENU_STAY, MENU_TO_SELECT, MENU_TO_QUIT } MenuResult;

static MenuResult update_pause_menu(Game *g)
{
    if (g->confirming) {
        if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_N)) g->confirming = false;
        else if (enter_pressed() || IsKeyPressed(KEY_Y)) {
            int item = g->menuItem;
            set_paused(g, false);
            if (item == MENU_RESTART) start_run(g, g->runSeed, true);
            return item == MENU_SELECT ? MENU_TO_SELECT : item == MENU_QUIT ? MENU_TO_QUIT : MENU_STAY;
        }
        return MENU_STAY;
    }
    if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_P)) { set_paused(g, false); return MENU_STAY; }
    if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) g->menuItem = (g->menuItem + MENU_COUNT - 1) % MENU_COUNT;
    if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) g->menuItem = (g->menuItem + 1) % MENU_COUNT;
    if (enter_pressed()) {
        if (g->menuItem == MENU_RESUME) set_paused(g, false);
        else if (g->menuItem == MENU_FULLSCREEN) toggle_fullscreen();
        else g->confirming = true;
    }
    return MENU_STAY;
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
    g = (Game){ .ct = ct, .state = STATE_SELECT, .selectedSeed = o->seed, .runSeed = o->seed,
                .baseSpeed = ct_param_get(ct, "baseSpeed"),
                .baseFov = ct_param_get(ct, "fovBase"), .bossTest = o->bossTest, .bossRound = o->bossRound,
                .showBox = o->steerBox || o->shotSeconds < 0.0 };
    combat_draw_init(&g.draw);
    ct_set_scene_draw(ct, scene_draw, &g);
    bool shotMode = o->shotSeconds >= 0.0;
    // Shot mode is deterministic: no audio thread, fixed step, scripted pilot.
    if (o->skipSelector || shotMode) start_run(&g, o->seed, !shotMode);
    int frames = 0;

    while (!WindowShouldClose()) {
        float dt = shotMode ? 1.0f / 60.0f : GetFrameTime();
        int w = GetRenderWidth(), h = GetRenderHeight();

        if (!shotMode && (IsKeyPressed(KEY_F11) || (alt_down() && IsKeyPressed(KEY_ENTER)))) toggle_fullscreen();

        if (g.state == STATE_SELECT) {
            g.selectorTime += dt;
            bool quit = false;
            if (g.confirming) {
                if (enter_pressed() || IsKeyPressed(KEY_Y)) quit = true;
                else if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_N)) g.confirming = false;
            } else {
                if (IsKeyPressed(KEY_ESCAPE)) g.confirming = true;
                if (IsKeyPressed(KEY_LEFT)) g.selectedSeed -= 1u;
                if (IsKeyPressed(KEY_RIGHT)) g.selectedSeed += 1u;
                if (IsKeyPressed(KEY_UP)) g.selectedSeed += 100u;
                if (IsKeyPressed(KEY_DOWN)) g.selectedSeed -= 100u;
                if (enter_pressed()) start_run(&g, g.selectedSeed, true);
            }
            if (quit) break;
            if (g.state != STATE_SELECT) continue;
            BeginDrawing();
            ClearBackground(BLACK);
            DrawSeedSelector(g.selectedSeed, g.selectorTime, GetSynthSeedBpm(g.selectedSeed),
                             GetSynthSeedKeyName(g.selectedSeed), w, h);
            if (g.confirming) {
                DrawRectangle(0, h / 2 - 50, w, 100, (Color){ 0, 0, 0, 200 });
                draw_centered("QUIT RECURSION_94?", h / 2 - 30, 28, (Color){ 255, 80, 140, 255 }, w);
                draw_centered("ENTER YES   //   ESC NO", h / 2 + 10, 16, (Color){ 150, 190, 210, 220 }, w);
            }
            EndDrawing();
            continue;
        }

        if (!shotMode) {
            MenuResult menu = MENU_STAY;
            if (g.paused) {
                menu = update_pause_menu(&g);
            } else if (g.play.gameOver) {
                // Nothing left to lose: Esc goes straight back to the selector.
                if (IsKeyPressed(KEY_ESCAPE)) menu = MENU_TO_SELECT;
                if (IsKeyPressed(KEY_R)) start_run(&g, g.runSeed, true);
            } else if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_P)) {
                set_paused(&g, true);
            }
            if (menu == MENU_TO_QUIT) break;
            if (menu == MENU_TO_SELECT) {
                stop_audio(&g);
                g.selectedSeed = g.runSeed;
                g.confirming = false;
                g.state = STATE_SELECT;
                continue;
            }
            if (IsKeyPressed(KEY_F4)) g.showBox = !g.showBox;
        }

        if (!g.paused) {
            Controls c = shotMode ? scripted_controls(o, g.play.runTime, dt) : read_controls();
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
    ct_param_set(ct, "baseSpeed", g.baseSpeed);
    ct_param_set(ct, "fovBase", g.baseFov);
    ct_set_scene_draw(ct, NULL, NULL);
    combat_draw_unload(&g.draw);
    return 0;
}
