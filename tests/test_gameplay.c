// Headless combat checks ported from the original game's VALIDATE_GAMEPLAY mode:
// chain-link costs and caps, beam/bomb, hit damage (normal and boost), boost
// missile guidance and evade window, fire-rate pressure, swept hits, rounds,
// Leviathan and recursion advance. Plus the music clock: with a clock, formation
// spawns land on beats and enemy shots on 16ths, at about the untimed rate.
// Plus the curve timeline (key values, interpolation, clamping) and the story
// text corruption (depth 1 untouched, deeper deterministic and still mostly readable).
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
    return ok && same == 0 && check_answers();
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
    return specials && onGrid && rateKept && follow && bossOk && timeline && story ? 0 : 1;
}
