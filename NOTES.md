# Cybertunnel sandbox — notes

Machine used for all numbers below: AMD Ryzen AI MAX+ 395, Radeon 8060S (integrated,
Strix Halo), Linux (Wayland), Mesa. raylib 5.5 / raygui 4.0 pinned via FetchContent.

Main project: this repository contains no main game yet, so the brief's default stack
(C11, CMake, raylib 5.5) was used as-is. No game code was looked at.

Commit history: the phases were implemented in one working session and committed in
groups (scaffold+core, render pipeline, environment, tooling+docs) rather than strictly one
commit per phase. Each phase's acceptance checks are recorded below.

Debug renders live in `shots/` (git-ignored). Reproduce any of them with
`./build/cybertunnel --shot S [--preset NAME] [--out FILE]`.

## Build / run

```
cmake -S . -B build && cmake --build build -j
./build/cybertunnel_tests
./build/cybertunnel [--seed N] [--w W --h H] [--vsync 0|1] [--fps N] [--env 0|1]
                    [--bench SECONDS [--speed U]] [--shot S [--out FILE]] [--preset NAME]
                    [--write-preset NAME] [--list-params] [--hdr8] [--noship] [--ui] [--whiteout]
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
