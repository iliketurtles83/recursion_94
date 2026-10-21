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
                    [--boost]               (hold boost during --shot)
```

Keys: arrows steer, Shift/Space boost, Q/E barrel roll, F1 panel, F2 free camera (WASD +
mouse), F3 overlay, F12 screenshot (`shots/vortex_seed{seed}_s{s}.png`), R reseed, P pause, [ ] base speed, - = boost speed,
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

## Speed change (user request, after Phase C)

The overall flying speed was faster than wanted: the old base speed (40) is now the boost speed,
and the base is 25 (`baseSpeed` 25, `boostSpeed` 40; presets: calm 18/30, intense 40/70,
`default.ini` 25/40). Live tuning keys: `[` / `]` base speed ±5, `-` / `=` boost speed ±5; the
overlay shows both. Things that scale with absolute speed and may want retuning once a speed
is chosen: streak length (`streakLenMul`, streaks stretch with speed), and pacing runs, which
are measured in chunks, so at 25 u/s an archetype run lasts ~1.6× longer than before. Zoom blur
and the boost FOV are relative to base/boost and are unaffected. The vortex regression renders
were re-baselined after this change.

## Phase D — Circuit traces, code panels, rain, props

- `env/city_decor.c` (pure CPU, seeded from the tower hash): each face is classified against
  the path at the tower's owner point. Faces toward the approaching camera get dense trace buses;
  faces toward the lane get a glyph panel (2-column strip by the front edge 55 %, block 25 %) and
  sparser traces around it; faces turned away get nothing.
- Trace routing: a square lattice per face (pitch 0.55) with a 64-bit-per-row occupancy bitmap.
  A bus of 2–4 traces climbs in runs of 3–14 rows and jogs 1–3 columns at 45° as one, always
  keeping one free column to anything else, so buses never cross or touch. Traces drop out one by
  one (staggered endings) and end in a via; buses starting mid-face start at vias. Stubs under
  5 rows are dropped (they read as pale blocks). Glyph panels reserve their cells first.
  Density falls to 35 % at bandMax (LOD). Strips sit 0.08 off the face: below that, depth
  precision near the fog distance (≈ d²/(near·2²⁴) = 0.05 at 300 units) causes z-fighting.
- `trace.vs/fs`: planar strips with min-pixel widening (energy kept), the neon line profile,
  and data pulses running upward on ~45 % of traces.
- `glyph.vs/fs`: code text. Rows of dash "words" with hashed lengths, indents and blank lines,
  pink/violet (strips) or pink/pale-cyan/violet (blocks), slow upward scroll, row flicker,
  and a fade to average brightness once rows or characters shrink toward a pixel.
- Rain: `StreakParams.volume = 1` (slab ±45 across, 0.8–25 above the floor) plus `fallTilt`:
  streaks start higher the further ahead, so they descend as they approach. The cylinder branch
  in `streak.vs` is untouched (vortex renders byte-identical). 1200 streaks, density 0.5, long
  (`rainLenMul` 4), wide, 5 % comets.
- Props: 0–3 per chunk, slowly turning cubes, octahedra and wire spheres (new `shape_sphere`)
  between the lane edge and the towers, 0.4–4 above the floor; same object/face shaders and
  per-chunk uniform layout as the vortex debris; registered as collision debris.
- Tuning: first pass had solid hot-pink strips on distant towers and a starfield of rain at the
  vanishing point; glyph intensity 2.0 → 1.2, fixed 2-column strips, rain density 1 → 0.5,
  half width 70 → 45, z range 220 → 170.
  (`screenshots/compare_city_decor_t0.5.png`, `compare_city_decor_t2.5.png`,
  `city_decor_closeup_1080p.png`)
- **Accept:**
  - Tests: 57 passed. Over 200 full city chunks: max 40,558 vertices per chunk (budget 60k),
    on average 8k trace vertices and 13 glyph panels per chunk, 325 props all between lane edge
    and towers and above the floor, worst full chunk build 1.56 ms. Env-level order
    independence (200 chunks) still holds with decor.
  - Bench 1080p, speed 120, 60 s: avg 0.69 ms, p99 0.83 ms, 76 draw calls, ~100k vertices;
    max chunk build 2.55 ms, max streaming per frame 2.96 ms (budgets 3 / 4 ms: within, but the
    build is the closest to its budget; first candidate for optimisation if content grows).
  - Vortex regression set byte-identical.

## Zoom blur is a boost effect (user feedback)

At base speed the radial zoom blur smeared the edges without adding anything; when boosting it
helps the feeling of speed. It used to scale with `speed / baseSpeed` (always 1 when cruising);
now it ramps with `speedNorm` (0 at base, 1 at boost): `zoomBlur` is the strength at full boost
(0.096 = the previous 0.06 × the new boost/base ratio 1.6, so boost looks unchanged) and
`zoomBlurAtBase` (default 0) is the fraction kept at base speed. Presets updated
(default 0.096, calm 0.048). Harness: `--boost` holds boost during `--shot`.
`screenshots/zoomblur_base_vs_boost.png` (left base, right boost; city top, vortex bottom).
The regression renders were re-baselined (they are taken at base speed).

## Phase E — Floor reflection

- Extra reference frames pulled from `reference/video.mp4` with ffmpeg (`reference/t_1.0s.png`,
  `t_2.0s.png`): the floor is a glossy dark navy; reflections are soft, vertically smeared
  purple/pink streaks under the code strips and traces, faint rain dashes, and a bright cyan
  stripe under the ship from the engine trails. Grid lines are not reflected (they are on the
  surface); line crossings have small bright square pads.
- Core (`render_reflection` in `cybertunnel.c`): when the environment sets
  `Environment.reflectiveFloor`, `PathStyle.floorDepth > 0` and `reflectRes > 0`, the scene is
  rendered a second time with `view' = R·view`, R = mirror about the floor plane in render space.
  A floor point projects to the same pixel in both passes, so the floor samples the reflection
  at its own screen uv. The pass draws the env's opaque + emissive content and the ship (hull,
  edges, trails) into a ½- or ¼-resolution RGBA16F target with depth (towers occlude what is
  behind them), using a FrameContext copy with `reflectionPass = true` and the mirrored camera
  (billboards, fog and fades use it). Envs skip their floor and sky in that pass. Mirroring flips
  winding; culling is already off during the scene.
