# Integrating the cybertunnel module

This document is everything needed to drop the tunnel into the main game. The
sandbox harness (`src/main.c`) uses only the public API described here.

## 1. What you link

| Piece | Path |
|---|---|
| Public API | `src/cybertunnel.h` |
| Static library target | `cybertunnel_lib` (CMake) |
| Shaders (runtime files, hot-reloaded) | `src/render/shaders/` |
| Presets | `presets/*.ini` |

Dependencies: raylib 5.5 (pinned via FetchContent in `CMakeLists.txt`), raygui 4.0
(header only, used by the debug panel), libm. C11.

From the game's CMake:

```cmake
add_subdirectory(sandbox/cybertunnel)          # brings cybertunnel_lib (and raylib, if not already present)
target_link_libraries(game PRIVATE cybertunnel_lib)
```

If the game already builds raylib itself, remove the `FetchContent_MakeAvailable(raylib ...)`
line from the sandbox CMake and keep only `raygui`; `cybertunnel_lib` links the `raylib` target.

Shader and preset directories are compiled in as `CT_SHADER_DIR` / `CT_PRESET_DIR`
(absolute source paths). For a shipped build, copy `src/render/shaders/` and `presets/`
next to the executable and set `CtConfig.shaderDir` / `CtConfig.presetDir`.

## 2. Init order

```c
InitWindow(w, h, "game");                    // 1. GL context must exist first
CtConfig cfg = ct_default_config();          // 2. defaults: seed 1, 1280x720, own camera, own post
cfg.seed   = 1234;
cfg.width  = GetRenderWidth();               //    initial render size (resized automatically later)
cfg.height = GetRenderHeight();
CtTunnel *ct = ct_create(&cfg);              // 3. loads shaders, builds GPU targets, prewarms chunks
ct_preset_load(ct, "default");               // 4. optional
...
ct_destroy(ct);                              // 5. before CloseWindow (frees GL objects)
CloseWindow();
```

`ct_create` does all heavy work up front (shader compile, ~10 chunk builds). After that
the steady state performs no heap allocation per frame.

## 3. GL resource ownership

The module owns and frees everything it creates; the game must not delete any of it:

- Shaders (`ShaderLib`, hot reload polls file mtimes every 0.5 s).
- HDR scene target: RGBA16F colour + depth renderbuffer (`RGBA8` fallback with `cfg.hdr16 = false`
  or automatically if RGBA16F is unsupported).
- Bloom chain: 2 × up to 8 half-res (or quarter-res) RGBA16F targets.
- Chunk meshes (VAO/VBO/EBO), streak mesh, ship and trail meshes, one `Material`.

`ct_scene_texture()` is valid until the next resize or `ct_destroy`.

## 4. Per-frame calls

```c
CtInput in = { steerX, steerY, boost, rollLeft, rollRight };   // -1..1, triggers latch until consumed
ct_set_input(ct, &in);
ct_update(ct, GetFrameTime());    // fixed 120 Hz sim + streaming (<= 1 build, 1 upload per frame) + env update
BeginDrawing();
ct_render(ct, NULL, GetRenderWidth(), GetRenderHeight());      // NULL = currently bound framebuffer
// ... game HUD ...
ct_debug_draw(ct, GetScreenWidth(), GetScreenHeight());        // optional
EndDrawing();
```

`ct_update` must run before `ct_render` every frame: it computes the interpolated
ship/camera state, the view/projection matrices and the vanishing point used by render.

Input conventions: `steerX` right, `steerY` up; `rollLeft/rollRight` trigger a barrel roll.

## 5. Rendering into the game's render target

```c
RenderTexture2D rt = LoadRenderTexture(w, h);
ct_render(ct, &rt, w, h);         // tonemapped sRGB result written into rt
```

`ct_render` resizes its internal targets when `width/height` change. The output is
display-ready (sRGB, tonemapped, vignette, grain, whiteout applied).

## 6. Using the game's own post-processing

Set `cfg.usePost = false`. `ct_render` then stops after the scene pass and leaves the
linear HDR result in `ct_scene_texture()` (RGBA16F, GL orientation: v = 0 is the bottom).
The game is responsible for bloom/tonemapping. Useful inputs for a game-side post chain:

