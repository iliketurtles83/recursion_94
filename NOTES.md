# Cybertunnel sandbox — notes

Machine used for all numbers below: AMD Ryzen AI MAX+ 395, Radeon 8060S (integrated,
Strix Halo), Linux (Wayland), Mesa. raylib 5.5 / raygui 4.0 pinned via FetchContent.

Main project: this repository contains no main game yet, so the brief's default stack
(C11, CMake, raylib 5.5) was used as-is. No game code was looked at.

Commit history: the phases were implemented in one working session and committed in
groups (scaffold+core, render pipeline, environment, tooling+docs) rather than strictly one
commit per phase. Each phase's acceptance checks are recorded below.

Debug renders live in `shots/` (git-ignored). Reproduce any of them with
`./build/cybertunnel --shot S [--preset NAME] [--out FILE]`. Shot mode runs on a fixed
1/60 s step and a frame count (since Env #2 Phase A), so the same command produces a
byte-identical PNG; that is what the refactor regression checks below rely on.

## Build / run

```
cmake -S . -B build && cmake --build build -j
./build/cybertunnel_tests
./build/cybertunnel [--seed N] [--w W --h H] [--vsync 0|1] [--fps N] [--env 0|1|2]
                    [--bench SECONDS [--speed U]] [--shot S [--out FILE]] [--preset NAME]
                    [--write-preset NAME] [--list-params] [--hdr8] [--noship] [--ui] [--whiteout]
                    [--set NAME=VALUE ...]   (applied after --preset, then rebuilds)
```

Keys: arrows steer, Shift/Space boost, Q/E barrel roll, F1 panel, F2 free camera (WASD +
mouse), F3 overlay, F12 screenshot (`shots/vortex_seed{seed}_s{s}.png`), R reseed, P pause,
W whiteout, T switch environment, 1–9 layer toggles (rings, ribbons, streaks, debris, holo,
core, background, ship, path debug).

---

## Phase 0 — Scaffold

- `CMakeLists.txt`: `cybertunnel_lib` (static), `cybertunnel` (harness), `cybertunnel_tests`
  (no window). `-Wall -Wextra` (minus unused-parameter / missing-field-initializers, which the
  interface-callback style triggers everywhere).
- Window 1280×720 resizable, vsync toggle, no MSAA; fixed 120 Hz loop; overlay; CLI.
- **Accept:** builds with no warnings in project code (raygui itself emits one
  `-Wunused-but-set-variable` in its own header); tests exit 0.

## Phase 1 — Procedural path (`core/path.c`)

- Control points every 40 units from yaw/pitch noise exactly as specified
  (`yaw += clamp(noise·maxYawStep)`, pitch spring). Point 0 sits one spacing behind the
  origin so the spline starts at s = 0.
- Centripetal Catmull-Rom (Barry–Goldman), 96 arc-length subsamples per segment,
  uniform resampling at Δs = 0.5 into a ring buffer of 8192 samples (4096 units).
- Rotation-minimizing frames by double reflection; roll integrated from a noise twist rate
  and applied on sampling; radius from noise.
- `path_ensure()` extends incrementally; jumping behind the ring regenerates from 0.
- **Accept (tests):** `path_sample(5000)` identical (|Δ| < 1e-9) for one-shot vs incremental
  generation and after regeneration; over 20,000 units min dot(N) 0.99997, min dot(T) 0.99999;
  max curvature 0.0078/unit vs limit 0.026 (`path_curvature_limit`: 2.5 × max heading change per
  control spacing); uniform step error 5e-5; tangent vs chord ≥ 0.9999 (C1).
- Debug draw (key 9): centerline, RGB frame axes every 5 units, radius circles every 10.

## Phase 2 — Chunks and floating origin (`core/chunks.c`)

- C = 48, window `[floor((s−24)/C), floor((s+400)/C)]`, pool of 24 slots, states
  EMPTY → BUILT → UPLOADED → released. Budget 1 build + 1 upload per frame, nearest first;
  `ct_create`/jumps prewarm the whole window.
- Chunk fade-in (0.3 s) via `uChunkFade`, plus far fade into the core glow.
- CPU buffers (`LineGeo`) live in the slot and are reused; capacity only grows.
- Floating origin snaps to the ship at > 1024 units. Chunk model matrix =
  translate(chunk.origin − renderOrigin).
- **Accept:** see bench below. Determinism test: hashes of 24 chunks identical when built in
  order vs shuffled order on an independently generated path.

## Phase 3 — HDR pipeline (`render/pipeline.c`)

- Scene: RGBA16F colour + depth renderbuffer built via rlgl (`rlLoadFramebuffer`,
  `rlLoadTexture(... R16G16B16A16 ...)`, `rlLoadTextureDepth(..., true)`,
  `rlFramebufferAttach`), RGBA8 fallback (`--hdr8`, or automatic).
- Bloom: Karis-averaged soft-knee prefilter at half (or quarter) res → dual-Kawase down ×N →
  dual-Kawase up, each level `up(coarser) * radius + down(level)`.
- Composite (one pass): scene + bloom → radial zoom blur toward the vanishing point
  (masked by distance, strength ∝ speed/baseSpeed × intensity) → radial CA → exposure →
  ACES (Hill fit) → saturation → vignette → sRGB → grain → whiteout.
- All fullscreen passes draw an NDC quad (`fullscreen.vs` ignores matrices), so there is no
  render-texture Y-flip bookkeeping anywhere.
- Shader hot reload: mtime poll every 0.5 s including `common.glsl`; `#include "file"` is
  expanded by `shaderlib.c`; a failed compile keeps the old program.
- **Accept:** every stage toggles from the panel (Bloom/Post group); CPU time per pass in the
  overlay. Bloom is soft and wide without blockiness at 720p/1080p.

## Phase 4 — Neon line geometry (`render/linegeo.c`)

- SoA builder matching raylib's `Mesh`, auto-splits into ≤ 65,535-vertex batches.
- Planar strips (arcs, radial strips, polylines) and camera-facing segments (expanded in
  the vertex shader). Attribute packing documented at the top of `linegeo.h`.
- Fragment profile: crisp core (`fwidth` AA) + Gaussian halo; tint → white towards the core.
- Minimum pixel width: lines narrower than `minPixelWidth` px are widened in the vertex shader
  and their intensity scaled down by the same factor (energy conserving), which removes far
  shimmer.
- **Deviation / bug found:** after `UploadMesh` the CPU `indices` pointer must stay non-NULL or
  raylib's `DrawMesh` silently switches to a non-indexed draw (this produced "hairy" random
  triangles). `lg_upload_mesh` keeps a sentinel pointer; `lg_unload_mesh` clears it.