- Blur: two vertical 9-tap gaussian passes (`reflect_blur.fs`, the second twice as wide):
  wet-floor streaks that stay sharp horizontally.
- `grid.fs`: adds `reflection × floorReflect × Fresnel` (R0 = 0.3 looking down, 1 at grazing)
  before the distance fog; new square crossing pads (`gridNodeSize`, filtered like the lines).
  Floor base colour slightly bluer (`#030B14`).
- No clip plane: nothing in the city sits below the floor (rain ≥ 0.8, props ≥ 0.4, ship ≥ 0.6
  above it). An environment with geometry below its floor would need one.
- Reflection pass skips the city's dim tower outlines and faint prop faces (invisible after the
  blur): 140 → 122 draw calls.
- Tuning against t_1.0/t_2.0/t_2.5: `floorReflect` 1.3, `floorReflectR0` 0.3, `reflectBlur` 2.5,
  `gridNodeSize` 0.12, `gridNodeBoost` 2.0. `screenshots/compare_city_reflect_t1.0.png`,
  `compare_city_reflect_t2.5.png`; geometry check with exaggerated, unblurred reflection and dim
  grid: `city_reflection_debug.png` (towers mirror at their bases, mirrored ship below the ship).
- **Accept:**
  - Cost at 1080p (speed 40, 30 s, before the batch skip): off 0.42 ms, ¼ res 0.50 ms,
    ½ res 0.54 ms average. With the skip, ½ res (default): 0.53 ms avg, 122 draw calls
    (budget 150); speed 120 for 60 s: 0.56 ms avg, p99 0.73 ms.
  - Tests 57 passed; vortex regression set byte-identical (`reflectiveFloor` is off there).

## Phase F — Pacing, presets, docs

- Archetypes (hash-driven Markov chain over runs of chunks, same scheme as the vortex;
  every flight starts on an AVENUE, the reference opening). Each is a `CityArch` of
  multipliers/offsets on the base params:

  | Archetype | Run (chunks) | Character | Energy |
  |---|---|---|---|
  | AVENUE | 4–9 | two tidy rows lining the lane (near density ×1.6, far ×0.8) | 0.45 |
  | DOWNTOWN | 3–6 | dense and tall (far density ×2, heights ×1.35, far heights ×1.5, landmarks ×2.5), more traces and glyph panels | 0.85 |
  | OPEN | 3–6 | sparse and low (near ×0.3, heights ×0.55), towers start 8 units further out | 0.25 |
  | PLAZA | 2–3 | no towers near the lane (+26 clearance), props ×2 | 0.35 |

- Order independence across archetype boundaries: a tower's parameters are blended by the
  cell's owner point s* across the first chunk of a new archetype (`city_arch_at`), and only the
  owning chunk builds the cell, so a cell's look depends on (seed, params, s*) alone. The
  archetype never moves a tower closer than the base clearance (`clearanceAdd ≥ 0`), so the lane
  guarantees hold. The tower lattice and `bandMax` stay global (they define ownership).