- `ct_vanishing_point(ct)` – uv (y up) of the tunnel's vanishing point for radial blur.
- `CtStats.speed` / `CtStats.intensity` – to drive blur strength.

## 7. Camera ownership

- `cfg.ownCamera = true` (default): the module runs the chase camera.
- `cfg.ownCamera = false`: call `ct_set_camera_world(ct, pos, target, up, fovy)` every frame
  **before** `ct_update`. Positions are world-space doubles; the module converts them to render space.

## 8. Coordinates

- Path arc length `s` and all world positions are `double`.
- Rendering uses a floating origin: `ct_render_origin()` snaps to the ship whenever it
  is more than 1024 units away. Render-space = world − origin. Anything the game draws
  inside the module's 3D pass must use the same offset.
- Max float coordinate passed to GL stays well under 2048 (reported in `CtStats.maxRenderCoord`).

## 9. Gameplay hooks and queries

| Call | Purpose |
|---|---|
| `ct_set_intensity(ct, v)` | 0..1 global energy (ring brightness, streak speed/density, zoom blur strength); `< 0` = automatic pacing. Zoom blur itself only appears with boost (`zoomBlur` at full boost, `zoomBlurAtBase` fraction at base speed) |
| `ct_trigger_whiteout(ct)` | whiteout + core flare animation (`whiteoutDuration`) |
| `ct_set_whiteout(ct, w)` | manual whiteout 0..1 (e.g. scripted transitions); `< 0` returns to the animation |
| `ct_path_sample(ct, s)` | centerline position, frame (T, N=up, B=right), radius, roll, curvature |
| `ct_tunnel_radius_at(ct, s)` | radius only |
| `ct_debris_in_range(ct, s0, s1, out, max)` | debris bounding spheres (world) for collision |
| `ct_ship_s`, `ct_ship_world` | ship arc length / world position |
| `ct_jump_to(ct, s)` | teleport (regenerates path from 0 if jumping backwards past the cached window) |
| `ct_reseed(ct, seed)` | new world |
| `ct_param_set/get(ct, name, v)` | any parameter from the table below |
| `ct_preset_load/save(ct, name)` | `presets/<name>.ini` or an explicit path |

Determinism: for a given seed and parameter set, chunk geometry is bit-identical
regardless of build order or frame timing (tested in `cybertunnel_tests`).

## 10. Disabling debug features

`ct_debug_handle_input` reads raylib keyboard state (F1 panel, F2 free camera, F3 overlay,
F12 screenshot request, R reseed, P pause, W whiteout, T switch environment, 1–9 layer toggles,
`[`/`]` base speed ±5, `-`/`=` boost speed ±5).
A game simply never calls it, nor `ct_debug_draw`.

## 11. Adding an environment

1. Copy `src/env/env_template.c` to `src/env/env_<name>.c`. Rename the functions and the factory
   (`env_<name>_create`).
2. In `src/env/env.h` add `ENV_<NAME>` before `ENV_COUNT` and declare the factory.
   Add the matching `CT_ENV_<NAME>` to `cybertunnel.h`, a name in `ct_environment_name()`,
   and `ct->envs[ENV_<NAME>] = env_<name>_create();` in `ct_create` (`src/cybertunnel.c`).
