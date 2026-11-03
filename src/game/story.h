// Story told the demoscene way (recursion_94.md §9.3-9.4): a sine scroller with
// the dead group's notes at each level entry, the collapse line after the
// Leviathan, and archetype title cards when a boss arrives.
//
// The base texts play at recursion depth 1. Deeper, story_corrupt rewrites them
// from a hash of (seed, depth): glitched characters, stuttered words and words
// lost to holes. From depth 3, story_compose splices in answer lines (§9.5)
// that are never corrupted. Nothing but the depth-1 texts and the answers is stored.
//
// Everything is timed on the music clock: scrolls start on a bar line and move
// a fixed distance per 16th; the title card's animation is a set of timeline
// tracks (timeline.h) in steps from the boss's arrival.
#ifndef R94_STORY_H
#define R94_STORY_H

#include "gameplay.h"
#include "music_clock.h"
#include <stdbool.h>
#include <stdint.h>

#define STORY_LEVEL_TEXTS BOSSES_PER_RECURSION
#define STORY_COLLAPSE    BOSSES_PER_RECURSION   // index of the collapse line
#define STORY_TEXT_MAX    640

typedef struct Story {
    uint32_t seed;
    int      queue[2];          // texts waiting to scroll, in order
    int      queueDepth[2];
    int      queued;
    double   waitUntil;         // earliest music step for the next scroll
    int      scrollText;        // active text index, -1 = none
    double   scrollStart;       // music step the scroll started on
    char     line[STORY_TEXT_MAX];
    unsigned char voice[STORY_TEXT_MAX];   // 1 = answer character (drawn in the clean voice)
    int      cardRound;         // boss title card: round 0..4, -1 = none
    int      cardDepth;
    double   cardStart;
} Story;

// Text sources.
const char *story_text(int index);           // 0..4 level entries, STORY_COLLAPSE
const char *story_archetype(int round);      // "THRESHOLD GUARDIAN", ...
const char *story_boss_name(int round);      // "RECURSIVE CORE", ...
const char *story_level_name(int round);     // "SERVER CORRIDOR", ... (§9.2, provisional for 2-5)

// Depth rewrite: depth <= 1 copies src. Pure and deterministic. Returns the length.
int story_corrupt(const char *src, uint32_t seed, int depth, char *out, int cap);
// A scroll line: text `index` corrupted for depth, with the answers of §9.5
// spliced in from depth 3 (own answer; a pool line from depth 5). voice (may be
// NULL, cap bytes) marks answer characters with 1. Pure. Returns the length.
int story_compose(int index, uint32_t seed, int depth, char *out, unsigned char *voice, int cap);
// Answer sources (%d = recursion depth).
const char *story_answer(int index);         // own answer of text 0..STORY_COLLAPSE
const char *story_answer_pool(int i);        // shared pool, i wraps
int         story_answer_pool_count(void);

// Run start: queues the level-1 text to scroll after the controls legend.
void story_reset(Story *s, uint32_t seed, int depth, float stepsPerSecond, float legendSeconds);
// Per frame, after UpdateGameplay: boss arrivals and defeats queue cards and texts.
void story_update(Story *s, const GameplaySystem *g, const GameplayEvents *e, const MusicClock *c);
// 2D overlay. Ends the scroll once its last character has left the screen.
void story_draw(Story *s, const GameplaySystem *g, const MusicClock *c, int width, int height);

#endif // R94_STORY_H