- Decor follows the tower's archetype (`TowerDesc.traceMul`, `glyphMul`); props scale per chunk;
  `energyAt` drives the global intensity (grid brightness, rain speed and density).
- `cityPacing = 0` uses the base parameters everywhere. Overlay shows the archetype name.
- `presets/city.ini`: full snapshot of the tuned city (written with `--env 2 --write-preset city`,
  like `default.ini` for the vortex). Load after selecting the city: `--env 2 --preset city`.
- `INTEGRATION.md`: new §10b "Available environments"; city params complete.
- `screenshots/city_archetypes.png`: AVENUE (s=40), DOWNTOWN (300), OPEN (1400), PLAZA (4230), seed 1.
- **Accept:**
  - Tests: 60 passed. Pacing over 200 chunks: 47 runs, AVENUE 91 / DOWNTOWN 49 / OPEN 26 / PLAZA 34
    chunks, all runs within bounds, starts on AVENUE. Env-level build order independence (200
    chunks) holds with pacing. Heaviest chunk (DOWNTOWN) 51k vertices (budget 60k); worst full
    build in the test 1.7 ms.
  - Environment switching at runtime (smoke program through the public API, real window): 10
    switches city ↔ template ↔ vortex including jumps back (path regenerated from 0) and to
    s = 52,000, 900 rendered frames, no errors; city 111–121 draw calls, vortex 62–68.
  - 10-minute bench (`--env 2 --bench 600 --speed 120`, 1280×720, autopilot, final s = 72,084):
    avg 0.41 ms, p99 0.58 ms, max frame 14.6 ms; max streaming per frame 3.72 ms (target < 4 ms);
    max GL coordinate 1418 (< 2048); GPU meshes 40–60 (bounded); 114 draw calls.
  - One earlier run had a single 107 ms frame. The bench is deterministic, so I added hitch logging
    (`bench: hitch …` for any frame > 20 ms, with that frame's and the previous frame's streaming
    work) and re-ran the identical flight twice: worst frames 8.9 ms and 14.6 ms, no hitch logged.
    The spike was outside the module (driver/compositor), like the 17.9 ms one in the vortex bench.
  - **Over budget:** max chunk build in the game runs is 3.2–3.4 ms vs the brief's 3 ms. The same
    chunks built headless (seed 1, 2000 chunks) take p50 0.48 / p99 0.93 ms, max 0.9 ms warm, so
    the in-game peaks are the build thread being descheduled while the driver works, not the work
    itself. Pre-reserving the chunk buffers (to rule out realloc growth) did not change the peak
    and was reverted. Streaming per frame stays within its 4 ms target. If it matters for the
    game: build on a worker thread (buildChunk is already pure CPU), or split decor into its own
    budgeted step.

## Light rain retune (user feedback)

The rain was too heavy: it should only give the feel of passing through, with almost none in
the middle where enemies will be, and shorter lines. `rainDensity` 0.5 → 0.3, `rainLenMul` 4 →
2.8, and the slab now has a mostly empty centre band: `rainCenterGap` (half width 12, a bit wider
than the lane) with only `rainCenterFrac` (5 %) of the streaks inside it; the rest spawn over the
tower sides. New `StreakParams.slabGap/slabGapFrac` (`uSlabGap` in `streak.vs`, slab branch only;
vortex renders byte-identical). `screenshots/city_rain_before_after.png`. Boost still stretches
the streaks with speed.

## Environment #2 summary

The Neon Grid City is complete per `PLAN_ENV2_GRID_CITY.md`: infinite filtered grid floor with
planar reflection, world-aligned towers with circuit traces and code text, light rain, props,
four pacing archetypes, `presets/city.ini`. Core additions made for it are generic (floor/ceiling
clamp, yaw spring, path reach, atmosphere hook, reflection pass, shared streaks/shapes/grid floor)
and are documented in `INTEGRATION.md`; they are what Env #3 (server canyon, same grid floor)
will build on. Open: per-environment camera framing and environment transitions, both waiting on
the transition design.


# Environment #3 — Server Corridor

Plan: `PLAN_ENV3_SERVER_CORRIDOR.md`. Reference frames `reference/t_4.0s` … `t_7.25s`.

## Phase A — Extraction and stub

- `env/lattice_place.c/.h`: the city's world-lattice cell ownership (path window around a chunk,
  nearest-point owner, exact path-to-rectangle distance) as a generic iterator
  (`lattice_window_load`, `lattice_for_each` with a per-cell callback, `lattice_rect_dist`,
  `lattice_nearest`). `city_towers_plan` is now a callback on it.