3. Add the `.c` file to `CT_MODULE_SRC` in `CMakeLists.txt`.
4. Implement the interface (`Environment` in `env.h`):
   - `pathStyle` – radius, noise, curvature limits, twist, `hasWalls` (set `false` for open
     environments; nothing in the core assumes a cylinder). Open environments with a ground
     set `floorDepth` (and optionally `ceilingHeight`) together with `maxPitchDeg = 0` and
     `twistRateMax = 0`: the path then stays in the plane y = 0 with N = world up, the floor
     is the plane y = −floorDepth, and the flight model keeps the ship `FLIGHT_CLEARANCE`
     above it. `baseRadius` remains the lateral steering limit (lane half-width).
     `yawSpring` / `yawAxisDeg` (0 = off) make the heading weave around world axes, for
     content aligned to a world grid.
   - `registerParams` – register tunables with **globally unique names**; group names become
     debug-panel sections and ini sections. Path-affecting groups trigger a full rebuild when
     edited (`group_rebuilds_path` in `debug_ui.c`), content groups a chunk rebuild (`group_rebuilds`).
   - `init` – `sl_load` shaders, create static meshes. Called with `shaders == NULL` in tests:
     skip all GL work then.
   - `buildChunk` – **pure CPU and deterministic**. Use only `(seed, params, chunkIndex, path)`.
     Seed RNGs with `ct_hash64_3(seed, ENV_ID, chunkIndex)` then per-feature sub-streams
     (`ct_hash64_2(chunkSeed, FEATURE)`), never `rand()`/`GetRandomValue()`. Write geometry into
     `out->geo[batch]` (see the vertex packing in `render/linegeo.h`) relative to `out->origin`,
     per-chunk uniform data into `out->uniforms` (96 vec4 slots, layout is yours), and collision
     data into `out->debris`.
   - `uploadChunk`/`releaseChunk` – usually `chunk_upload_default` / `chunk_release_default`.
   - `pathReach` (field) – how far beyond its chunk `buildChunk` samples the path; the core keeps
     the path generated that much further around the chunk window (max 1500).
   - `energyAt` – the pacing energy 0..1 at `s` (drives global intensity when not overridden).
   - `atmosphere` (optional) – adjust the frame's `Atmosphere` (fog range, fades, `fogToCore`,
     core tint). It receives a per-frame copy filled from the core "Fog" params.
   - `drawBackground`, `drawOpaque`, `drawEmissive` – called inside the HDR scene pass.
     On entry: scene target bound, camera matrices loaded, depth test on, backface culling off.
     Opaque: alpha blend, depth write on. Emissive: additive blend, depth write off.
     Use `env_chunk_transform(chunk, ctx->renderOrigin)` as the model matrix and
     `env_set_common_uniforms()` for the shared fog/fade/time uniforms (`shaders/common.glsl`).
     Count draw calls in `ctx->stats`.
   - Shared building blocks: `render/streaks.h` (GPU speed streaks along the path, in a
     cylinder or, with `volume = 1`, a slab above a floor with optional fall slant; keep a
     `StreakParams` in your params and register its fields under your own names) and
     `render/shapes.h` (wireframe cube / octahedron with data faces, wire sphere) and
     `render/gridfloor.h` (infinite filtered neon grid floor; call from `drawOpaque`, it writes
     depth from `ctx->zNear/zFar`).
5. Test: `./cybertunnel --env <id>` or press `T` at runtime; add a determinism case to
   `tests/test_main.c` with `build_order_independent()` (see `test_chunks`, `test_city`).

Transitions between environments (future): the whiteout uniform and core flare already
exist (`ct_set_whiteout`); `PathStyle` is sampled at path-generation time, so blending two
styles over a transition zone and keeping two environments' chunk sets alive during the
window are the remaining pieces.

## 12. Parameters

Generated from the registry (`./cybertunnel --list-params`). All of them can be set with
`ct_param_set`, saved/loaded via presets (`key=value`, `[section]` lines are ignored on load,
unknown keys are skipped, and loading first resets everything to defaults).

