// Headless combat checks ported from the original game's VALIDATE_GAMEPLAY mode:
// chain-link costs and caps, beam/bomb, hit damage (normal and boost), boost
// missile guidance and evade window, fire-rate pressure, swept hits, rounds,
// Leviathan and recursion advance. Plus the music clock: with a clock, formation
// spawns land on beats and enemy shots on 16ths, at about the untimed rate.
// Plus the curve timeline (key values, interpolation, clamping) and the story
// text corruption (depth 1 untouched, deeper deterministic and still mostly readable).
// Plus the boss arrival: no formations while clearing, the boss lands on a bar
// line within BOSS_CLEAR_BARS + 1 bars (the first bar line if the field is
// clear), and survivors are pulled in and explode with no reward or boss damage.
// Plus the Core's desperation lattice: walls alternate with fans, every wall
// shot crosses the player's plane outside the guided lane, and the guide
// leads the wall by about a second.
// Plus the 886ba1b roles: snipers come as a mirrored pair at their own depth
// and commit their aim, carriers launch chasers, supports halve damage nearby.
// Plus the pickup weapons: arc jumps on 16ths up the arpeggio and through
// shields, drill ticks and detonates on a bar line, mirror returns shots for one
// charge per roll, capsules top up / replace, carry over, reset, and drop.
#include "gameplay.h"
#include "music_clock.h"
#include "story.h"
#include "timeline.h"
#include "raylib.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct { int formations, shots, offGridFormations, offGridShots; } GridRun;

static GridRun run_combat(bool withClock, float bpm, bool boss)
{
    static GameplaySystem game;
    GridRun r = { 0 };
    InitGameplay(&game, 94u);
    game.bossesEnabled = boss;
    MusicClock clock;
    music_clock_reset(&clock, bpm);
    clock.valid = withClock;
    GameplayInput in = { 0 };
    in.playerX = 0.0f; in.playerY = 2.4f;
    const float dt = 1.0f / 60.0f;
    for (int frame = 0; frame < 60 * 120; frame++) {
        music_clock_advance(&clock, dt);
        SetGameplayMusicClock(&game, &clock);
        bool wasActive[MAX_ENEMY_PROJECTILES];
        for (int i = 0; i < MAX_ENEMY_PROJECTILES; i++) wasActive[i] = game.projectiles[i].active;
        float spawnBefore = game.spawnTimer;
        game.player.energy = 100.0f;           // keep the run alive
        game.player.invulnerability = 1.0f;
        UpdateGameplay(&game, dt, boss ? 1800.0 : 600.0, false, &in);
        if (game.spawnTimer > spawnBefore) {
            r.formations++;
            if (withClock && !music_clock_crossed(&clock, MUSIC_BEAT)) r.offGridFormations++;
        }
        for (int i = 0; i < MAX_ENEMY_PROJECTILES; i++) {
            if (wasActive[i] || !game.projectiles[i].active) continue;
            r.shots++;
            if (withClock && !music_clock_crossed(&clock, MUSIC_STEP)) r.offGridShots++;
        }
    }
    return r;
}

static bool check_clock_follow(void)
{
    // Follows a stair-stepped (per-callback) source smoothly and monotonically,
    // and jumps back on a restart.
    MusicClock c;
    music_clock_reset(&c, 120.0f);
    double source = 0.0, last = 0.0;
    bool monotonic = true;
    for (int frame = 0; frame < 600; frame++) {
        if (frame % 3 == 0) source += 3.0 * (8.0 / 60.0);   // 8 steps/s, published every 3 frames
        music_clock_follow(&c, 1.0f / 60.0f, source, 120.0f);
        if (c.step < last) monotonic = false;
        last = c.step;
    }
    bool tracks = c.step > source - 0.5 && c.step < source + 0.5;
    music_clock_follow(&c, 1.0f / 60.0f, 0.0, 120.0f);       // synth restarted
    bool restarts = c.step < 0.5;
    return monotonic && tracks && restarts;
}

static bool near(float a, float b) { return fabsf(a - b) < 1e-4f; }