- `core/pacing.c/.h`: the archetype Markov chain (`PacingTable` of run bounds + transition
  matrix → `pacing_archetype`), the smoothstep blend factor and the float-struct lerp. City and
  vortex keep their own tables.
- `env/trace_route.c/.h`: the trace bus router from `city_decor.c` (occupancy lattice, buses with
  45° jogs, vias). Faces now carry an in-plane `v` axis, so horizontal faces (beam undersides)
  can be routed too; colours, start-row and alternate-colour chances are parameters.
  `city_decor.c` keeps face classification and glyph panels.
- `ENV_CORRIDOR = 3` (`CT_ENV_CORRIDOR`, `--env 3`) with a stub `env_corridor.c`: level path,
  lane half-width 6.5, floor 2.2 below, ceiling 14 above, yaw spring 0.35 with a 3° step,
  grid floor (cell 2.0), dark navy sky, floor reflection on. Params are prefixed `cor` so they
  never collide with the city's in presets.
- **Accept:**
  - Tests: 65 passed (60 + 5 new). Corridor path over 5 seeds × 20,000 units: max |y| 0, min
    N.y 1.000000000, max heading off-axis 8.3° (target ±12°), curvature within limit.
  - Regression set (13 renders: vortex s=1200/3000/4200, seed 7, calm, intense, template, city
    s=40/300/1400/4230, city seed 4, city preset) byte-identical before and after the extraction.
  - Stub bench (`--env 3 --bench 8 --speed 120`): avg 0.15 ms, 22 draw calls.

## Phase B — Pillars: placement and bodies

- `env/corridor_pillars.c/.h`, on `lattice_place` with an 8-unit lattice (4 floor cells of 2.0).
  The corridor's yaw spring keeps the heading within ~8° of one world axis, so each cell takes
  its lane axis from the path tangent at its owner point ("along" with the lane, "across" away
  from it); the camera-facing end is the one toward −T.
- Wall rows: cells whose usable near edge is within `corWallBand` (6) of lane + clearance. The
  lane face sits at the closest allowed position plus 0–1.5 slack (snapped to 0.5), and is pushed
  out in 0.5 steps until the exact path-to-footprint distance (`lattice_rect_dist`) clears
  lane + clearance; otherwise the cell stays empty. Width 4–6.5 along the lane inside the cell.
- Merged slabs: even/odd cell pairs along the lane. The even cell may build one slab across both
  (gaps 0.5–2 at the ends); the odd cell evaluates its partner's occupancy and merge roll itself
  (both computed from the global nearest-point owner), so it is empty exactly when the partner
  extends into it, whatever the build order. Window reach covers the extra lattice cell.
- Back rows: placed freely inside their cell, density ramping in over `corBackRamp` (30) behind
  the wall band so gaps between wall slabs stay dark; taller with distance, darker tint, 0–1 step.
- Profile: 1–3 steps on wall pillars, the lane face moving in or out by 1–2 units (snapped to
  0.5), never closer than the base lane face. Geometry is only what the chase camera can see:
  per tier the lane face and the camera-facing face, plus overhang undersides and ledges.
  Shaded with the city's `tower.fs` (rack-like panels 2.4 × 1.8, darker slate) instead of a new
  `pillar.fs`: the panel uniforms already give the rack look, so a separate shader was not needed.
- (Screenshots: see Phase E, `corridor_e.png`.)
- **Accept:**
  - Tests: 75 passed. 200 chunks, 5709 pillars (1716 wall, 247 merged slabs, 879 with an outline
    flag): no cell claimed twice, every cell in the band claimed by its brute-force owner, no
    overlapping pillars, min lane gap 7.50 (lane 6.5 + clearance 1.0), worst build 0.52 ms;
    env-level build order independence over 200 chunks.
  - Curves: with the heading within 8° the world-aligned walls stagger in and out by at most a
    few units along a chunk; reads as natural variation, no need for path-aligned placement.
  - Bench (`--env 3 --bench 20 --speed 120`): avg 0.23 ms, 42 draw calls, max chunk build 1.65 ms.
  - Regression set byte-identical.

## Phase C — Code rain and stepped outlines

- Visual check against t_6.0 / t_7.0 changed the pillar proportions: in the reference the near
  pillars are deep across, so their camera-facing faces (code rain + outline) fill the frame. The
  lattice is now 12 units (6 floor cells), wall pillars 5–9 along × 6–10 across, wall band one
  full cell (12) at density 0.75. Steps on wall pillars sit between 4 units and ~55 % of the
  height (where the chase camera sees them) and move the lane face by 1.5–3 units.