## Phase 5 — Segmented rings

- Rings every 5–10 units (archetype-scaled), gate clusters of 3–5 at 1.5 spacing, ≤ 16 per chunk.
- 2–3 layers in `[0.95R, 1.4R]`, 3–12 segments, gaps 12–45 %; styles BAND, CASTELLATED (cells
  5–12°, step 0.5–1.4 scaled by R/12, radial edges at transitions), TICKS, DASHED, DOUBLE;
  spokes with pads.
- Vertices are ring-local; per-chunk `uRingA/B/C[16]` vec4 arrays carry the basis; the vertex
  shader rotates each layer by `angVel·time` (alternating sign by layer). No CPU work per frame.
- Data pulses in the fragment shader along arc length.
- **Added:** longitudinal wall "rails" (on/off per 12-unit grid cell, global hash so they join
  across chunks). The reference t_9.4 is dominated by these long radial lines.
- Defaults were tuned down from the brief (layers 2–4 → 2–3, band width 0.15–0.5 → 0.10–0.28)
  because more/wider layers turned the walls into a translucent mush compared with t_9.4/t_9.9.

## Phase 6 — Speed streaks

- One static mesh of 6000 quads; everything derived from the streak id in `streak.vs`.
- **Deviation:** instead of a cylinder around the tangent at the ship (with an optional
  quadratic bend, which I tried first and which exploded on curvier stretches), the shader
  gets 16 path frames along the streak volume (`uPathP/B/N[16]`) and interpolates, so streaks
  follow the real tunnel.
- Speed multipliers are quantized to k/8 and the travel distance wraps at `zRange·64`, so the
  wrap is seamless. Streaks within ~12 units of the camera are faded (they otherwise smear
  across the whole screen).
