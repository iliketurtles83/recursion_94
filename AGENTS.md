# AGENTS.md

RECURSION_94 is a procedural treadmill shooter in C11 + Raylib 6: a love letter to
the 90s floppy-disk era, the game scene and the demoscene. It is being rebuilt on
the cybertunnel engine imported from the rec_tunnel sandbox. The planned structure
is five levels, each with its own environment, song and boss.

## Current state (transition)

- The repo root is the **engine** (formerly rec_tunnel's `sandbox/cybertunnel`).
  `src/game/` is the RECURSION_94 game layer: seed selector and run loop
  (`game.c`), combat logic (`gameplay.c`, ported), its neon presentation and HUD
  (`combat_draw.c`), the procedural synth (`audio_synth.c`, ported) and the
  selector art and seed palettes (`demoscene.c`, `palette.h`). `./build/recursion94` starts
  the game in the server corridor (preset `r94_corridor`); `--sandbox` gives the
  environment harness.
- `legacy/` holds the rest of the original game (environment, old main loop):
  unbuilt reference only. Combat and bosses are ported; boss presentation is
  next. The original boss chassis drawing code is in git history
  (`git show 1a7c6ca:legacy/src/gameplay.c`, `Draw*Boss`).
- **Arena mapping.** Combat runs in arena units: x lateral, y up, z = -distance
  ahead of the ship. 1 arena unit = `ARENA_SCALE` (0.4) world units on all axes,
  riding the path (`ArenaMap`, `arena_to_world` in `combat_draw.c`). The arena
  bounds come from the engine's steering box each frame (`SetGameplayArena`);
  formation placement is bounds-relative, and with the original box it gives
  the original numbers (so `game_combat` checks stay valid). The ship position
  is read from the engine (`ct_ship_world` vs the path frame); the engine owns
  flight. Leviathan gravity is reported in `player.pull` but not applied yet.
  Bosses are disabled (`bossesEnabled = false`) until their presentation lands. Its environment and its ship, enemy and shot graphics are
  being dropped. Delete legacy files once their content is ported.
  `archive/AGENTS_legacy.md` has the rules that applied to that code.
- First milestone: a vertical slice (server corridor, engine flight, ported
  weapons/enemies, one boss, one song).

## Spirit

- Procedural generation and demoscene-style math lead. Generated assets (ComfyUI)
  are acceptable where they earn their place, such as the optional jet texture.
  There is no hard size limit any more (the 1.44 MB contest is over), but prefer
  clever code over stored data. Never pad with data.
- Abstract, light-on-dark, rhythmic visuals. Don't drift back toward realistic
  industrial clutter.

## Build and test

```sh
cmake -S . -B build && cmake --build build -j     # RelWithDebInfo by default
ctest --test-dir build --output-on-failure         # engine_core/_ship/_vectoring, game_audio, game_combat
./build/cybertunnel_tests                          # core determinism/continuity, no window or GL
```

- Raylib 6.0 and raygui 4.0 are pinned through FetchContent. Offline, point CMake
  at local checkouts with `-DFETCHCONTENT_SOURCE_DIR_RAYLIB=<raylib-6.0 dir>` and
  `-DFETCHCONTENT_SOURCE_DIR_RAYGUI=<raygui dir>`.
- Flags: `-Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers`.
  Don't introduce warnings.
- The executable loads shaders, presets and assets from copies next to it
  (`build/shaders`, `build/presets`, `build/assets`), copied after each build.
  After editing a shader or preset, rebuild (or copy it) before taking a shot.
  Hot reload watches the `build/` copies. Windows builds embed them as RC
  resources (`src/core/resources.c`).

## Running and visual regression

```sh
./build/recursion94                         # game: seed selector, then the run
./build/recursion94 --seed 94               # skip the selector
./build/recursion94 --seed 94 --game-shot 5 --out FILE --hidden   # deterministic game frame
./build/recursion94 --sandbox --env N [--seed N] [--preset NAME] [--set name=value ...]
    [--shot DISTANCE --out FILE] [--bench SECONDS] [--list-params] [--write-preset NAME]
    [--boost] [--noship] [--hdr8] [--hidden]
```

- Environments: 0 vortex, 1 template, 2 grid city, 3 server corridor, 4 greybox
  (use `--preset greybox`), 5 crystal field. Presets: `city`, `corridor`, `crystal`,
  etc. `mission` and `mission_tunnel` carry the flight tuning (thrust vectoring,
  courier scale) inherited from rec_tunnel's game layer.
- `--game-shot S` runs the game for S seconds on a fixed 1/60 s step with no
  audio thread (beat pulse held at 0) and a scripted pilot that weaves, taps
  and charges, saves a PNG and exits: byte-reproducible.
  Flush the render batch (`rlDrawRenderBatchActive`) before reading the
  framebuffer, or 2D HUD draws are missing from the capture.
- Sandbox `--shot` jumps to a path distance, steps a fixed 1/60 s and saves after 2 s of
  simulated time, so the same command gives a byte-identical PNG. Use it for
  before/after checks of any visual change. Debug renders go in `shots/` (ignored).
- On this machine, run shots with `--hidden` and `DISPLAY=:1`,
  `XAUTHORITY=/run/user/1000/xauth_*`. A visible window on `:1` never presents
  and hangs. Without a display, GLFW fails.
- Runtime keys: F1 param panel, F2 free cam, F3 overlay, T switch environment,
  1–9 layer toggles, R reseed, F12 screenshot.

## Architecture

- **Core / environment split** (`src/env/env.h`). The core owns the path, chunk
  streaming, ship/flight, camera and HDR post. An `Environment` is a table of
  callbacks: `pathStyle`, `registerParams`, `buildChunk`, `uploadChunk`,
  `drawBackground/Opaque/Emissive`, `energyAt`, `atmosphere`. To add one, follow
  `docs/engine/INTEGRATION.md` §11 (copy `env_template.c`, register it in `env.h`,
  `cybertunnel.h`, `cybertunnel.c` and `CMakeLists.txt`).
- **Determinism is a hard rule.** `buildChunk` is pure CPU with no GL calls; its
  output depends only on (seed, params, chunkIndex, path). Seed RNGs with
  `ct_hash64_3(seed, ENV_ID, chunkIndex)` and per-feature sub-streams. Never use
  `rand()`, `GetRandomValue()` or `SetRandomSeed()`. Tests check build-order
  independence. `init` is called with `shaders == NULL` in tests, so skip GL work
  then.
- **Path** (`core/path.c`): centripetal Catmull-Rom through noise-driven control
  points, arc-length resampled into a ring buffer, with rotation-minimizing frames.
  Open environments (city, corridor) set `floorDepth`, `maxPitchDeg=0` and
  `twistRateMax=0` for a flat path with world-up normals.
- **Precision**: world positions are double (`DVec3`). Rendering uses render space
  = world − `renderOrigin` (floating origin); chunk geometry is stored relative to
  `chunk->origin`.
- **Rendering**: HDR scene target, then bloom, then the composite pass
  (`render/pipeline.c`). Neon lines are camera-facing ribbons (`render/linegeo.c`).
  Shared fog/fade/time uniforms come from `env_set_common_uniforms()` and
  `shaders/common.glsl`. Shared building blocks: `render/streaks`, `render/shapes`,
  `render/gridfloor`, `env/lattice_place`, `env/trace_route`, `core/pacing`.
- **Shared shaders are shared state.** `ring.fs`, `tower.fs`, `sky.fs`, `grid.fs`,
  `streak.vs` and `common.glsl` serve several environments, and `sl_load` caches
  programs by shader pair, so uniform values persist across environment switches.
  Make zero mean the old behaviour, and have every user set the uniform explicitly
  every frame (as `env_corridor.c` does for `uWindows`, `uSkyline`, `uDetailRange`).
- **Params**: every tunable has a globally unique name; group names become debug
  panel and ini sections. Editing a path-affecting group triggers a full rebuild.
- **Audio**: `game_audio` checks the synth's deterministic PCM hash
  (`AUDIO_VALIDATION_HASH` in `src/game/audio_synth.c`); re-bless it only for
  intentional synth changes. The synth runs in Raylib's audio callback thread;
  the game talks to it through lock-free command queues (`UpdateAudioSynth`,
  `TriggerSynthSFX`) and reads telemetry such as the beat pulse.
- **Beat**: `ct_set_beat(pulse)` scales bloom by the `beatBloom` parameter
  (default 0, so sandbox looks are unchanged; `r94_corridor` sets 0.35).
- **Game hook**: game code uses only `src/cybertunnel.h`. It feeds input via
  `ct_set_input`, may own the camera (`ownCamera=false`), and draws its objects into
  the HDR scene with `ct_set_scene_draw`.

## Docs

- `docs/engine/INTEGRATION.md`: public API, GL ownership, parameter table.
- `docs/engine/NOTES.md`: environment implementation log. Record new environment
  work there in the same style.
- `docs/engine/BACKLOG.md`: open engine items (steering box, engine trail, ...).
- `recursion_94.md`: design reference for the original game (gameplay, audio and
  boss sections are the spec being ported).
- `RECURSION_94_decisions_log.md`: decisions and results log.
- `archive/`: finished plans and superseded docs. Not current guidance.
