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
                      int screenWidth, int screenHeight);
void DrawDemosceneBackdrop(const DemosceneSystem *demo, float time, float intensity,
                           int screenWidth, int screenHeight);
void DrawDemosceneOverlay(const DemosceneSystem *demo, float time, float intensity,
                          float beatPulse, int screenWidth, int screenHeight);
void DrawCoreTransition(const DemosceneSystem *oldDemo, const DemosceneSystem *nextDemo,
                        float time, float progress, Vector2 focus,
                        int screenWidth, int screenHeight);

#endif // DEMOSCENE_H