- 1 % comets with bright heads, 15 % purple, 20 % helix streaks hugging the wall.

## Phase 7 — Spiral ribbons

- 3 ribbons, angle `θ0 + ω·s` from world-seed hashes, radius 1.05–1.3R, width 2–5.
- `u` = s wrapped at 960 (a multiple of the chunk length) and the shader's value noise is
  periodic in u with matching lattice periods, so there is no seam at chunk boundaries nor at
  the wrap.

## Phase 8 — Debris

- 0–6 per chunk (archetype ranges), cubes 0.6–2.5, 20 % octahedra, polar placement with the
  bounding sphere kept outside `keepout·R` (tested). Tumble in the vertex shader from
  `uObjA/B/C[16]` (center+size, axis+speed, phase). Edges = camera-facing lines, faces =
  additive "data" pattern (hash-toggled rectangles, slow flicker).
- Collision query: `ct_debris_in_range(ct, s0, s1, ...)`.
- Holographic rings: 0–2 sets per chunk of 1–3 concentric tilted ellipses with optional gap.

## Phase 9 — Core, fog, atmosphere

- Core is a screen-space additive pass (hot center, halo, anamorphic streak, rotating rays),
  brightness scaled by how centered it is.
- **Deviation:** the brief places the core at `path(s + 0.9·viewDist)`. With the specified
  curvature that point is frequently off-screen, which loses the visual anchor. The core walks
  outward along the path and stops where the tunnel leaves the screen (smoothed), i.e. it sits
  at the visible end of the tunnel and equals the brief's position on straight stretches.
- Glow fog toward the core tint (`fogStart/fogEnd/fogToCore`) plus far fade; background is a
  navy-to-teal gradient around the vanishing point plus hashed static dust on view directions.
- Whiteout: `W` (or `--whiteout` in shot mode) runs whiteout + core flare.

## Phase 10 — Ship, flight, camera

- Dart hull (opaque, rim lit), cyan edge lines, two engine glows, trails from a 0.6 s ring
  buffer (dynamic mesh updated with `UpdateMeshBuffer`, cyan core / magenta edge).
- Flight: speed smoothing, steering offset with damping clamped to `0.55R`, bank ∝ lateral
  velocity (35°), tilt ∝ vertical velocity, Q/E barrel roll.
- Chase camera: critically damped spring **in the path frame at the ship** (right/up/forward
  offsets). A world-space spring lags `2v/ω` behind a 40–80 u/s target, i.e. 10+ units, so the
  path-frame formulation is used; it is also immune to origin rebases and chunk boundaries.
  Up vector smoothed with `1 − exp(−k·dt)`; look-at blends ship and `path(s + 25)`;
  FOV 70° + 15°·boost; shake at boost; camera roll = tunnel roll + `cameraRollFollow` × bank.
- **Accept (test):** the fixed-step accumulator (`flight_advance`) driven at 30, 60 and
  144 fps with the same input script: identical step counts and ≤ 0.013 units ship/camera
  deviation after 6 s (inputs are sampled per frame, so bit equality is not expected).
- F2 free camera (raylib `CAMERA_FREE`, debug only); P pauses the sim.

## Phase 11 — Pacing

- Archetypes CALM / GATES / DENSE / CORRIDOR as multipliers on the base parameters (spacing,
  gate chance, debris range, castellated weight, gap, brightness, ribbons, rail density, energy).
- Hash-driven Markov chain over runs (run r seeded by `hash(seed, r)`), min/max run lengths
  5–10 / 3–7 / 3–6 / 3–6 chunks (≈ 6–12 s at base speed), no self transitions. Order-independent:
  the archetype of chunk k is found by walking the runs from 0 (cheap).
- Parameters blend across the first chunk of a new archetype (smoothstep in s).
- Global intensity = archetype energy at s + 40, smoothed; `ct_set_intensity` overrides.

## Phase 12 — Tooling

- Registry-driven raygui panel (F1): seed / rebuild / pause / whiteout / env switch / jump to s /
  presets; collapsible groups; editing a Path group param triggers a full rebuild, content groups a
  chunk rebuild (on mouse release).
