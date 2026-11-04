// Neon vector presentation for the ported combat: enemies, shots, effects and HUD.
// Combat runs in arena units (x lateral, y up, z = -distance ahead of the ship);
// this module maps them onto the engine's path so the arena rides the corridor.
#ifndef R94_COMBAT_DRAW_H
#define R94_COMBAT_DRAW_H

#include "cybertunnel.h"
#include "gameplay.h"
#include "raylib.h"

// Arena units -> world: s = shipS - z*scale; offset = B*x*scale + N*(y - yZero)*scale.
typedef struct ArenaMap {
    double shipS;
    float  scale;   // world units per arena unit
    float  yZero;   // arena y on the path centre line
} ArenaMap;

typedef struct CombatDraw {
    Shader   shader;
    Shader   dimShader;   // black with alpha: darkens what is already drawn
    bool     ready;
    float    focus;       // 0..1, eased toward 1 while anything hostile is on the field
    // Per-frame state captured in the scene callback (for HUD projection).
    Camera3D camera;
    double   origin[3];
    bool     haveCamera;
} CombatDraw;

void combat_draw_init(CombatDraw *d);
void combat_draw_unload(CombatDraw *d);

// Per-frame presentation state (the combat focus dim). Call once per game update.
void combat_draw_update(CombatDraw *d, const GameplaySystem *g, float dt);

// Called from the engine's scene-draw hook: emissive = false for the dark bodies,
// true for the additive glow pass.
void combat_draw_scene(CombatDraw *d, CtTunnel *ct, const ArenaMap *map, const GameplaySystem *g,
                       const CtSceneView *view, bool emissive);

// 2D overlay: energy, chain links, score, combo/weapon, hit flash, game over.
void combat_draw_hud(const CombatDraw *d, const GameplaySystem *g, int width, int height);

// World position (double) of an arena point.
void arena_to_world(const CtTunnel *ct, const ArenaMap *map, Vector3 arena, double out[3]);

#endif