| Group | Name | Type | Default | Min | Max |
|---|---|---|---|---|---|
| Sim | `timeScale` | float | 1 | 0 | 4 |
| Sim | `intensityOverride` | float | -1 | -1 | 1 |
| Sim | `intensityResponse` | float | 0.6 | 0.05 | 10 |
| Sim | `whiteoutDuration` | float | 1.8 | 0.2 | 10 |
| Sim | `buildBudget` | int | 1 | 1 | 8 |
| Sim | `uploadBudget` | int | 1 | 1 | 8 |
| Sim | `showShip` | bool | on | | |
| Sim | `showPathDebug` | bool | off | | |
| Sim | `rebaseDist` | float | 1024 | 1 | 100000 |
| Sim | `engineIntensity` | float | 10 | 0 | 40 |
| Sim | `trailIntensity` | float | 2.2 | 0 | 20 |
| Sim | `trailWidth` | float | 0.035 | 0.005 | 0.5 |
| Flight | `baseSpeed` | float | 25 | 0 | 300 |
| Flight | `boostSpeed` | float | 40 | 0 | 400 |
| Flight | `speedResponse` | float | 2.5 | 0.1 | 20 |
| Flight | `steerLimit` | float | 0.55 | 0 | 0.9 |
| Flight | `steerAccel` | float | 60 | 1 | 300 |
| Flight | `steerDamping` | float | 5 | 0.1 | 30 |
| Flight | `maxBankDeg` | float | 35 | 0 | 80 |
| Flight | `maxShipPitchDeg` | float | 15 | 0 | 45 |
| Flight | `autopilot` | bool | off | | |
| Camera | `camDistance` | float | 6 | 1 | 30 |
| Camera | `camHeight` | float | 1.6 | -5 | 10 |
| Camera | `camStiffness` | float | 7 | 0.5 | 40 |
| Camera | `camUpStiffness` | float | 5 | 0.5 | 40 |
| Camera | `camFollowLateral` | float | 0.75 | 0 | 1 |
| Camera | `lookAhead` | float | 25 | 1 | 100 |
| Camera | `lookBlend` | float | 0.55 | 0 | 1 |
| Camera | `fovBase` | float | 70 | 30 | 120 |
| Camera | `fovBoost` | float | 15 | 0 | 40 |
| Camera | `shakeAmount` | float | 0.06 | 0 | 0.5 |
| Camera | `cameraRollFollow` | float | 0.35 | 0 | 1 |
| Camera | `barrelRollTime` | float | 0.7 | 0.2 | 3 |
| Fog | `fogStart` | float | 60 | 0 | 600 |
| Fog | `fogEnd` | float | 300 | 1 | 800 |
| Fog | `fogToCore` | float | 0.6 | 0 | 1 |
| Fog | `farFadeStart` | float | 220 | 0 | 800 |
| Fog | `farFadeEnd` | float | 390 | 1 | 900 |
| Fog | `nearFadeStart` | float | 0.5 | 0 | 10 |
| Fog | `nearFadeEnd` | float | 2.5 | 0 | 20 |
| Fog | `minPixelWidth` | float | 0.7 | 0 | 4 |
| Fog | `coreR` | float | 0.91 | 0 | 1 |
| Fog | `coreG` | float | 0.98 | 0 | 1 |
| Fog | `coreB` | float | 1 | 0 | 1 |
| Bloom/Post | `bloomOn` | bool | on | | |
| Bloom/Post | `bloomThreshold` | float | 1 | 0 | 8 |
| Bloom/Post | `bloomKnee` | float | 0.6 | 0 | 2 |
| Bloom/Post | `bloomIntensity` | float | 0.8 | 0 | 4 |
| Bloom/Post | `bloomRadius` | float | 0.85 | 0 | 1.5 |
| Bloom/Post | `bloomMips` | int | 6 | 1 | 8 |
| Bloom/Post | `bloomQuarterRes` | bool | off | | |
| Bloom/Post | `zoomBlurOn` | bool | on | | |
| Bloom/Post | `zoomBlur` | float | 0.096 | 0 | 0.5 |
| Bloom/Post | `zoomBlurAtBase` | float | 0 | 0 | 1 |
| Bloom/Post | `zoomBlurInner` | float | 0.08 | 0 | 1 |
| Bloom/Post | `zoomBlurOuter` | float | 0.75 | 0 | 2 |
| Bloom/Post | `zoomTaps` | int | 10 | 2 | 16 |
| Bloom/Post | `caOn` | bool | on | | |
| Bloom/Post | `caPixels` | float | 2 | 0 | 8 |
| Bloom/Post | `tonemapOn` | bool | on | | |
| Bloom/Post | `exposure` | float | 1 | 0.05 | 8 |
| Bloom/Post | `saturation` | float | 1.25 | 0 | 2 |
| Bloom/Post | `vignetteOn` | bool | on | | |
| Bloom/Post | `vignette` | float | 0.55 | 0 | 1.5 |
| Bloom/Post | `grainOn` | bool | on | | |
| Bloom/Post | `grain` | float | 0.025 | 0 | 0.2 |
| Path | `baseRadius` | float | 12 | 3 | 40 |
| Path | `radiusNoiseAmp` | float | 0.12 | 0 | 0.5 |
| Path | `radiusNoiseScale` | float | 60 | 5 | 400 |
| Path | `maxYawStepDeg` | float | 14 | 0 | 40 |
| Path | `maxPitchDeg` | float | 20 | 0 | 60 |
| Path | `pitchSpring` | float | 0.15 | 0 | 1 |
| Path | `twistRateMax` | float | 0.015 | 0 | 0.1 |
| Rings | `showRings` | bool | on | | |
| Rings | `ringSpacingMin` | float | 5 | 1 | 30 |
| Rings | `ringSpacingMax` | float | 10 | 1 | 40 |
| Rings | `gateChance` | float | 0.12 | 0 | 1 |
| Rings | `gateMin` | int | 3 | 2 | 8 |
| Rings | `gateMax` | int | 5 | 2 | 8 |
| Rings | `gateSpacing` | float | 1.5 | 0.5 | 5 |
| Rings | `layersMin` | int | 2 | 1 | 6 |
| Rings | `layersMax` | int | 3 | 1 | 6 |
| Rings | `layerRMin` | float | 0.95 | 0.5 | 2 |
| Rings | `layerRMax` | float | 1.4 | 0.5 | 2.5 |
| Rings | `segMin` | int | 3 | 1 | 24 |
| Rings | `segMax` | int | 12 | 1 | 24 |
| Rings | `gapMin` | float | 0.12 | 0 | 0.9 |
| Rings | `gapMax` | float | 0.45 | 0 | 0.9 |
| Rings | `bandWMin` | float | 0.1 | 0.02 | 2 |
| Rings | `bandWMax` | float | 0.28 | 0.02 | 2 |
| Rings | `castStepMin` | float | 0.5 | 0.05 | 3 |
| Rings | `castStepMax` | float | 1.4 | 0.05 | 3 |
| Rings | `castCellMinDeg` | float | 5 | 1 | 30 |
| Rings | `castCellMaxDeg` | float | 12 | 1 | 30 |
| Rings | `tickLenMin` | float | 0.3 | 0.05 | 3 |
| Rings | `tickLenMax` | float | 1.2 | 0.05 | 3 |
| Rings | `wBand` | float | 1 | 0 | 5 |
| Rings | `wCastellated` | float | 2.2 | 0 | 5 |
| Rings | `wTicks` | float | 0.5 | 0 | 5 |
| Rings | `wDashed` | float | 0.4 | 0 | 5 |
| Rings | `wDouble` | float | 0.6 | 0 | 5 |
| Rings | `spokesMax` | int | 6 | 0 | 16 |
| Rings | `angVelMax` | float | 0.4 | 0 | 2 |
| Rings | `ringIntensity` | float | 2.6 | 0 | 10 |
| Rings | `pulseChance` | float | 0.25 | 0 | 1 |
| Rings | `pulseAmount` | float | 2 | 0 | 8 |
| Rings | `pulseSpeed` | float | 0.6 | 0 | 4 |
| Rings | `railIntensity` | float | 2.6 | 0 | 8 |
| Rings | `railDensityMul` | float | 1 | 0 | 3 |
| Ribbons | `showRibbons` | bool | on | | |
| Ribbons | `ribbonCount` | int | 3 | 0 | 6 |
| Ribbons | `ribbonRMin` | float | 1.05 | 0.5 | 2 |
| Ribbons | `ribbonRMax` | float | 1.3 | 0.5 | 2 |
| Ribbons | `ribbonWMin` | float | 2 | 0.2 | 10 |
| Ribbons | `ribbonWMax` | float | 5 | 0.2 | 10 |
| Ribbons | `ribbonOmegaMin` | float | 0.03 | 0 | 0.3 |
| Ribbons | `ribbonOmegaMax` | float | 0.08 | 0 | 0.3 |
| Ribbons | `ribbonIntensity` | float | 0.4 | 0 | 3 |
| Ribbons | `ribbonScroll` | float | 0.15 | 0 | 2 |
| Streaks | `showStreaks` | bool | on | | |
| Streaks | `streakDensity` | float | 1 | 0 | 1 |
| Streaks | `streakLenMul` | float | 1 | 0.1 | 5 |
| Streaks | `streakWMin` | float | 0.03 | 0.005 | 0.3 |
| Streaks | `streakWMax` | float | 0.08 | 0.005 | 0.3 |
| Streaks | `streakIntensity` | float | 1.6 | 0 | 10 |
| Streaks | `cometFrac` | float | 0.01 | 0 | 0.2 |
| Streaks | `purpleFrac` | float | 0.15 | 0 | 1 |
| Streaks | `helixFrac` | float | 0.2 | 0 | 1 |
| Streaks | `helixRate` | float | 0.05 | -0.3 | 0.3 |
| Streaks | `streakZRange` | float | 160 | 40 | 400 |
| Streaks | `streakZBehind` | float | 12 | -50 | 50 |
| Streaks | `streakSpeedMul` | float | 1 | 0 | 4 |
| Debris | `showDebris` | bool | on | | |
| Debris | `debrisMax` | int | 6 | 0 | 8 |
| Debris | `debrisSizeMin` | float | 0.6 | 0.1 | 5 |
| Debris | `debrisSizeMax` | float | 2.5 | 0.1 | 5 |
| Debris | `octaChance` | float | 0.2 | 0 | 1 |
| Debris | `debrisRMin` | float | 0.45 | 0 | 1.2 |
| Debris | `debrisRMax` | float | 0.95 | 0 | 1.2 |
| Debris | `keepout` | float | 0.35 | 0 | 0.9 |
| Debris | `tumbleMin` | float | 0.2 | 0 | 3 |
| Debris | `tumbleMax` | float | 1.2 | 0 | 3 |
| Debris | `edgeWidth` | float | 0.06 | 0.01 | 0.3 |
| Debris | `edgeIntensity` | float | 3 | 0 | 10 |
| Debris | `faceIntensity` | float | 0.35 | 0 | 3 |
| Debris | `dataRate` | float | 1.5 | 0 | 10 |
| Debris | `showHolo` | bool | on | | |
| Debris | `holoChance` | float | 0.45 | 0 | 1 |
| Debris | `holoIntensity` | float | 1.6 | 0 | 8 |
| Core | `showCore` | bool | on | | |
| Core | `showBackground` | bool | on | | |
| Core | `coreIntensity` | float | 14 | 0 | 40 |
| Core | `coreSize` | float | 0.07 | 0.01 | 0.6 |
| Core | `coreStreak` | float | 1 | 0 | 4 |
| Core | `coreRays` | float | 0.5 | 0 | 4 |
| Core | `bgGlow` | float | 0.3 | 0 | 1 |
| Core | `dust` | float | 0.03 | 0 | 0.3 |
| Pacing | `pacing` | bool | on | | |

