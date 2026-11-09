// Title and level-transition overlays, animated by curve-timeline tracks
// (timeline.h) on the music clock:
//
//   intro       run start: RECURSION_94 drops in letter by letter over copper
//               bars, the stack frame and loader lines type out, then the
//               title flies into the corner logo (3 bars)
//   result      a boss falls ("> RETURN FRAME n"): the frame's tally types in
//               one line per beat, values counting up, while the ship glides
//               through the old level; it holds, fades and the game continues
//               on its own (4 bars). The ship falls ("SIGNAL LOST"): the same
//               tally, then the keys (R, Esc).
//   transition  the game continues: a raster wipe, "> CALL FRAME n" typed
//               with a cursor and the next level's name closing in (2 bars,
//               from the next bar line); after the Leviathan,
//               "> CALL RECURSION_94" and the new depth
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
#define TITLES_RESULT_LINES 7          // tally lines, one per beat (the last is the run score)

// One frame's tally (a stage: from its start, or the last continue, to its
// boss falling or the ship's death).
typedef struct StageResult {
    bool  cleared;        // the boss fell (false: signal lost)
    bool  recursion;      // the boss was the Leviathan
    int   round, depth;   // frame 0..4 and recursion depth it was played at
    float seconds;
    int   kills, spawned, maxChain, hits, grazes;
    int   frameScore, score;
} StageResult;

typedef struct Titles {
    uint32_t seed;
    int      introDepth;
    double   introStart;      // music step the intro started on
    int      transRound;      // round being entered 0..4, -1 = no transition
    int      transDepth;
    bool     transRecurse;    // the Leviathan fell: a new recursion
    double   transStart;      // music step (a bar line)
    bool        resultUp;
    double      resultStart;  // music step the tally starts on (a bar line)
    StageResult result;
} Titles;

void titles_reset(Titles *t, uint32_t seed, int depth, const MusicClock *c);
// Per frame, after UpdateGameplay.
void titles_update(Titles *t, const GameplaySystem *g, const GameplayEvents *e, const MusicClock *c);
// The tally from `start` (a music step). Hidden by titles_continue or a reset.
void titles_show_result(Titles *t, const StageResult *r, double start);
// Tally lines revealed so far (0..TITLES_RESULT_LINES); the game plays a note per line.
int titles_result_revealed(const Titles *t, const MusicClock *c);
// A cleared tally has played out (held and faded): time to continue.
bool titles_result_done(const Titles *t, const MusicClock *c);
// Hide the tally and start the transition into the next frame from the next bar line.
void titles_continue(Titles *t, const GameplaySystem *g, const MusicClock *c);
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