static bool check_timeline(void)
{
    static const TlKey keys[] = { { 0, 0, TL_LINEAR }, { 4, 2, TL_SMOOTH }, { 8, 4, TL_STEP },
                                  { 12, 1, TL_RAMP }, { 16, 3, TL_HERMITE }, { 20, 5, TL_HERMITE }, { 24, 4, TL_STEP } };
    TlTrack t = TL_TRACK(keys);
    bool ok = near(tl_eval(t, -5), 0) && near(tl_eval(t, 99), 4);              // clamped ends
    for (int i = 0; i < t.count; i++) ok = ok && near(tl_eval(t, keys[i].t), keys[i].v);   // hits every key
    ok = ok && near(tl_eval(t, 2), 1) && near(tl_eval(t, 6), 3)                 // linear, smooth midpoint
            && near(tl_eval(t, 11.9), 4) && near(tl_eval(t, 14), 1.5f);        // step holds, ramp u^2
    // Hermite: continuous through the key at 20 (no jump between sides).
    ok = ok && fabsf(tl_eval(t, 19.999) - tl_eval(t, 20.001)) < 0.01f;
    printf("TIMELINE: %s\n", ok ? "yes" : "no");
    return ok;
}

static int count_voice(const unsigned char *v, int n)
{
    int k = 0;
    for (int i = 0; i < n; i++) k += v[i] != 0;
    return k;
}

static int count_nonspace(const char *t)
{
    int k = 0;
    for (; *t; t++) k += *t != ' ';
    return k;
}

// Answers (recursion_94.md §9.5): none below depth 3, the own answer clean at the
// end at depth 3, cutting in from depth 4, a pool line too from depth 5. Answer
// characters are marked in the voice mask and never corrupted.
static bool check_answers(void)
{
    char a[STORY_TEXT_MAX], b[STORY_TEXT_MAX], own[160];
    unsigned char va[STORY_TEXT_MAX], vb[STORY_TEXT_MAX];
    bool ok = true;
    int cuts = 0, cutChecks = 0, pools = 0;
    for (uint32_t seed = 90u; seed < 100u; seed++) {
        for (int text = 0; text <= STORY_COLLAPSE; text++) {
            for (int depth = 1; depth <= 9; depth++) {
                int n = story_compose(text, seed, depth, a, va, sizeof(a));
                story_compose(text, seed, depth, b, vb, sizeof(b));
                ok = ok && strcmp(a, b) == 0 && memcmp(va, vb, (size_t)n) == 0 && n < STORY_TEXT_MAX - 1;
                story_corrupt(story_text(text), seed, depth, b, sizeof(b));
                snprintf(own, sizeof(own), story_answer(text), depth);
                int voiced = count_voice(va, n);
                if (depth < 3) { ok = ok && strcmp(a, b) == 0 && voiced == 0; continue; }
                const char *hit = strstr(a, own);
                ok = ok && hit != NULL;
                if (!hit) continue;
                int at = (int)(hit - a), len = (int)strlen(own);
                for (int i = 0; i < len; i++) ok = ok && (va[at + i] != 0) == (own[i] != ' ');
                int expect = count_nonspace(own);
                if (depth >= 5) {
                    int poolHits = 0;
                    for (int p = 0; p < story_answer_pool_count(); p++) {
                        char line[160];
                        snprintf(line, sizeof(line), story_answer_pool(p), depth);
                        if (strstr(a, line)) { poolHits++; expect += count_nonspace(line); }
                    }
                    ok = ok && poolHits == 1;
                    pools += poolHits;
                }
                ok = ok && voiced == expect;
                if (depth == 3) ok = ok && at + len + 2 == n;          // after the text
                if (depth >= 4) { cutChecks++; if (at + len + 2 < n) cuts++; }
            }
        }
    }
    ok = ok && cuts * 2 > cutChecks;   // most deep answers cut in rather than trail
    story_compose(0, 94u, 5, a, va, sizeof(a));
    printf("ANSWERS: %s (cut in %d/%d, pool lines %d)\n  depth 5: %s\n", ok ? "yes" : "no", cuts, cutChecks, pools, a);
    return ok;
}

// Floor-scroller word wrap: every composed line (all texts, depths 1-6) wraps into
// lines of at most STORY_LINE_CHARS that, rejoined with single spaces, give the
// text back with its spaces collapsed only at the breaks; no line starts or ends
// with a space, and the line count fits.
static bool check_wrap(void)
{
    bool ok = true;
    int worst = 0;
    for (int text = 0; text <= STORY_COLLAPSE; text++) {
        for (int depth = 1; depth <= 6; depth++) {
            char line[STORY_TEXT_MAX];
            int len = story_compose(text, 94u, depth, line, NULL, sizeof(line));
            int start[STORY_LINES_MAX], n[STORY_LINES_MAX];
            int count = story_wrap(line, len, STORY_LINE_CHARS, start, n, STORY_LINES_MAX);
            ok = ok && count > 0 && count < STORY_LINES_MAX;
            if (count > worst) worst = count;
            int at = 0;
            for (int k = 0; k < count; k++) {
                ok = ok && n[k] > 0 && n[k] <= STORY_LINE_CHARS;
                ok = ok && line[start[k]] != ' ' && line[start[k] + n[k] - 1] != ' ';
                while (at < start[k]) ok = ok && line[at++] == ' ';   // only spaces skipped
                at = start[k] + n[k];
            }
            while (at < len) ok = ok && line[at++] == ' ';
        }
    }
    printf("WRAP: %s (at most %d lines of %d chars)\n", ok ? "yes" : "no", worst, STORY_LINE_CHARS);
    return ok;
}

