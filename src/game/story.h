// Story told the demoscene way (recursion_94.md §9.3-9.4): a sine scroller with
// the dead group's notes at each level entry, the collapse line after the
// Leviathan, and archetype title cards when a boss arrives.
//
// The base texts play at recursion depth 1. Deeper, story_corrupt rewrites them
// from a hash of (seed, depth): glitched characters, stuttered words and words
// lost to holes. Nothing but the depth-1 texts is stored.
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
    int      cardRound;         // boss title card: round 0..4, -1 = none
    int      cardDepth;
    double   cardStart;
} Story;

// Text sources.
const char *story_text(int index);           // 0..4 level entries, STORY_COLLAPSE
const char *story_archetype(int round);      // "THRESHOLD GUARDIAN", ...
const char *story_boss_name(int round);      // "RECURSIVE CORE", ...

// Depth rewrite: depth <= 1 copies src. Pure and deterministic. Returns the length.
int story_corrupt(const char *src, uint32_t seed, int depth, char *out, int cap);

// Run start: queues the level-1 text to scroll after the controls legend.
void story_reset(Story *s, uint32_t seed, float stepsPerSecond, float legendSeconds);
// Per frame, after UpdateGameplay: boss arrivals and defeats queue cards and texts.
void story_update(Story *s, const GameplaySystem *g, const GameplayEvents *e, const MusicClock *c);
// 2D overlay. Ends the scroll once its last character has left the screen.
void story_draw(Story *s, const GameplaySystem *g, const MusicClock *c, int width, int height);

#endif // R94_STORY_H
