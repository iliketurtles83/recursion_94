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
} GameOptions;

// Runs until the player quits. Returns a process exit code.
int game_run(CtTunnel *ct, const GameOptions *options);

#endif