- Code rain (`coderain.fs`, vertex shader `glyph.vs`): one quad per tier on the lane face and on
  the camera-facing face, 0.05 off the surface (back rows too, at 35 %; lane faces at 60 %, they
  are seen at grazing angles and read denser). u in world units across the face, v above the
  floor, so columns line up across tiers. Per column (hash): on/off (35 % empty), vertical span,
  speed 1–4 u/s, dash period 0.32–0.62 and length; per dash: on/off (70 %) and brightness; rounded
  dashes from an SDF; a brighter head with a trail runs down some columns; 18 % mint columns, rare
  white flicker in the trail. Colour `#39FF6A`, HDR 1.6.
- Distance filter: when columns (`fwidth(u/pitch)`) or dash rows shrink below ~2 px the pattern
  converges to its mean brightness, halved, because a fully averaged grazing face otherwise reads
  as flat green paint.
- Outlines: for wall pillars with the outline flag (~half), a polyline in the camera-facing plane
  follows the lane-side silhouette from the floor up every step and 30–100 % across the top. It
  is a planar strip with mitred right-angle corners (in-plane normal turned toward the outside of
  the silhouette, summed at corners), offset 0.1 off the edges, half width 0.2, HDR 4, own shader
  `outline.fs` (white-hot core, pale cyan halo) with a gain so the reflection pass draws them at
  35 % (full strength made long hard lines on the floor).
- Floor kerbs: a camera-facing segment from the outline's foot along the lane toward the camera,
  2–6 long, on the floor. The reference also runs some floor lines across the lane; those are
  left out so nothing bright crosses the centre band where enemies will be.
- Floor retuned for the enclosed look: grid intensity 1.7, halo 0.2, node pads 1.0, reflection 1.0.
  Pillar bodies brighter (2.6) so the slate shows between the rain.
- `screenshots/compare_corridor_c.png` (reference t_6.0 / t_6.5 / t_7.0 left, s = 100 / 2600 /
  1500 right). Beams, sky lattice, rings and rain come in D and E.
- **Accept:**
  - Tests: 79 passed. New: over 200 full chunks every outline and kerb vertex stays ≥ 6.98 from the
    path (lane half-width 6.5; the ship reaches at most 0.9 × 6.5 = 5.85); max 1144 vertices per
    chunk, worst build 0.26 ms.
  - Shimmer: no scripted 60 s flight capture exists, so I used frames 1/60 s apart at base speed
    (camera moved 0.42 units) and measured the mean absolute difference in the far third of the
    screen, with the city as the accepted baseline: corridor 0.047–0.055 (720p) / 0.052–0.065
    (1080p) vs city 0.033–0.051 / 0.036–0.060, at s = 900 / 2600 / 4100. Same range, no
    shimmer seen in the 1080p crops.
  - Bench 720p speed 120, 30 s: avg 0.31 ms, p99 0.45 ms, 98 draw calls; 1080p speed 40:
    avg 0.49 ms, 102 draw calls. Max chunk build 0.9 ms.
  - Regression set byte-identical.

## Phase D — Overhead beams and sky lattice

- `env/corridor_beams.c/.h`: beams on a 16-unit world lattice, owned through `lattice_place`
  (band 70). Lines (rows along x, columns along z) are selected by their own hash (1 in 3) and
  carry their width (6–10), thickness (2–3) and height level, so every cell of a line agrees.
  Hashed per-cell gaps (25 %) break them. Beams running along the lane are kept out from above
  it (`corBeamLaneKeep` 8 from the path, else they roof the corridor); beams crossing it frame
  the sky. A segment gets an end cap only where the next cell on its line has no segment, which
  each chunk evaluates the same way, so beams join across chunk boundaries. Rows use even height
  levels and columns odd ones, so crossing beams never share a face plane. Geometry: underside,
  both sides, caps (no top, never seen).
- `beam.fs` (on `tower.vs`): dark body with procedural cyan circuitry: segmented runs along the
  beam on a 0.5 lattice, sparser cross runs, vias at segment ends, data pulses; dark margins
  along the long edges. u is the world coordinate along the beam (mod 4096, computed in double),
  so the pattern continues across cells. Fades toward a reduced mean when the lattice nears
  pixel size (the true mean on grazing faces read as flat teal paint, as with the rain).
  Kept fully procedural: no CPU-routed traces were needed.
- Sky lattice: `gridfloor_draw_ceiling` (new ceiling mode, `skygrid.fs`, loaded on first use): a
  plane 90 above the floor, cell 8, 30 % of cell edges lit as thin faint white lines, faded out
  over 100–300, filtered (sub-pixel segments converge to the mean); drawn additively in the
  emissive pass with `gl_FragDepth`, so pillars and beams occlude it.