static bool check_story(void)
{
    char a[STORY_TEXT_MAX], b[STORY_TEXT_MAX];
    bool ok = true;
    for (int i = 0; i <= STORY_COLLAPSE; i++) {
        story_corrupt(story_text(i), 94u, 1, a, sizeof(a));
        ok = ok && strcmp(a, story_text(i)) == 0 && strlen(story_text(i)) < STORY_TEXT_MAX / 2;
    }
    int same = 0, total = 0;
    for (int depth = 2; depth <= 8; depth++) {
        story_corrupt(story_text(0), 94u, depth, a, sizeof(a));
        story_corrupt(story_text(0), 94u, depth, b, sizeof(b));
        ok = ok && strcmp(a, b) == 0;                                            // deterministic
        ok = ok && (strcmp(a, story_text(0)) != 0);                              // something changed
        story_corrupt(story_text(0), 95u, depth, b, sizeof(b));
        if (strcmp(a, b) == 0) same++;
        total++;
    }
    // Deep text stays mostly readable: count unchanged characters at depth 8 when no
    // stutter shifted the line (compare letters present).
    story_corrupt(story_text(0), 94u, 8, a, sizeof(a));
    ok = ok && strlen(a) > strlen(story_text(0)) * 3 / 4;
    printf("STORY: %s (seeds differ at %d/%d depths)\n", ok && same == 0 ? "yes" : "no", total - same, total);
    return ok && same == 0 && check_answers() && check_wrap();
}

typedef struct { bool ok; int survivors, absorbed, bars; } Arrival;

static Arrival run_arrival(bool clearField)
{
    static GameplaySystem game;
    Arrival a = { 0 };
    InitGameplay(&game, 94u);
    MusicClock clock;
    music_clock_reset(&clock, 129.0f);
    GameplayInput in = { 0 };
    in.playerY = 2.4f;
    const float dt = 1.0f / 60.0f;
    bool ok = true;
    for (int frame = 0; frame < 60 * 10; frame++) {   // fill the field
        music_clock_advance(&clock, dt);
        SetGameplayMusicClock(&game, &clock);
        game.player.invulnerability = 1.0f;
        UpdateGameplay(&game, dt, 600.0, false, &in);
    }
    if (clearField) for (int i = 0; i < MAX_ENEMIES; i++) game.enemies[i].active = false;
    double z = game.boss.nextSpawnDistance;
    double startStep = -1.0;
    int score = 0, spawnFrame = -1;
    for (int frame = 0; frame < 60 * 12; frame++) {
        music_clock_advance(&clock, dt);
        SetGameplayMusicClock(&game, &clock);
        game.player.invulnerability = 1.0f;
        float spawnBefore = game.spawnTimer;
        bool wasActive = game.boss.active;
        GameplayEvents e = UpdateGameplay(&game, dt, z, false, &in);
        if (startStep < 0.0 && game.boss.clearing) startStep = clock.prevStep;
        if (game.boss.clearing && game.spawnTimer > spawnBefore) ok = false;   // a formation spawned
        if (!wasActive && game.boss.active) {
            spawnFrame = frame;
            if (!music_clock_crossed(&clock, MUSIC_BAR)) ok = false;
            a.bars = (int)ceil((clock.step - startStep) / MUSIC_BAR);
            score = game.score;
            for (int i = 0; i < MAX_ENEMIES; i++) {
                if (game.enemies[i].active && game.enemies[i].phase == ENEMY_ABSORBED) a.survivors++;
            }
        }
        a.absorbed += e.enemiesAbsorbed;
        if (spawnFrame >= 0 && frame == spawnFrame + 90) break;   // 1.5 s after arrival
    }
    if (spawnFrame < 0) ok = false;
    else {
        const Enemy *core = &game.enemies[game.boss.coreSlot];
        if (game.score != score || core->health != game.boss.maxHealth) ok = false;
        for (int i = 0; i < MAX_ENEMIES; i++) {
            if (game.enemies[i].active && game.enemies[i].phase == ENEMY_ABSORBED) ok = false;
        }
        if (a.absorbed != a.survivors) ok = false;
        if (clearField ? a.bars > 1 : a.bars > 3) ok = false;
        if (!clearField && a.survivors == 0) ok = false;   // the window should leave some to absorb
    }
    a.ok = ok;
    return a;
}