- Overlay (F3): FPS, frame ms, CPU ms per pass, chunks, builds/uploads, chunk build ms, vertices,
  draw calls, s, origin, max GL coordinate, intensity + archetype, shader reload count.
- Presets: `default.ini` (generated with `--write-preset default`), `calm.ini`, `intense.ini`
  (sparse overrides).
- Tests (`cybertunnel_tests`, 35 checks): RNG, noise, path determinism/regeneration/continuity/
  curvature/C1, chunk order-independent hash, far-chunk local coordinates, batch vertex limit,
  debris keep-out, frame-rate equivalence, ini round trip and clamping.

## Bench results

| Run | Result |
|---|---|
| `--bench 600 --speed 120 --vsync 0` (10 min, 3× base speed, 1280×720) | 1,061,064 frames, avg 0.401 ms, p50 0.393 ms, p99 0.537 ms, max 6.82 ms |
| streaming during that run | max chunk build 0.87 ms, max build+upload in one frame **1.40 ms** (target: no hitch > 4 ms from streaming) |
| precision during that run | final s = 51,141; max float coordinate passed to GL **1401** (< 2048); no visible jitter at s > 50,000 |
| GPU meshes during that run | 36–59 (varies with archetype content; bounded, no growth) |
| `--bench 30 --speed 40 --vsync 0 --w 1920 --h 1080` | avg 0.585 ms, p50 0.593 ms, p99 0.722 ms; 74 draw calls, ~146k vertices; CPU update 0.004–0.09 ms, scene submission 0.19–0.38 ms |

The single 6.8 ms / 17.9 ms max frames do not coincide with streaming (streaming max is 1.4 ms);
they are compositor/driver stalls in an uncapped windowed run. All budgets in §6 (CPU < 2 ms,
chunk build < 3 ms, < 150 draw calls) are met on this machine. Runtime environment switching was
smoke-tested (10 switches while rendering, no crash).

## Screenshots (`screenshots/`, committed, downscaled to 960 px)

- `vortex_seed1_s1200.png`, `vortex_seed1_s3000.png`, `vortex_seed1_s4200.png` — default preset
- `compare_t9.4.png` — s=3000 next to reference t_9.4
- `calm_seed1_s3000.png`, `intense_seed1_s3000.png` — presets
- `debug_ui.png` — panel + overlay; `template_env.png` — template environment


## Phase 13 — Integration prep

- `cybertunnel.h` public API; `main.c` uses only it. `env_template.c` (switch with `T` or
  `--env 1`). `INTEGRATION.md` with a generated parameter table.

## Open questions

- Visual: our far tunnel still accumulates into a brighter haze than the reference in DENSE /
  CORRIDOR stretches. `fogToCore`, `farFadeStart` and the archetype `ringMul` are the knobs.
- The core can legitimately end up behind the ship when the path pitches down right ahead
  (e.g. seed 1, s ≈ 5000). A gentler `maxPitchDeg` or a camera that looks further ahead on
  pitch changes would keep it more central; left as tuning.
- Engine glow intensity is baked into the ship mesh at creation (`engineIntensity` changes
  need a restart).
- Not measured here: the GTX 1660 / Iris Xe targets in §6 — only one machine was available.

---

# Environment #2 "Neon Grid City" (plan: `PLAN_ENV2_GRID_CITY.md`)

## Phase A — Core generalisation (no visual change)

- `PathStyle.floorDepth` / `ceilingHeight` (0 = none). `flight_step` clamps the vertical
  offset to `[-floorDepth + FLIGHT_CLEARANCE, ceilingHeight - FLIGHT_CLEARANCE]` after the
  existing radial limit (`FLIGHT_CLEARANCE = 0.6` covers wingtips at full bank).
- `Environment.atmosphere` optional hook, called on the per-frame `Atmosphere` copy after the
  core fills it.
- Streaks extracted to `render/streaks.c` (`StreakParams` + `StreakSystem`); vortex keeps its
  param names (`streakDensity`, `purpleFrac`, … — `purpleFrac` maps to `accentFrac`) so presets
  load unchanged. Cube/octa builders moved to `render/shapes.c` (`shape_cube`, `shape_octa`).
  Slab volume / fall tilt for rain are deliberately **not** added yet (Phase D).