- **User feedback:** the lattice sits *above* the server towers with space in between, not cutting
  through them. Pillars are now 14–20 high (back rows the same) and beams 24–29, so there is
  always a gap of at least 4 between pillar tops and beam undersides; the outlines' top runs now
  show. Beams stay above the ship's ceiling (16.2 above the floor).
- `screenshots/compare_corridor_d.png` (reference t_5.0 / t_5.25 left, s = 700 / 100 right).
- **Accept:**
  - Tests: 85 passed. 200 chunks: 2265 beam segments, no cell owned twice, every end capped
    exactly when the neighbouring cell on its line has no segment (3112 joins, 1118 caps, 0
    wrong), no along-lane beam above the lane, lowest underside 24 (> 16.2), plan 0.09 ms.
  - Bench 720p speed 120, 30 s: avg 0.30 ms, p99 0.43 ms, 119 draw calls, 15.7k vertices;
    1080p speed 40: avg 0.50 ms, 123 draw calls. Max chunk build 1.2 ms.
  - Regression set byte-identical.

## Phase E — Holo rings, rain, props; Matrix code rain

- **User feedback:** the green dots on the pillars are meant as "The Matrix" digital rain. The
  code rain shader was rewritten: glyphs on a fixed character grid (0.5 × 0.675), each a hashed
  set of strokes on a 3×3 node lattice (reads as katakana-like symbols, never empty), changing
  over time and quickly at the head. Each column (65 % active) carries a stream falling at
  2–6 u/s with a white head glyph and a green trail (length 3–10, fading and darkening toward
  deep green), then waits a random gap; faint residual glyphs (10 %) elsewhere; 15 % mint
  columns. Distance filter: strokes converge to their mean coverage below ~4 px per character
  and the stream to its mean below ~2 px per row. Params renamed to match (`corRainCharWidth`,
  `corRainResidual`, `corRainHead`, `corRainTrail`, `corRainGlyphRate`).
  `screenshots/corridor_matrix_rain.png` (1080p close-up).
- Holo rings: clusters of 2–3 rings (concentric or stacked, each split into 1–3 arcs with gaps,
  per-ring spin, some with pulses) share one basis slot of the existing ring shader, so a chunk
  holds up to 16 clusters. On 12 % of wall pillars (owned with the pillar), radius capped so the
  edge stays outside the lane; and 0–1 per chunk over the lane, but always above the ship's
  ceiling and below the beams, so the play band stays clear. Tilt ±6°.
- Rain: the shared slab streaks, 600 max at density 0.4, white-cyan, 9–26 above the floor (upper
  half), centre gap 9 with 4 % inside.
- Props: 0–2 white wireframe cubes per chunk, 1–7 above the floor beside the lane, only where no
  pillar of this chunk or a neighbour (planned on demand when the prop is within 24 of that chunk
  end) overlaps them; registered as debris for `ct_debris_in_range`. Orange polyhedra not added
  (open question, see the plan §6).
- Draw calls: kerbs, rings and prop faces are skipped in the reflection pass.
- `screenshots/compare_corridor_e.png` (reference t_5.0 / t_7.25 left, s = 700 / 2600 right),
  `corridor_e.png` (s = 100, 700, 1500, 2600).
- **Accept:**
  - Tests: 87 passed. New: over 200 chunks, 101 props all outside the lane and above the floor,
    248 ring clusters all outside the lane or above the ship's ceiling.
  - Bench 720p speed 120, 30 s: avg 0.33 ms, 133 draw calls; 1080p speed 40: avg 0.53 ms, p99
    0.71 ms, 135 draw calls (budget 150), max chunk build 2.06 ms, streaming 2.2 ms per frame.
  - Regression set byte-identical.

## Phase F — Pacing, presets, docs, bench

- Archetypes (same Markov scheme as the city and vortex, `core/pacing.c`; every flight starts in
  a COLONNADE, the reference framing). Each is a `CorArch` of multipliers:

  | Archetype | Run (chunks) | Character | Energy |
  |---|---|---|---|
  | COLONNADE | 4–9 | wall pillars both sides with gaps, sparse beams | 0.50 |
  | SERVER HALL | 3–6 | denser walls (×1.3), merged slabs ×2 (few gaps), outlines ×1.6, beams ×1.9 | 0.85 |
  | ATRIUM | 2–4 | walls thinned (×0.45) and pushed 10 further out, no beams, ring clusters ×3 and 1.4× larger, props ×2 | 0.30 |

  CRYSTAL (orange polyhedra) is not added: left out for now by user decision, to be reconsidered
  together with collision (plan §6).