static bool check_boss_arrival(void)
{
    Arrival busy = run_arrival(false);
    Arrival clear = run_arrival(true);
    bool ok = busy.ok && clear.ok;
    printf("BOSS ARRIVAL: %s (busy field: %d bars, %d/%d absorbed; clear field: %d bar)\n",
           ok ? "yes" : "no", busy.bars, busy.absorbed, busy.survivors, clear.bars);
    return ok;
}

static bool check_lattice(void)
{
    static GameplaySystem game;
    InitGameplay(&game, 94u);
    game.boss.nextSpawnDistance = 0.0;
    GameplayInput in = { 0 };
    in.playerY = 2.4f;
    const float dt = 1.0f / 60.0f;
    UpdateGameplay(&game, dt, 1.0, false, &in);          // boss arrives (no clock: field clear)
    for (int n = 0; n < 4; n++) {
        int slot = game.boss.nodeSlots[n];
        if (slot >= 0) game.enemies[slot].active = false;
    }
    game.boss.shieldNodes = 0;
    game.boss.phase = BOSS_EXPOSED;
    game.enemies[game.boss.coreSlot].health = game.boss.maxHealth * 0.40f;   // -> enraged

    int walls = 0, volleys = 0, worstMiss = 0, fewest = 1000;
    float guideLead = 0.0f, minLead = 1000.0f, widest = 0.0f;
    bool ok = true;
    for (int frame = 0; frame < 60 * 20; frame++) {
        game.player.energy = 100.0f;
        game.player.invulnerability = 1.0f;
        bool was[MAX_ENEMY_PROJECTILES];
        for (int i = 0; i < MAX_ENEMY_PROJECTILES; i++) was[i] = game.projectiles[i].active;
        float guideBefore = game.boss.latticeGuide;
        GameplayEvents e = UpdateGameplay(&game, dt, 1.0, false, &in);
        if (guideBefore <= 0.0f && game.boss.latticeGuide > 0.0f) guideLead = 0.0f;
        guideLead += dt;
        volleys += e.enemyVolleys;
        int fresh = 0;
        float xs[MAX_ENEMY_PROJECTILES];
        for (int i = 0; i < MAX_ENEMY_PROJECTILES; i++) {
            const EnemyProjectile *p = &game.projectiles[i];
            if (was[i] || !p->active || p->velocity.z <= 0.0f) continue;
            float t = (game.player.position.z - p->position.z) / p->velocity.z;
            xs[fresh++] = p->position.x + p->velocity.x * t;
        }
        if (fresh < 20) continue;                          // a fan or reinforcements
        walls++;
        if (fresh < fewest) fewest = fresh;
        if (guideLead < minLead) minLead = guideLead;
        for (int i = 0; i < fresh; i++) {
            if (fabsf(xs[i] - game.boss.latticeLane) < BOSS_LATTICE_GAP - 1e-3f) worstMiss++;
        }
        // Columns either side of the lane must sit no more than one spacing apart
        // elsewhere (no accidental second gap).
        for (int i = 0; i < fresh; i++) {
            float nearest = 1000.0f;
            for (int j = 0; j < fresh; j++) {
                float d = fabsf(xs[j] - xs[i]);
                if (d > 0.5f && d < nearest) nearest = d;
            }
            if (nearest < 1000.0f && nearest > widest) widest = nearest;
        }
    }
    if (game.boss.phase != BOSS_ENRAGED || walls < 3 || worstMiss > 0 || minLead < 1.0f ||
        widest > 2.6f || volleys < walls * 2 - 1) ok = false;
    printf("LATTICE: %s (%d walls of >=%d shots in %d volleys, guide lead %.2f s, shots in lane %d, column step %.2f)\n",
           ok ? "yes" : "no", walls, fewest, volleys, minLead, worstMiss, widest);
    return ok;
}