- `ENV_CITY = 2` / `CT_ENV_CITY` registered; `env_city.c` stub with the real path style
  (lane half-width 9, yaw step 4°, pitch 0, twist 0, floor 2.2), fog to dark
  (`fogToCore = 0`, 120–420), and lane-edge dashes on the floor plane. City params are prefixed
  `city*`; the "City" group triggers a chunk rebuild in the panel.
- `--shot` made reproducible (fixed step, see top of file).
- **Accept:**
  - Tests: 41 passed (35 existing + 6 new). The order-independence check became the helper
    `build_order_independent()` and now also runs on the city. New: city path max |y| = 0 and
    min N.y = 1.000000000 over 20,000 units; with full-down + sideways input for 6 s the lowest
    ship y is −1.600 (floor −2.2 + clearance 0.6) while the ship still reaches the clamp.
  - Pixel identity: 8 renders (vortex s=1200/3000/4200, seed 7, calm, intense, whiteout,
    template) are byte-identical before and after the refactor.
  - City stub: `--env 2 --bench 8 --speed 120`: avg 0.18 ms, max chunk build 0.007 ms.
- Not checked: switching environments at runtime with `T` (no scripted key input in the
  harness); the code path is unchanged apart from the extra environment.

## Phase B — Grid floor and sky

Direction from the user: paths may drift gently as in the vortex, as long as flight stays a
treadmill (fixed path, player moves inside the lane, camera pans slightly toward the
movement). Per-environment camera settings are on hold until the environment transitions are
designed (one idea: the vortex itself as the transition).