The template environment adds `tplRadius`, `tplYawStepDeg`, `tplMarkerIntensity` (group Template) while it is active.
The city environment (Env #2, in progress) adds, while it is active: `cityLaneHalfWidth`, `cityYawStepDeg`, `cityFloorDepth`, `cityYawSpring` (group Path); `showFloor`, `gridCell`, `gridLineWidth`, `gridHaloWidth`, `gridIntensity`, `gridHalo`, `gridNodeBoost`, `gridWhiteCore`, `floorFogStart`, `floorFogEnd`, `floorSheen`, `gridFilter` (group Floor); `showSky`, `cityFogStart`, `cityFogEnd`, `hazeWidth`, `hazeGlow`, `skyDust` (group City); `showTowers`, `towerLatticeCells`, `towerClearance`, `towerBandMax`, `towerDensityNear`, `towerDensityFar`, `towerHeightMin`, `towerHeightMax`, `towerHeightFar`, `landmarkChance`, `landmarkMul`, `towerFootMin`, `towerFootMax`, `setbackChance`, `towerEdgeIntensity`, `towerEdgeWidth`, `towerBrightness`, `towerRim`, `towerUnderglow`, `panelW`, `panelH`, `seamDark`, `recessed` (group Towers); `showTraces`, `showGlyphs`, `tracePitch`, `traceWidth`, `traceIntensity`, `traceBuses`, `traceFar`, `traceStop`, `tracePulse`, `tracePulseSpeed`, `tracePulseAmount`, `viaRadius`, `glyphStripChance`, `glyphBlockChance`, `glyphIntensity`, `glyphScroll`, `decorOffset` (group Decor); `showRain`, `rainDensity`, `rainLenMul`, `rainWMin`, `rainWMax`, `rainIntensity`, `rainCometFrac`, `rainZRange`, `rainHalfWidth`, `rainYMin`, `rainYMax`, `rainFallTilt`, `rainSpeedMul` (group Rain); `showProps`, `propsMax`, `propSizeMin`, `propSizeMax`, `propHeightMax`, `propEdgeIntensity`, `propFaceIntensity` (group Props). The core adds `rebaseDist` (group Sim, default 1024).