static bool check_roles(void)
{
    static GameplaySystem game;
    InitGameplay(&game, 94u);
    game.bossesEnabled = false;
    GameplayInput in = { 0 };
    in.playerY = 2.4f;
    const float dt = 1.0f / 60.0f;
    int sniperPairs = 0, carriers = 0, supports = 0, launches = 0, marks = 0, aimMoved = 0;
    int sniperShots = 0, sniperMisses = 0;
    bool seen[MAX_ENEMIES] = { 0 };
    int seenSerial[MAX_ENEMIES];
    float holdZ[3] = { 0, 0, 0 };
    Vector3 lastAim[MAX_ENEMIES];
    for (int frame = 0; frame < 60 * 180; frame++) {
        game.player.energy = 100.0f;
        game.player.invulnerability = 1.0f;
        in.playerX = sinf(frame * 0.01f) * 6.0f;          // keep moving: the aim must not follow
        int chasersBefore = 0;
        for (int i = 0; i < MAX_ENEMIES; i++)
            chasersBefore += game.enemies[i].active && game.enemies[i].type == ENEMY_CHASER && game.enemies[i].generation > 0;
        bool shotWas[MAX_ENEMY_PROJECTILES];
        for (int i = 0; i < MAX_ENEMY_PROJECTILES; i++) shotWas[i] = game.projectiles[i].active;
        UpdateGameplay(&game, dt, 1000.0, false, &in);
        int chasersAfter = 0, newSnipers = 0;
        for (int i = 0; i < MAX_ENEMIES; i++) {
            Enemy *e = &game.enemies[i];
            if (!e->active) { seen[i] = false; continue; }
            chasersAfter += e->type == ENEMY_CHASER && e->generation > 0;
            if (!seen[i] || seenSerial[i] != e->identity) {
                seen[i] = true; seenSerial[i] = e->identity;
                if (e->type == ENEMY_SNIPER) newSnipers++;
                if (e->type == ENEMY_CARRIER) carriers++;
                if (e->type == ENEMY_SUPPORT) supports++;
            }
            if (e->type >= ENEMY_SNIPER && e->phase == ENEMY_HOVER) holdZ[e->type - ENEMY_SNIPER] = e->position.z;
            if (e->type == ENEMY_SNIPER && e->phase == ENEMY_TELEGRAPH) {
                if (e->phaseTime <= dt * 1.5f) marks++;
                else if (fabsf(lastAim[i].x - e->aim.x) > 1e-4f) aimMoved++;
                lastAim[i] = e->aim;
            }
        }
        if (newSnipers == 2) sniperPairs++;
        if (chasersAfter > chasersBefore) launches++;
        for (int i = 0; i < MAX_ENEMY_PROJECTILES; i++) {
            const EnemyProjectile *p = &game.projectiles[i];
            if (shotWas[i] || !p->active) continue;
            float speed = sqrtf(p->velocity.x * p->velocity.x + p->velocity.y * p->velocity.y + p->velocity.z * p->velocity.z);
            if (fabsf(speed - 46.0f) > 0.5f) continue;     // the sniper shot
            sniperShots++;
            // It must cross the player's plane at an aim point committed earlier,
            // not where the player is now.
            float t = (game.player.position.z - p->position.z) / p->velocity.z;
            float x = p->position.x + p->velocity.x * t;
            if (fabsf(x - game.player.position.x) < 1e-3f && fabsf(in.playerX) > 0.5f) sniperMisses++;
        }
    }
    // Support: an enemy next to one takes half damage.
    InitGameplay(&game, 94u);
    game.enemies[0] = (Enemy){ .active = true, .type = ENEMY_DRIFTER, .health = 1.0f, .position = { 0, 3, -25 } };
    game.enemies[1] = (Enemy){ .active = true, .type = ENEMY_SUPPORT, .health = 3.0f, .position = { 0, 4, -33 } };
    game.enemies[2] = (Enemy){ .active = true, .type = ENEMY_DRIFTER, .health = 1.0f, .position = { 14, 3, -25 } };
    bool shielded = GetEnemySupport(&game, &game.enemies[0]) == 1 && GetEnemySupport(&game, &game.enemies[2]) < 0 &&
                    GetEnemySupport(&game, &game.enemies[1]) < 0;
    bool ok = sniperPairs >= 2 && carriers >= 1 && supports >= 1 && launches >= 2 && marks >= 2 &&
              aimMoved == 0 && sniperShots >= 2 && sniperMisses == 0 && shielded &&
              holdZ[0] < holdZ[1] && holdZ[1] < holdZ[2] && holdZ[2] < -30.0f;
    printf("ROLES: %s (sniper pairs %d, marks %d, shots %d, aim moved %d; carriers %d, launches %d; supports %d, shield %s; hold z %.0f/%.0f/%.0f)\n",
           ok ? "yes" : "no", sniperPairs, marks, sniperShots, aimMoved, carriers, launches, supports,
           shielded ? "yes" : "no", holdZ[0], holdZ[1], holdZ[2]);
    return ok;
}