- `render/gridfloor.c` + `grid.fs`: one fullscreen pass in the opaque phase. The view ray
  through each pixel hits the plane `y = floorY`; the shader writes `gl_FragDepth` (from the
  core's `zNear/zFar`, now in `FrameContext`) so towers will depth-test against it, and
  discards misses so the sky shows. No floor geometry, no streaming, infinite extent.
- Filtering: per axis, the line core is a box widened to ≥ ½ pixel with its energy kept and
  blended to its mean coverage once cells shrink below a pixel; the halo is a gaussian widened
  by the pixel footprint (energy kept) and blended to its lattice mean (h·√π). `gridFilter = 0`
  turns this off (debug) to show what it prevents.
- Precision: the shader only sees render-space positions plus `gridfloor_offset()` = render
  origin mod (1024 cells), reduced in double.
- `sky.fs`: zenith→horizon gradient, horizon haze band, dust above the horizon. At the horizon it
  equals `horizonColor × hazeGlow`, which is exactly what the floor fogs into (the env passes
  `hazeGlow` as the floor's `horizonGlow`), so the two meet without a seam.
- **Path heading spring (deviation from the plan's "free drift"):** with free drift the heading
  wandered ~40° by s = 800 and the world-aligned grid ran diagonally across the lane; the
  reference flies along the grid. New `PathStyle.yawSpring` / `yawAxisDeg` pull the heading
  toward the nearest world axis *inside the existing per-step yaw clamp* (so
  `path_curvature_limit` still holds). City: spring 0.12, axis 90°; the path still weaves,
  max 21.6° off-axis over 20,000 units. Vortex has spring 0 and takes the old code path.
- Tuning vs t_0.5 (floor only, `screenshots/compare_city_t0.5.png`): cell 2.2 (≈ the reference's
  8 cells across the bottom of the frame), core half width 0.03, halo 0.07, white core 0.25,
  floor depth 1.4 (was 2.2: the reference ship skims the floor).
- Harness: `--set NAME=VALUE` (repeatable, rebuilds afterwards); `rebaseDist` Sim param
  (default 1024 = previous constant).
- **Accept:**
  - Shimmer (1080p, 10 consecutive frames at s = 1500, mean per-frame change in grey levels):
    far floor just below the horizon (rows 485–515) **0.2–0.9 filtered vs 27–36 unfiltered**;
    mid/near floor ≈ 45 for both (real motion). Stills: `screenshots/city_grid_filter_on_off.png`
    (top filtered, bottom unfiltered, 2× crop of the horizon).
  - Rebase continuity: the same shot with `rebaseDist=1` (snap every frame) vs `100000`
    (never): 0 pixels differ by more than 2 %, PSNR 71.6 dB at s = 3000 and 71.9 dB at s = 52,000
    (vortex: 74.2 dB). Test: grid phase from `gridfloor_offset` + float local position vs exact
    double, 20,000 random origins up to 1e6 → worst error 2.4e-4 cells.
  - Tests: 44 passed (new: heading near axis, curvature within limit with the spring, grid phase).
  - Vortex regression set still byte-identical.
  - Bench (autopilot, 60 s): 1080p speed 120 avg 0.52 ms / p99 0.62 ms; 720p speed 40 avg
    0.29 ms; 17 draw calls.
- Open: our camera frames the horizon at ~45 % from the top vs ~55 % in the reference (camera
  pitched further down); left alone per the camera decision above. Floor reflections (the purple
  smears and the trail stripe under the ship) are Phase E.

## Phase C — Towers: placement and bodies

- `env/city_towers.c` (pure CPU): world xz lattice of `towerLatticeCells` (8) floor cells, so
  footprints land on grid lines. A cell belongs to the chunk containing s*, the nearest path
  point to its centre, found on a polyline of path samples at fixed global arc lengths
  (every 2 units). Every chunk that looks at a cell runs the same computation, so all agree
  on the owner. A chunk only looks at cells within `bandMax` of its own piece of the path and
  searches s* within `city_towers_reach()` = 2.2 · bandMax + 4. That covers the true nearest
  point while the path cannot bend back on itself, which the yaw spring guarantees.
- New core field `Environment.pathReach`: the core generates the path that far beyond the
  chunk window (`ensure_path`, also used by `ct_jump_to`'s regeneration check). City: 668
  (enough for bandMax up to 300).
- Per cell (RNG from `hash(seed, ENV_CITY, F_TOWERS, ix, iz)`): occupancy (near 0.5 → far 0.3),
  footprint 3–5 cells, height 28–65 + up to 40 with distance, 5 % landmarks ×1.7, 0–2 setback
  tiers inset by one grid cell. The footprint's exact distance to the path polyline
  (segment/rectangle distance) must be ≥ lane + clearance (9 + 6), otherwise the cell stays empty.
- Bodies: `tower.vs/fs`, opaque, drawn before the floor. Panel seams and recessed panels (world-
  unit face UVs, seams fade out below a few pixels), key light + faint cyan rim, grid underglow
  near the base, fog toward the sky colour *along the view ray* (same formula as `sky.fs`).
  Edges: dim cyan camera-facing lines (emissive pass).
- Pop-in: the far chunk becomes visible with its nearest towers ≈ 358 units away, so the city
  fog ends at 350 (`cityFogEnd`) and emissive lines use the same range as their far fade.
- Tuning vs t_0.5: density/footprint/height lowered from the first try, which read as continuous
  walls; clearance 6 gives an avenue (`screenshots/compare_city_towers_t0.5.png`,
  `city_towers_seed4_s3000.png`). The purple traces and code text are Phase D.
- **Bug found by the tests (test setup, not placement):** ensuring 200 chunks of path up front
  overflows the 4096-unit path ring, so `path_sample(0)` clamped to s ≈ 5900 and chunk 0
  claimed cells far away. Tests now stream the path per chunk (`ensure_for_chunk`), as the core does.
- **Accept:**
  - 200 chunks, built in order and shuffled (env level, hashes): identical; all 199 neighbours differ.
  - Placement over 200 chunks: 3244 towers, 9415 claimed cells, **0 cells claimed twice**;
    9200 cells checked against a brute-force nearest point on the whole path: **0 unclaimed,
    0 with the wrong owner**; minimum footprint distance to a 0.5-step path polyline 15.01 (limit
    15.0); all footprints on grid lines.
  - Build: worst 0.34 ms per chunk in the test; in the 1080p bench max chunk build 1.18 ms, max
    streaming per frame 1.24 ms (budget 4 ms).
  - Bench (60 s, 1080p, speed 120): avg 0.64 ms, p99 0.77 ms, 37 draw calls, 13k vertices.
  - Tests: 53 passed. Vortex regression set byte-identical.

