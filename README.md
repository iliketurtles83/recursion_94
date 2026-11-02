# RECURSION_94

A procedural treadmill shooter: a love letter to the 90s floppy-disk era, the game
scene and the demoscene. Worlds, geometry and music are generated at runtime from
a seed, by math rather than stored data.

Written in C11 with Raylib 6 and hand-written GLSL.

## Status

RECURSION_94 is being rebuilt. The original floppy-sized game (in `legacy/`, not
built) is being merged with the procedural flight engine and environments from the
rec_tunnel sandbox, which now live at the repo root. The plan is five levels, each
with its own environment, song and boss. The first milestone is a one-level
vertical slice. Today the executable runs a playable combat slice: pick a seed, then fight
through the server corridor to a procedurally synthesized soundtrack. Bosses
come next.

## Build

Requires a C11 compiler, CMake 3.20+, Git, and Raylib's desktop dependencies (on
Debian/Ubuntu: `libasound2-dev libgl1-mesa-dev libx11-dev libxrandr-dev libxi-dev
libxcursor-dev libxinerama-dev`). CMake fetches pinned Raylib 6.0 and raygui 4.0.

```sh
cmake -S . -B build && cmake --build build -j
ctest --test-dir build --output-on-failure
```

To build offline, pass local checkouts:
`-DFETCHCONTENT_SOURCE_DIR_RAYLIB=/path/to/raylib-6.0 -DFETCHCONTENT_SOURCE_DIR_RAYGUI=/path/to/raygui`.

## Play

```sh
./build/recursion94              # seed selector: Left/Right +-1, Up/Down +-100, Enter to start
./build/recursion94 --seed 94    # start a run directly
```

- WASD / arrows: steer. Q / E: barrel roll.
- J or left mouse: tap for a straight shot; hold to lock up to 4 (later 6)
  targets and release for a homing volley.
- K: spend 50 chain links on a piercing beam. L: spend 100 on a projectile-
  clearing bomb.
- Space: boost. Faster and louder, but regular enemies fire faster, their shots
  home in mildly, and hits cost 15% more Core Energy.
- P pauses, R restarts after game over, Esc returns to the seed selector (Esc
  there quits).

Kills build a chain that upgrades the weapon (x4, x8, x12); any hit resets it.
The run ends when Core Energy reaches zero.

## Environment sandbox

```sh
./build/recursion94 --sandbox --env 3 --preset corridor   # server corridor
./build/recursion94 --sandbox --env 0                     # data vortex
./build/recursion94 --sandbox --env 2 --preset city       # grid city
./build/recursion94 --sandbox --env 5 --preset crystal    # crystal field
```

Arrow keys steer, Shift/Space boosts, Q/E barrel-roll. F1 opens the parameter
panel, F2 a free camera, F3 the overlay, T switches environment, R reseeds and
F12 saves a screenshot. `--help` lists all options.

## Docs

- `AGENTS.md`: build, test, architecture and engineering rules.
- `docs/engine/`: engine API (`INTEGRATION.md`), environment log (`NOTES.md`), backlog.
- `recursion_94.md`: design reference for the original game's gameplay, audio and bosses.
- `RECURSION_94_decisions_log.md`: decisions and results log.
- `archive/`: finished plans, past release notes and superseded material.