// ---------------------------------------------------------------------------
// Pickup weapons

static void weapon_world(GameplaySystem *g)
{
    InitGameplay(g, 94u);
    g->bossesEnabled = false;
    g->spawnTimer = 1e9f;   // no formations: the test places its own enemies
}

static Enemy *place(GameplaySystem *g, int slot, EnemyType type, float health, Vector3 p)
{
    Enemy *e = &g->enemies[slot];
    *e = (Enemy){ 0 };
    e->active = true; e->type = type; e->health = health;
    e->size = type == ENEMY_CARRIER ? (Vector3){ 4.4f, 1.8f, 4.4f } : (Vector3){ 1.8f, 1.8f, 1.8f };
    e->position = p; e->basePosition = p;
    e->phase = ENEMY_HOVER;
    e->fireTimer = 1e9f;    // keep the test field quiet
    return e;
}

// Steps the game; `press` taps I on the first frame. Calls back per frame via counters.
typedef struct { int notes, noteFrame[32], degree[32], kind[32], detonations, detonationFrame, empty, fired, collected; } WeaponLog;

static void step(GameplaySystem *g, GameplayInput *in, MusicClock *clock, int frames, bool press, WeaponLog *log, int *frame)
{
    const float dt = 1.0f / 60.0f;
    for (int f = 0; f < frames; f++, (*frame)++) {
        g->player.energy = 100.0f;
        g->player.invulnerability = 1.0f;
        for (int i = 0; i < MAX_ENEMIES; i++) if (g->enemies[i].active) g->enemies[i].fireTimer = 1e9f;
        if (clock) { music_clock_advance(clock, dt); SetGameplayMusicClock(g, clock); }
        in->weaponPressed = press && f == 0;
        GameplayEvents e = UpdateGameplay(g, dt, 1000.0, false, in);
        for (int k = 0; k < e.weaponNotes && log->notes < 32; k++, log->notes++) {
            log->noteFrame[log->notes] = *frame;
            log->degree[log->notes] = e.weaponNoteDegree[k];
            log->kind[log->notes] = e.weaponNoteKind[k];
        }
        if (e.drillDetonations) { log->detonations += e.drillDetonations; log->detonationFrame = *frame; }
        log->empty += e.weaponEmpty;
        log->fired += e.weaponFired;
        log->collected += e.weaponCollected;
    }
}

