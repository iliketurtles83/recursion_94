// Title and level-transition overlays, animated by curve-timeline tracks
// (timeline.h) on the music clock:
//
//   intro       run start: RECURSION_94 drops in letter by letter over copper
//               bars, the stack frame and loader lines type out, then the
//               title flies into the corner logo (3 bars)
//   transition  a boss falls: a raster wipe, "> CALL FRAME n" typed with a
//               cursor and the next level's name closing in (2 bars, from
//               the next bar line); after the Leviathan, "> CALL RECURSION_94"
//               and the new depth
//
// titles_draw_logo is the shared letter drop-in, also used by the seed selector.
#ifndef R94_TITLES_H
#define R94_TITLES_H

#include "gameplay.h"
#include "music_clock.h"
#include "raylib.h"
#include <stdbool.h>
#include <stdint.h>

#define TITLES_INTRO_STEPS 48.0        // three bars
#define TITLES_TRANSITION_STEPS 32.0   // two bars

typedef struct Titles {
    uint32_t seed;
    int      introDepth;
    double   introStart;      // music step the intro started on
    int      transRound;      // round being entered 0..4, -1 = no transition
    int      transDepth;
    bool     transRecurse;    // the Leviathan fell: a new recursion
    double   transStart;      // music step (a bar line)
} Titles;

void titles_reset(Titles *t, uint32_t seed, int depth, const MusicClock *c);
// Per frame, after UpdateGameplay.
void titles_update(Titles *t, const GameplaySystem *g, const GameplayEvents *e, const MusicClock *c);
// False while the intro title is still on its way to the corner.
bool titles_logo_settled(const Titles *t, const MusicClock *c);
// 2D overlay. keyName and bpm describe the seed for the loader line.
void titles_draw(const Titles *t, const GameplaySystem *g, const MusicClock *c, int width, int height,
                 const char *keyName, float bpm);

// Letter drop-in at time `steps` (16ths): letter i starts falling at i * stagger
// steps, overshoots a little and lands; it lands hot and cools to `color`.
// Drawn with the default font at DrawText spacing, left edge x, top y.
void titles_draw_logo(const char *text, float x, float y, float size, double steps, float stagger,
                      Color color, Color hot, float alpha);
float titles_logo_width(const char *text, float size);

#endif // R94_TITLES_H
