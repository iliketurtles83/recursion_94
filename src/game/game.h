// RECURSION_94 game layer: seed selector and the run loop on top of the engine.
// Talks to the engine only through cybertunnel.h.
#ifndef R94_GAME_H
#define R94_GAME_H

#include <stdbool.h>
#include <stdint.h>
#include "cybertunnel.h"

typedef struct GameOptions {
    uint32_t    seed;          // initial selector seed, or the run seed when skipSelector
    bool        skipSelector;  // --seed N: start the run immediately
    double      shotSeconds;   // >= 0: deterministic run screenshot after this many seconds, then exit
    const char *shotOut;       // screenshot path for shotSeconds
    bool        bossTest;      // --boss: start each run just short of the first boss
    int         bossRound;     // --boss N: ... of boss N of the recursion (1..5; 0 = 1)
    int         depth;         // --depth N: start the run at recursion depth N (story, difficulty; 0 = 1)
    bool        steerHold;     // --steer X,Y: the shot pilot holds this stick (no weave)
    float       steerX, steerY;
    double      rollAt;        // --roll T: the shot pilot starts a right barrel roll at T s (< 0 = never)
    bool        steerBox;      // --steer-box: show the steering box in shots (always toggleable with F4)
    int         ship;          // --ship N: initial ship, 1 dart, 2 probe, 3 jet (0 = 1)
} GameOptions;

// Runs until the player quits. Returns a process exit code.
int game_run(CtTunnel *ct, const GameOptions *options);

#endif