static bool check_weapons(void)
{
    static GameplaySystem g;
    GameplayInput in = { 0 };
    in.playerY = 2.4f;
    int frame = 0;

    // ARC: five drifters in a loose chain; one cast kills them all, one jump per
    // 16th (no clock: 0.125 s = 7-8 frames), climbing the arpeggio.
    weapon_world(&g);
    g.weapon = WEAPON_ARC; g.ammo = 10;
    for (int i = 0; i < 5; i++) place(&g, i, ENEMY_DRIFTER, 1.0f, (Vector3){ -2.0f + i * 5.0f, 2.4f, -30.0f - i * 3.0f });
    WeaponLog arc = { 0 };
    step(&g, &in, NULL, 90, true, &arc, &frame);
    int alive = 0;
    for (int i = 0; i < 5; i++) alive += g.enemies[i].active;
    bool arcKills = alive == 0 && g.ammo == 9 && arc.notes == 5;
    bool arcClimbs = arc.notes == 5 && arc.degree[0] == 0 && arc.degree[1] == 2 && arc.degree[2] == 4 && arc.degree[3] == 7;
    bool arcGrid = true;
    for (int k = 1; k < arc.notes; k++) { int gap = arc.noteFrame[k] - arc.noteFrame[k - 1]; arcGrid &= gap >= 7 && gap <= 8; }

    // ARC ignores the support's shield: a 3-health drifter guarded by a support
    // takes the full 2 (a tap would do half).
    weapon_world(&g);
    g.weapon = WEAPON_ARC; g.ammo = 10;
    Enemy *guarded = place(&g, 0, ENEMY_DRIFTER, 3.0f, (Vector3){ 0.0f, 2.4f, -25.0f });
    place(&g, 1, ENEMY_SUPPORT, 30.0f, (Vector3){ 4.0f, 3.0f, -30.0f });
    WeaponLog sh = { 0 };
    step(&g, &in, NULL, 3, true, &sh, &frame);
    bool shieldIgnored = guarded->active && fabsf(guarded->health - 1.0f) < 1e-3f;

    // Nothing in range: no ammo spent.
    weapon_world(&g);
    g.weapon = WEAPON_ARC; g.ammo = 10;
    WeaponLog dud = { 0 };
    step(&g, &in, NULL, 5, true, &dud, &frame);
    bool dudFree = g.ammo == 10 && dud.empty == 1;

    // DRILL with the music clock: sticks to a carrier, ticks on 16ths and
    // detonates on a bar line at least DRILL_MIN_STUCK after sticking.
    weapon_world(&g);
    MusicClock clock;
    music_clock_reset(&clock, 129.0f);
    g.weapon = WEAPON_DRILL; g.ammo = 5;
    Enemy *carrier = place(&g, 0, ENEMY_CARRIER, 40.0f, (Vector3){ 1.0f, 2.4f, -30.0f });
    WeaponLog dr = { 0 };
    int startFrame = frame;
    // Count bar crossings frame by frame alongside.
    step(&g, &in, &clock, 1, true, &dr, &frame);
    int detonatedOnBar = -1;
    for (int f = 0; f < 240 && dr.detonations == 0; f++) {
        step(&g, &in, &clock, 1, false, &dr, &frame);
        if (dr.detonations) detonatedOnBar = music_clock_crossed(&clock, MUSIC_BAR);
    }
    int ticks = 0;
    for (int k = 0; k < dr.notes; k++) ticks += dr.kind[k] == WEAPON_DRILL;
    float dealt = 40.0f - carrier->health;
    bool drillOk = dr.detonations == 1 && detonatedOnBar == 1 && g.ammo == 4 && ticks >= 3 &&
                   fabsf(dealt - (ticks * 0.3f + 4.0f)) < 0.05f;
    (void)startFrame;

    // MIRROR: a sniper's shot caught by a rolling ship goes back to the sniper;
    // one charge per roll, however many shots it catches.
    weapon_world(&g);
    g.weapon = WEAPON_MIRROR; g.ammo = 4;
    Enemy *sniper = place(&g, 0, ENEMY_SNIPER, 2.0f, (Vector3){ 6.0f, 5.0f, -45.0f });
    sniper->size = (Vector3){ 1.0f, 1.0f, 3.2f };
    for (int k = 0; k < 2; k++) {
        EnemyProjectile *p = &g.projectiles[k];
        p->active = true; p->life = 6.0f;
        p->position = (Vector3){ 6.0f, 5.0f, -43.0f - k * 4.0f };
        Vector3 d = { 0.0f - 6.0f, 2.4f - 5.0f, 0.0f + 43.0f + k * 4.0f };
        float l = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
        p->velocity = (Vector3){ d.x / l * 46.0f, d.y / l * 46.0f, d.z / l * 46.0f };
    }
    in.dodging = true;
    WeaponLog mi = { 0 };
    step(&g, &in, NULL, 75, false, &mi, &frame);
    in.dodging = false;
    bool caughtBoth = !g.projectiles[0].active && !g.projectiles[1].active;
    bool oneCharge = g.ammo == 3;
    step(&g, &in, NULL, 60, false, &mi, &frame);
    bool sniperDown = !sniper->active;

    // Capsules: same type tops up (to twice a magazine), another type replaces.
    weapon_world(&g);
    WeaponLog cap = { 0 };
    PickupWeapon order[4] = { WEAPON_ARC, WEAPON_ARC, WEAPON_ARC, WEAPON_DRILL };
    int ammoAfter[4];
    for (int k = 0; k < 4; k++) {
        g.capsules[0] = (WeaponCapsule){ .position = { 0.0f, 2.4f, -12.0f }, .type = order[k], .active = true };
        step(&g, &in, NULL, 60, false, &cap, &frame);
        ammoAfter[k] = g.ammo;
    }
    bool pickups = cap.collected == 4 && ammoAfter[0] == 10 && ammoAfter[1] == 20 && ammoAfter[2] == 20 &&
                   g.weapon == WEAPON_DRILL && ammoAfter[3] == 5;
    AdvanceGameplayLoop(&g, 95u, 3000.0);
    bool carried = g.weapon == WEAPON_DRILL && g.ammo == 5;
    InitGameplay(&g, 94u);
    bool resetOnDeath = g.weapon == WEAPON_NONE && g.ammo == 0;

    // Drops in play: a pilot that hunts the nearest enemy and taps; capsules
    // come from wiped groups, deterministic per seed, all three types.
    int drops[2] = { 0, 0 }, types[WEAPON_COUNT] = { 0 };
    for (int run = 0; run < 2; run++) {
        InitGameplay(&g, 94u);
        g.bossesEnabled = false;
        GameplayInput hunt = { 0 };
        hunt.playerY = 2.4f;
        for (int f = 0; f < 60 * 240; f++) {
            g.player.energy = 100.0f;
            g.player.invulnerability = 1.0f;
            int best = -1; float bz = -1e9f;
            for (int i = 0; i < MAX_ENEMIES; i++)
                if (g.enemies[i].active && g.enemies[i].position.z < -8.0f && g.enemies[i].position.z > bz) { best = i; bz = g.enemies[i].position.z; }
            if (best >= 0) { hunt.playerX = g.enemies[best].position.x; hunt.playerY = g.enemies[best].position.y; }
            hunt.fireHeld = (f % 6) < 2;
            GameplayEvents e = UpdateGameplay(&g, 1.0f / 60.0f, 300.0 + f * 0.9, false, &hunt);
            drops[run] += e.capsulesDropped;
            if (run == 0 && e.capsulesDropped)
                for (int c = 0; c < MAX_CAPSULES; c++) if (g.capsules[c].active && g.capsules[c].age <= 1.5f / 60.0f) types[g.capsules[c].type]++;
        }
    }
    bool dropsOk = drops[0] >= 6 && drops[0] <= 240 / 12 + 1 && drops[0] == drops[1] && types[WEAPON_ARC] && types[WEAPON_DRILL] && types[WEAPON_MIRROR];

    bool ok = arcKills && arcClimbs && arcGrid && shieldIgnored && dudFree && drillOk && caughtBoth &&
              oneCharge && sniperDown && pickups && carried && resetOnDeath && dropsOk;
    printf("WEAPONS: %s (arc kills %d/5 notes %d grid %s shield-ignored %s dud-free %s; drill ticks %d dealt %.2f on-bar %d; "
           "mirror caught %s one-charge %s sniper-down %s; pickups %s carried %s reset %s; drops %d/%d types %d/%d/%d)\n",
           ok ? "yes" : "no", 5 - alive, arc.notes, arcGrid ? "yes" : "no", shieldIgnored ? "yes" : "no",
           dudFree ? "yes" : "no", ticks, dealt, detonatedOnBar, caughtBoth ? "yes" : "no", oneCharge ? "yes" : "no",
           sniperDown ? "yes" : "no", pickups ? "yes" : "no", carried ? "yes" : "no", resetOnDeath ? "yes" : "no",
           drops[0], drops[1], types[WEAPON_ARC], types[WEAPON_DRILL], types[WEAPON_MIRROR]);
    return ok;
}

