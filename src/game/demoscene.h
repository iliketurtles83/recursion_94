#ifndef DEMOSCENE_H
#define DEMOSCENE_H

#include "raylib.h"
#include <stdint.h>

typedef struct {
    uint32_t seed;
    const char *name;
    Color primary;
    Color secondary;
    Color hot;
    Color threat;
    Color shadowBody;
    Color skyZenith;
    Color skyHorizon;
} DemosceneSystem;

void InitDemoscene(DemosceneSystem *demo, uint32_t seed);
void DrawSeedSelector(uint32_t seed, float time, float bpm, const char *keyName,
                      int ship, int shipCount, const char *shipName,
                      int screenWidth, int screenHeight);

#endif // DEMOSCENE_H