- Order independence across archetype boundaries: a pillar cell's merge partner and a beam
  cell's neighbours can be owned by the next chunk, so the archetype is not a per-chunk blend
  but a global lookup `arch_at(s)` (previous chunk's archetype fading into this one across the
  chunk, evaluated at each cell's own owner point). `PillarArchFn` / `BeamParams.densityAt` take
  it as a callback. To keep it cheap the build caches the archetypes of the 24 chunks around the
  one being built in one walk of the chain (`pacing_archetypes`). The archetype never brings a
  pillar closer than the base clearance (`clearanceAdd ≥ 0`), never raises pillars (height ≤ 1×),
  and beam density only changes which lines carry beams, so the lane, ceiling and beam-gap
  guarantees hold in every archetype.
- `corPacing = 0` uses the base parameters everywhere; the overlay shows the archetype.
- Debug UI groups: Path, Floor, Pillars, CodeRain, Beams, Rings, Rain, Props, Pacing, Corridor
  (content groups rebuild chunks). 140 corridor params (206 registered of 256 with the core).
- `presets/corridor.ini`: full snapshot (`--env 3 --write-preset corridor`); loading it renders
  byte-identically to the defaults. `INTEGRATION.md`: §10b table row, keep-out notes, §11
  building blocks (`lattice_place`, `pacing`, `trace_route`, gridfloor ceiling mode), §12 params.
- `screenshots/corridor_archetypes.png`: COLONNADE (s = 40), SERVER HALL (900), ATRIUM (1700), seed 1.
- **Accept:**
  - Tests: 90 passed. Pacing over 200 chunks: 42 runs, COLONNADE 104 / SERVER HALL 69 / ATRIUM
    27 chunks, all runs within bounds, starts in a COLONNADE; env-level build order independence
    (200 chunks, shuffled) holds with pacing.
  - 10-minute bench (`--env 3 --bench 600 --speed 120`, 1280×720, autopilot, final s = 72,083):
    avg 0.36 ms, p99 0.48 ms, max frame 6.0 ms; max chunk build 2.36 ms (budget 3); max streaming
    per frame 2.44 ms (< 4); max GL coordinate 1421 (< 2048); GPU meshes 47–73 (bounded);
    134 draw calls (< 150); heaviest chunk 2876 vertices (budget 60k).
  - Regression set byte-identical.

## Environment #3 summary

The Server Corridor is complete per `PLAN_ENV3_SERVER_CORRIDOR.md` (minus the optional CRYSTAL
archetype): walls of stepped server pillars with Matrix digital rain and white stepped outlines
and kerbs over the shared reflective grid floor, a broken lattice of circuit beams above the
pillars with a gap, a sparse sky lattice, holo rings, light rain, props, three pacing
archetypes, `presets/corridor.ini`. Shared pieces extracted for it: `lattice_place`, `pacing`,
`trace_route`, gridfloor ceiling mode. Orange polyhedra are left out for now (revisit
with collision). Open: the corridor-into-vortex ending, tied to the transition design.

# Shared-shader regressions and the greybox (2026-10-20)

## Restoring the sandbox look after DATA // RUN

The game commits changed shared shaders and the city palette for every user, not just the game:

- `ring.fs` read a new `uDetailRange` that only the vortex sets. Left at (0,0) in the corridor,
  `smoothstep(0,0,d)` made every ordinary ring line 0, so the **corridor holo rings were
  invisible**. Distance simplification now applies only when the range is set; otherwise the
  original look (no fade, 0.3 halo).
- `tower.fs` lit windows and brighter base lighting, and `sky.fs` skyline silhouette and moon,
  reached both the city and the corridor. They are now behind `uWindows` / `uSkyline` (0 =
  original), set only by the city with `cityGameLook`.
- The city's muted palette and light tower body are restored to the originals; the game palette
  is `cityGameLook=1` in `presets/mission.ini`.
- **User feedback:** moons, skylines and lit windows read as "urbanization of cyberspace". Keep
  the environments invented and abstract, not real-world archetypes.
- **Accept:** corridor shots at s = 6 and 700 pixel-identical to the pre-game shaders; city sky
  matches the original sky shader. Note: the executable now loads shaders and presets from the
  copies in `build/`, so a shader A/B needs the `build/shaders` file swapped (or a rebuild).

Still changed for the sandbox: the coarse grid overlay in `grid.fs` (city and corridor floors)
and the vortex distance fade / guide rings (`detailNear`, `detailFar`).

## Greybox, step 1 (`env/env_greybox.c`, `CT_ENV_GREYBOX` = 4)

Purpose: tune ship movement against measurable references, without bloom, streaks and glyph rain
hiding speed and drift. Not a game; later steps only if this raises questions.

- Path style defaults equal the vortex "Path" group (own `gb*` names, since only the active
  environment's params are registered), so `--seed N` flies the vortex course.
- Content: grey rings every `gbRingSpacing` (10 m) on the tunnel radius, brighter and wider at
  50 m and 100 m; `gbRails` (8) lengthwise rails offset from the axes; an amber circle every
  50 m at `steerLimit` × radius, the flight model's reach (read from the registry at build time,
  so after changing `steerLimit` press R to rebuild).
- `shipDart` (Sim): the original placeholder dart and its shader (`ship_dart.fs`) and engine
  positions instead of the courier. Off by default; `presets/greybox.ini` turns it on, sets
  intensity 0.3 and removes zoom blur, chromatic aberration, grain and vignette, bloom 0.35.
  Flight keys stay at defaults (the original tuning). The courier's flight tuning is in
  `presets/mission_tunnel.ini` (steer accel 48 / damping 7, bank 25°, cam 7.25 / 2.45,
  FOV boost 3, shake 0.008).
- Run: `./build/cybertunnel --sandbox --env 4 --preset greybox`.
- **Accept:** tests 99 passed (new: style equals vortex defaults; order-independent build;
  96 rings + 20 envelope circles + rails over 960 m, no boundary duplicates). Shots at s = 300
  cruise / boost / vortex at the same seed: same course.

# Environment #4 — Crystal Field

Plan: `PLAN_ENV4_CRYSTAL_FIELD.md`. Targets: `reference/concepts/env4-crystal-field_*.png`, and
`reference/t_4.0s.png` / `t_4.25s.png`. Registry id `ENV_CRYSTAL = 5` (id 4 is the greybox).

## Phase A — Stub (2026-10-21)

- `env/env_crystal.c`: open path with no floor, ceiling, walls or twist; lane radius 12,
  yaw step 8°, pitch 8°. Background, warm vanishing glow and sparse white streaks (cylinder,
  centre clear). Empty chunks. Params in "Crystal Path", "Crystal" and "Crystal Streaks", all `cf*`.
- `background.fs` gets `uGlowFocus` (0 = the vortex's wide glow with a floor everywhere, 1 = a tight
  halo plus a hot centre). The orange glow with the old floor turned the whole sky brown. The vortex
  sets 0 every frame (shared-program rule).
- Run: `./build/cybertunnel --sandbox --env 5`.
- **Accept:** tests 105 passed (new: open style without twist; pitch ≤ 8.2° and roll 0 over
  5 seeds × 20 000 units; order-independent build). Vortex, city, corridor and greybox shots at
  seed 7, 4 s are byte-identical to before.

## Phase B — Polyhedra and placement, edges only (2026-10-21)

- `render/shapes.c`: `shape_solid()` builds any convex `ShapeSolid` at a position with three
  basis axes (rotation × per-axis scale). The existing cube/octa/sphere functions are untouched.
- `env/crystal_field.c/.h` (pure CPU):
  - Solids: tetrahedron, octahedron, icosahedron, cube, triangular prism, hexagonal bipyramid. Vertices
    only; edges and triangles come from a brute-force convex hull (coplanar vertices form one polygon, so
    cube and prism faces have no diagonals). Circumradius 1.
  - Placement: each chunk owns the clusters and single shapes anchored in its `[s0, s1)`. Clusters sit
    75 % left/right (±35°), the rest anywhere around, at `keep-out .. cfBandMax` with a near bias.
    Sizes: 3 % giants (8–16), 35 % medium (2–6), the rest small (0.5–2). Bipyramids are stretched
    1.6–2.6×, 15 % of the others on one axis. Random orientation. Colours: 70 % orange, 25 % amber, 5 % pale gold.
  - Keep-out: √2 × `steerLimit` × lane radius + `cfClearance` (16.6 by default), against the path
    around the shape, not only its anchor. Shapes that fail are pushed away from the nearest path point.
    `steerLimit` is read from the registry, as the greybox does (press R after changing it).
- `pathReach` covers the largest band; "Crystal Field" edits rebuild chunks, "Crystal Path" edits
  rebuild the path.
- `cfEdgeIntensity` 1.5: at 2.5 the white core of `line_facing.fs` and bloom turned the edges peach-white.
- **Accept:** tests 111 passed (new: solid edge counts and Euler characteristic; 3 seeds × 80 chunks,
  29 shapes per chunk, worst margin to the keep-out +0.02, checked exactly against the path segments;
  build 0.14 ms, 2.6k vertices per chunk).
- Note: `--shot N` jumps to arc length N, not N seconds. Shots: `shots/env4/b_crystal_300*.png`, `_700`.
- **Open for Phase C:** far shapes crowd the vanishing point where the path curves; the concept keeps the
  middle clearer. Edges are still paler than the concept's saturated orange.