int main(void)
{
    SetTraceLogLevel(LOG_INFO);
    bool specials = ValidateGameplaySpecials();

    GridRun plain = run_combat(false, 129.0f, false);
    GridRun timed = run_combat(true, 129.0f, false);
    GridRun bossPlain = run_combat(false, 129.0f, true);
    GridRun bossTimed = run_combat(true, 129.0f, true);
    float bossRatio = (float)bossTimed.shots / (float)(bossPlain.shots ? bossPlain.shots : 1);
    bool bossOk = bossTimed.offGridShots == 0 && bossTimed.shots > 50 && bossRatio > 0.80f && bossRatio < 1.20f;
    bool onGrid = timed.offGridFormations == 0 && timed.offGridShots == 0 &&
                  timed.formations > 20 && timed.shots > 20;
    float spawnRatio = (float)timed.formations / (float)(plain.formations ? plain.formations : 1);
    float shotRatio = (float)timed.shots / (float)(plain.shots ? plain.shots : 1);
    bool rateKept = spawnRatio > 0.85f && spawnRatio < 1.15f && shotRatio > 0.80f && shotRatio < 1.20f;
    bool follow = check_clock_follow();
    printf("MUSIC CLOCK boss: shots %d/%d off, rate x%.2f -> %s\n", bossTimed.offGridShots, bossTimed.shots,
           bossRatio, bossOk ? "yes" : "no");
    printf("MUSIC CLOCK: on-grid=%s (formations %d/%d off, shots %d/%d off) rate=%s (spawns x%.2f, shots x%.2f) follow=%s\n",
           onGrid ? "yes" : "no", timed.offGridFormations, timed.formations, timed.offGridShots, timed.shots,
           rateKept ? "yes" : "no", spawnRatio, shotRatio, follow ? "yes" : "no");
    bool timeline = check_timeline();
    bool story = check_story();
    bool arrival = check_boss_arrival();
    bool lattice = check_lattice();
    bool roles = check_roles();
    bool weapons = check_weapons();
    return specials && onGrid && rateKept && follow && bossOk && timeline && story && arrival && lattice && roles &&
           weapons ? 0 : 1;
}
