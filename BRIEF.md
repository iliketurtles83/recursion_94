# Cyberspace Tunnel Sandbox — Implementation Brief (raylib)

## 0. Mission

Build a **standalone sandbox** that renders an endless, procedurally generated "cyberspace" flight environment in raylib, which will later be integrated into the main game. The player's ship flies forward forever through generated geometry; the look target is the reference video described in §2.

Long term there will be **2–3 distinct environments** the player flies through. This task builds:

1. The **shared core** (path, chunk streaming, flight/camera, HDR render pipeline, debug tooling) designed so additional environments plug in without touching the core.
2. **Environment #1: "Data Vortex"** — a curving tunnel of segmented neon rings, spiral light ribbons, speed streaks, tumbling wireframe cubes and a blazing core at the vanishing point.

Environments #2 and #3 are **not** built now (see §9), but every architectural decision must leave room for them.

There is **no size constraint** (the old 1.44 MB limit is gone). Shaders, textures, generated meshes and external assets are allowed. Still prefer procedural generation; it is the point of the exercise.

---

## 1. Ground rules

1. Work only inside `sandbox/cybertunnel/` with its own build target. The sandbox must build and run without the main game.
2. **Do not reuse, port or study the main game's existing tunnel/flight/rendering code.** This is a clean-room design. You may look at the main project only to match: language/binding, raylib version, compiler and build system, and coding conventions. If the main project is not C, translate the C-flavoured specs below idiomatically.
3. Default stack if nothing dictates otherwise: C11, raylib 5.5 (`raylib.h`, `rlgl.h`, `raymath.h`), `raygui.h` for debug UI. Pin the raylib version in the build.
4. No global mutable state for environment/core data: everything lives in structs passed explicitly. Never use `GetRandomValue()`, `rand()` or `SetRandomSeed()` for generation (global state breaks determinism). Use the RNG in §3.3.
5. Work phase by phase (§4). Each phase ends with acceptance checks; do not start the next phase while checks fail. Commit at least once per phase.
6. When unsure of a raylib/rlgl API (function names, uniform types, framebuffer helpers), **read the pinned version's headers** rather than guessing. If reality contradicts this brief, choose the working solution and log the deviation in `NOTES.md`.
7. After each phase, write a short report in `NOTES.md`: what was done, screenshots taken (paths), metrics measured, deviations, open questions.

---

## 2. Visual reference

Reference video: 10 s, 1280×720, third-person chase camera behind a small ship. Keyframes are provided in `reference/` (contact sheet + full-res frames named by timestamp). Copy them into `sandbox/cybertunnel/reference/` and compare against them at every visual phase.

> Repo note: the video and all extracted keyframes now live in `sandbox/cybertunnel/reference/` (`video.mp4`, `t_*.png`, `contact_sheet.png`). Pull further frames with e.g. `ffmpeg -ss 7.4 -i reference/video.mp4 -frames:v 1 reference/t_7.4s.png`.

### 2.1 Timeline (for context; only the last part is Environment #1)

| Time | Content | Becomes |
|---|---|---|
| 0.0–3.5 s | Low flight over an infinite cyan neon grid floor; tall dark monolith towers covered in purple/magenta circuit traces and pink glyph-text lines; streaking light rain | Env #2 (future) |
| 3.5–4.5 s | Barrel roll through a field of orange wireframe polyhedra (icosahedra, octahedra, prisms) | Env #3 (future) |
| 4.5–7.5 s | Corridor of server monoliths with green "code rain", overhead circuit-lattice ceiling, thick white outline glows on pillar edges, holographic rings | Env #3 (future) |
| **7.5–10 s** | **Vortex tunnel: concentric segmented rings, swirling purple/blue ribbons, white dash particles streaming outward, wireframe cubes tumbling, blinding core, ends in whiteout** | **Env #1 — build this** |

### 2.2 Environment #1 visual breakdown (frames t_8.2 – t_9.9)

- **Segmented rings** form the tunnel walls. Each ring is several concentric layers of short arc segments with gaps. Many segments have a stepped, castellated, circuit-trace profile (radius jumps in/out at right angles). Radial tick marks and short spokes connect layers. Rings recede in dense perspective toward the core, so the far end reads as a bright, intricate mandala.
- **Spiral ribbons**: soft, wispy, translucent bands in purple and blue sweep helically around the outside of the tunnel. Low intensity, additive, slightly blurred.
- **Speed streaks**: hundreds of short white/cyan dashes streaming past, appearing to radiate out from the vanishing point; some longer comet streaks with bright heads.
- **Wireframe cubes**: medium-sized cubes tumbling slowly inside the tunnel, bright edges, faint translucent faces with a dashed "data" pattern.
- **Holographic rings**: thin elliptical rings floating near the walls at odd angles.
- **Core**: an intense white-cyan light at the vanishing point with a soft halo, a horizontal anamorphic streak and faint rays. Distant geometry fades *into* this glow rather than into darkness.
- **Ship**: small dark dart with twin engine trails (cyan core, pink/magenta edge).
- **Post**: heavy bloom, radial zoom blur stronger toward the edges, mild chromatic aberration, dark vignette, final whiteout.

### 2.3 Palette (linear HDR targets; tune by eye)

| Role | Hex (sRGB) | HDR intensity |
|---|---|---|
| Background | `#03060C` | 1.0 |
| Primary neon cyan (rings, grid, edges) | `#4FEAF5` | 1.5–4.0 |
| Core / hot white | `#E8FBFF` | 8–20 |
| Ribbon purple | `#7A5CFF` | 0.3–0.8 |
| Ribbon blue | `#3A7BFF` | 0.3–0.8 |
| Accent magenta (engine trail edges) | `#FF4FD8` | 1.0–2.5 |
| Env #3 orange (future) | `#FF9A3C` | — |
| Env #3 matrix green (future) | `#39FF6A` | — |

---

## 3. Architecture

### 3.1 Directory layout

```
sandbox/cybertunnel/
  CMakeLists.txt (or match main project's build system)
  NOTES.md            phase reports and deviations
  INTEGRATION.md      written in Phase 13
  reference/          video keyframes
  presets/            saved parameter sets (*.ini)
  src/
    main.c            harness only: window, input, CLI, calls into the module
    cybertunnel.h     public API (Phase 13)
    core/
      rng.h           splitmix64, pcg32, hash helpers
      noise.h/.c      1D/2D value noise, fbm
      path.h/.c       procedural centerline + frames
      chunks.h/.c     chunk streaming, floating origin
      flight.h/.c     ship motion, chase camera
      params.h/.c     parameter registry, ini load/save
    render/
      pipeline.h/.c   HDR target, bloom, composite/post
      linegeo.h/.c    neon line mesh builder
      shaderlib.h/.c  load + hot-reload shaders
      shaders/*.vs, *.fs
    env/
      env.h           Environment interface
      env_vortex.c    Environment #1
      env_template.c  empty skeleton for future environments (Phase 13)
    debug/
      debug_ui.c      raygui panel + overlay
  tests/
    test_main.c       determinism and continuity tests (no window)
```

### 3.2 Environment interface

The core owns path, chunk lifetime, ship, camera and post-processing. An environment only produces content for chunks, draws it, and declares how the shared path should behave inside it.

```c
typedef struct FrameContext {
    double   time;          // seconds since start
    float    dt;
    double   sShip;         // ship arc-length position on path
    float    speed;         // units/s
    float    intensity;     // 0..1 global "energy", driven by pacing or later by gameplay/music
    Camera3D camera;        // in render space (origin-rebased)
    Vector3  renderOrigin;  // double-precision world origin offset, stored as needed
    Matrix   view, proj;
} FrameContext;

typedef struct PathStyle {
    float baseRadius, radiusNoiseAmp, radiusNoiseScale;
    float maxYawStepDeg, maxPitchDeg, pitchSpring;
    float twistRateMax;      // roll rad/unit
    bool  hasWalls;          // false for open environments (future grid city)
} PathStyle;

typedef struct Environment {
    const char *name;
    int         id;
    void       *state;
    PathStyle (*pathStyle)(const struct Environment*);
    void (*init)(struct Environment*, uint64_t worldSeed);
    // Pure CPU, deterministic: output depends only on (seed, id, chunkIndex, path). No GL calls.
    void (*buildChunk)(struct Environment*, const PathSystem*, int chunkIndex, ChunkCpu *out);
    void (*uploadChunk)(struct Environment*, ChunkCpu *in, ChunkGpu *out);   // GL upload only
    void (*releaseChunk)(struct Environment*, ChunkGpu*);
    void (*update)(struct Environment*, const FrameContext*);
    void (*drawOpaque)(struct Environment*, const FrameContext*, const ChunkGpu *chunks, int n);
    void (*drawEmissive)(struct Environment*, const FrameContext*, const ChunkGpu *chunks, int n);
    void (*drawDebugGui)(struct Environment*);
    void (*unload)(struct Environment*);
} Environment;
```

Treat this as a starting shape, not gospel. Keep the separation it encodes: build (pure, threadable later) → upload (GL) → draw.

### 3.3 Determinism

- World seed is `uint64_t`. Per-chunk RNG seed = `hash64(worldSeed, envId, chunkIndex)` using splitmix64 mixing; generator is pcg32.
- Inside a chunk, derive sub-streams per feature (`hash64(chunkSeed, FEATURE_RINGS)`, etc.) so adding a feature doesn't reshuffle other features.
- Same seed ⇒ bit-identical chunk vertex data regardless of build order or frame timing.

### 3.4 Coordinate spaces and precision

- Path arc length `s` and all world positions in the path generator are **double**.
- Each chunk stores `origin` (double3, path position at chunk start). Its meshes are in **chunk-local float** coordinates.
- Rendering uses a **floating origin**: `renderOrigin` (double3) snaps to the ship's position whenever the ship is > 1024 units from it. Chunk model matrix = translate(chunk.origin − renderOrigin). Camera and ship are expressed relative to `renderOrigin`.
- Any value fed to shaders as "time-like" coordinates (UV along `s`, etc.) is wrapped (e.g. `fmod(s, 1024.0)`) before conversion to float.

### 3.5 Frame loop

- Simulation at fixed 120 Hz with an accumulator; render once per frame with interpolation of ship/camera state.
- Order per frame: input → fixed sim steps → chunk streaming (budgeted) → env update → render (opaque → emissive → post) → debug UI.
- Zero heap allocations per frame in steady state (pre-size buffers; allocations happen only on chunk build/upload).

---

## 4. Phases

### Phase 0 — Scaffold

1. Create the directory layout, build target, pinned raylib (+ raygui header).
2. Window 1280×720, resizable, vsync toggle, MSAA off (we AA in shaders; see §7).
3. Fixed-timestep loop skeleton, FPS/frame-time overlay.
4. CLI parsing: `--seed N`, `--w W --h H`, `--vsync 0|1`, `--bench SECONDS`, `--shot S` (stubs for now).
5. Separate `cybertunnel_tests` executable that runs without a window.

**Accept:** builds clean with warnings enabled (`-Wall -Wextra`), opens a window, tests binary runs and returns 0.

### Phase 1 — Procedural path

1. **Control points** every `L = 40` units, generated sequentially from `s = 0`:
   - State: yaw, pitch (radians), position (double3).
   - Turn rate comes from smooth 1D value noise sampled at control index (so turns persist over several points rather than jittering): `yaw += clamp(noise(i*0.15)*maxYawStep, ±maxYawStep)`.
   - Pitch: `pitch += noiseB(i*0.15)*k − pitchSpring*pitch`, clamped to ±maxPitch, so the path never drifts vertical.
   - `P[i+1] = P[i] + L * dir(yaw, pitch)`.
2. **Spline:** centripetal Catmull-Rom (α = 0.5) through control points.
3. **Arc-length table:** subdivide each segment densely (≥ 64 subsamples), accumulate length, then resample at uniform Δs = 0.5 units.
4. **Frames:** rotation-minimizing frames via the double-reflection method (Wang et al. 2008). Do **not** use Frenet frames (they flip at inflection points). Initial normal = world up projected onto the plane ⟂ T.
5. **Twist:** `roll(s)` = integral of a noise-driven twist rate in ±`twistRateMax`; applied as rotation of (N, B) around T. Store separately so the camera can follow it partially.
6. **Radius:** `R(s) = baseRadius * (1 + radiusNoiseAmp * noise(s / radiusNoiseScale))`.
7. **Storage:** ring buffer of samples covering `[sShip − 100, sShip + viewDist + 100]`, extended incrementally as the ship advances. Jumping to a distant `s` (debug) regenerates from 0 — acceptable.
8. **API:** `PathSample path_sample(const PathSystem*, double s)` → position (double3), T, N, B (after roll), radius, roll, curvature. Interpolate between table entries (lerp position, nlerp frame vectors then re-orthonormalize).
9. **Debug draw (toggle):** centerline, RGB frame axes every 5 units, radius circles every 10 units.
10. Temporary on-rails camera following the path so the curve can be inspected.

**Accept (tests):** same seed ⇒ identical `path_sample(5000.0)` within 1e-9; `dot(N_k, N_{k+1}) > 0.99` and `dot(T_k, T_{k+1}) > 0.99` across 20,000 units; max curvature ≤ the configured limit; position and tangent continuous across control segments (C1). Visual: a smooth, gently winding path with no snapping.

### Phase 2 — Chunk streaming and floating origin

1. Chunk length `C = 48` units; chunk `k` covers `s ∈ [kC, (k+1)C)`.
2. Active window: chunks from `floor((sShip − 24)/C)` to `floor((sShip + viewDist)/C)`, `viewDist = 400`.
3. States: `EMPTY → BUILT (CPU) → UPLOADED (GPU, visible) → RELEASED`. Fixed-size chunk pool, no per-frame allocation.
4. Budget: at most 1 build and 1 upload per frame, nearest-first. Pre-warm the whole window before the first frame.
5. Pop-in mitigation: per-chunk fade-in (0 → 1 over 0.3 s) passed as a uniform, plus distance fade in shaders (far geometry fades into core glow, see Phase 9).
6. Implement floating origin as in §3.4.
7. Keep `buildChunk` pure so it can move to a worker thread later. Do not thread now.

**Accept:** 10-minute `--bench` at 3× speed: no frame hitch > 4 ms attributable to streaming; GPU buffer count stable; at `s > 50,000` geometry shows no jitter; log max absolute float coordinate passed to GL stays < 2048. Chunk build determinism test: hash of chunk `k` vertex data identical whether built in order or out of order.

### Phase 3 — HDR render pipeline (do this before content so every visual is judged through bloom)

1. **Scene target:** RGBA16F color + depth. raylib's `LoadRenderTexture` is RGBA8, so build it via rlgl (`rlLoadFramebuffer`, `rlLoadTexture(NULL, w, h, PIXELFORMAT_UNCOMPRESSED_R16G16B16A16, 1)`, `rlLoadTextureDepth`, `rlFramebufferAttach`, `rlFramebufferComplete`) — verify exact signatures in the pinned `rlgl.h`. Provide an RGBA8 fallback flag.
2. **Draw order:** opaque/dark geometry (depth write on) → emissive geometry (`BLEND_ADDITIVE`, depth test on, depth write **off**) → core/flares.
3. **Bloom:** prefilter with soft-knee threshold (≈1.0 in HDR) at half res → dual-Kawase downsample ×5–6 → upsample with additive combine. Expose threshold, knee, intensity, radius, mip count.
4. **Composite shader** (single pass, in this order):
   - scene + bloom × intensity
   - radial zoom blur toward the vanishing point (8–12 taps), strength ∝ normalized speed, masked to be ~0 in the center and strong at the edges
   - radial chromatic aberration (0–3 px at edges)
   - ACES-fitted tonemap, exposure control
   - vignette, subtle animated film grain
   - `whiteout` uniform (0..1) mixing to white (for env transitions)
   - output to sRGB
5. Vanishing point in screen space = projection of `path_sample(sShip + 60).pos`; smooth it over time.
6. Window resize recreates targets.
7. **Shader hot reload:** poll file mtimes every 0.5 s; on compile failure keep the old shader and print the log.
8. Test scene: a handful of emissive lines and a bright sprite to tune bloom.

**Accept:** soft, wide bloom without blockiness or banding at 1080p; each post stage toggleable from debug UI; CPU-side timing per pass shown in the overlay.

### Phase 4 — Neon line geometry library

The whole look is glowing lines. Do not use `DrawLine3D` (1 px, immediate mode) for content.

1. CPU builder `LineGeo` that appends into growable vertex/index arrays, then splits into raylib `Mesh`es. **raylib `Mesh.indices` is 16-bit: cap each mesh at 65,535 vertices.**
2. Two primitive kinds:
   - **Planar strip:** a quad of given world width lying in a given plane (used for ring arcs). Arcs tessellated every ~3° (or adaptively).
   - **Camera-facing segment:** each vertex stores both endpoints and a side sign; the vertex shader expands perpendicular to the view direction (used for cube edges, holo rings, streaks). Width in world units with an optional minimum pixel width.
3. Vertex attribute packing (raylib's fixed set): position, texcoord (u = along line, v = −1..1 across), texcoord2 (per-object id / animation params), color (tint), normal/tangent (other endpoint or extra params as needed). Document the packing in `linegeo.h`.
4. Fragment profile: `I = core * (1 − smoothstep(0, coreW, |v|)) + halo * exp(−k v²)`, antialiased with `fwidth`. Output `tint * I * hdrIntensity`. This gives a crisp core plus soft halo before bloom.
5. Global uniforms shared by all emissive shaders: time, renderOrigin-relative camera pos, fog/fade params, core color, chunk fade.

**Accept:** test scene with 10,000 segments draws smoothly at 60 fps; lines look clean (no shimmer, no stair-stepping) from 1 to 400 units away; no z-fighting.

### Phase 5 — Environment #1: segmented rings (the hero element)

1. **Placement:** rings every 5–10 units (jittered). Occasionally a "gate" cluster of 3–5 rings spaced 1.5 units apart. Ring plane ⟂ T at its `s`, oriented by the path frame including roll.
2. **Layers:** 2–4 concentric layers per ring at radii in `[0.95R, 1.4R]`, so walls sit outside the flight corridor.
3. **Segments per layer:** 3–12 arcs, gap fraction 10–40%, random phase.
4. **Segment styles** (weighted random per layer):
   - `BAND`: solid strip, width 0.15–0.5.
   - `CASTELLATED`: arc split into cells of 4–10°; each cell's radius is `r` or `r + step` (step 0.3–0.8); radial edges at transitions. This produces the stepped circuit-trace look (see t_9.9).
   - `TICKS`: comb of radial ticks, length 0.3–1.2, every 2–5°.
   - `DASHED`: short dashes along the arc.
   - `DOUBLE`: two thin parallel bands.
5. **Spokes and pads:** 0–6 radial connectors per ring between layers, with small square "pads" at their ends.
6. **Animation without CPU rebuilds:** store ring vertices in ring-local space (xy = cross-section plane, z = axial). Per-vertex: ring index within chunk and layer angular velocity (±0–0.4 rad/s, alternating sign by layer). Per chunk, upload uniform arrays `ringOrigin[16]`, `ringRight[16]`, `ringUp[16]` as `vec4` (avoid matrix uniform arrays). The vertex shader rotates xy by `angVel * time`, then places the vertex using the ring basis. Max 16 rings per chunk; enforce in generation.
7. **Data pulses:** fragment shader moves bright pulses along arcs (`fract(u * freq − time * pulseSpeed)` with a hash per segment), sparse and subtle.
8. **Near fade:** fade emissive geometry within ~2 units of the camera to avoid flashes as rings pass.

**Accept:** screenshot side by side with `reference/t_9.4s.png` and `t_9.9s.png`; the far tunnel reads as a dense, intricate ring mandala; rings rotate smoothly; zero per-frame CPU work for ring animation.

### Phase 6 — Speed streaks (GPU-only particles)

1. One static mesh of N = 6,000 quads; each vertex carries a per-streak seed and corner id. No CPU updates.
2. Vertex shader derives per-streak: angle θ, radius `r ∈ [0.2R, 1.2R]` (biased outward), phase z0, speed multiplier m ∈ [0.6, 1.6], width 0.03–0.08.
3. Position in a camera-aligned cylinder: uniforms give origin (ship), axis (path tangent at ship), right/up. `z = mod(z0 − time·speed·m, zRange) − zBehind`.
4. Stretch along the axis with length ∝ `speed · m · 0.06`; fade at both ends of the range (no visible wrap pop).
5. Color mix: ~85% white-cyan, ~15% purple. ~1% "comets": longer, with a bright head.
6. A subset hugs the wall and follows the ribbon helix (`θ += helixRate · z`) to tie streaks to ribbons.

**Accept:** at base speed streaks read as motion; at boost they read as warp speed; no pops; cost negligible.

### Phase 7 — Spiral ribbons

1. 2–4 helical ribbons at radius `1.05–1.3R`, width 2–5 units. Angle = `θ0 + ω·s` with ω ∈ [0.03, 0.08] rad/unit; alternate direction per ribbon. Because angle depends on global `s`, chunks join seamlessly.
2. Built per chunk following path frames; u = wrapped global `s`, v = −1..1 across.
3. Fragment: fbm noise scrolled along u and time → wispy bands; `smoothstep` soft edges on v; fine streaks via high-frequency anisotropic noise; color lerps purple ↔ blue; intensity 0.3–0.8; additive.

**Accept:** no seams at chunk boundaries; ribbons stay subtle behind the rings (compare t_8.8s).

### Phase 8 — Floating debris (wireframe cubes)

1. 0–6 objects per chunk: mostly cubes (size 0.6–2.5), occasional octahedra.
2. Placement in polar coordinates `r ∈ [0.45R, 0.95R]` with a keep-out radius of `0.35R` around the centerline (clear flight lane for now; gameplay obstacles later).
3. Tumble in the vertex shader: per-object center, rotation axis and speed (0.2–1.2 rad/s) packed into attributes.
4. Edges: camera-facing lines from Phase 4, width 0.04–0.08, HDR 2–4.
5. Faces: faint additive fill with a procedural "data" pattern (grid of small rectangles toggled by hash, slow flicker), low intensity.
6. Expose a list of debris (center, radius, `s`) per chunk through a query for future collision.

**Accept:** compare with t_8.8s; cubes tumble smoothly and never intersect the flight lane.

### Phase 9 — Core, fog and atmosphere

1. **Core:** camera-facing billboard at `path_sample(sShip + 0.9·viewDist)`; radial exponential falloff, HDR 8–20; horizontal anamorphic streak (≈30:1 stretched quad); 4–6 faint slowly rotating rays. Brightness scales with how centered the core is on screen.
2. **Glow fog:** emissive geometry blends toward `coreTint` with distance and then fades, so the far tunnel brightens toward the center instead of going black. Params: `fogStart`, `fogEnd`, `fogToCore`.
3. **Holographic rings:** thin, tilted elliptical rings near the walls, slowly rotating, low density (see t_8.2s top).
4. **Background:** dark navy radial gradient plus sparse static dust points.
5. **Whiteout test:** key `W` (debug) animates `whiteout` 0 → 1 → 0 with a core flare spike, as at the end of the reference.

**Accept:** side-by-side with t_9.4s and t_9.9s; the core is the visual anchor; no hard far-plane cutoff visible.

### Phase 10 — Ship, flight model, camera feel

1. **Placeholder ship:** procedural low-poly dart (dark hull, cyan edge lines via Phase 4, two engine glow sprites).
2. **Engine trails:** ribbons from a ring buffer of the last ~0.6 s of engine positions (sampled at sim rate), tapered width, cyan core with magenta edge.
3. **Flight model:** ship advances along `s` at speed v (base 40 u/s, boost 80, exponential smoothing). Player steers an offset (x, y) in the tunnel cross-section, limited to `0.55·R(s)`, with damping. World pos = `path.pos + N·x + B·y`. Orientation: forward = T; bank roll ∝ lateral velocity (max 35°); pitch ∝ vertical velocity.
4. **Chase camera:** desired position = ship − T·6 + up·1.6; critically damped spring (frame-rate independent, `exp(−k·dt)` style) on position and up vector; look-at = blend of ship and `path_sample(s + 25)`. FOV = 70° + 15°·normalized boost; tiny noise-based shake at boost. Camera roll = blend of tunnel roll and ship bank (`cameraRollFollow` param).
5. **Barrel roll** on Q/E (reference t_3.8s) to test camera feel.
6. **Debug free camera** (F2) independent of the ship; sim keeps running or pauses (toggle).
7. Write camera math yourself; do not rely on `UpdateCamera` modes for the chase cam.

**Accept:** steering feels responsive and smooth at 30, 60 and 144 fps (results equivalent across frame rates); no camera jitter at chunk boundaries or origin rebases.

### Phase 11 — Pacing and variation

1. **Chunk archetypes** for this environment: `CALM` (sparse rings, ribbons dominant), `GATES` (ring clusters), `DENSE` (rings every ~3 units, more debris), `CORRIDOR` (near-continuous castellated walls).
2. Sequence from a deterministic, hash-driven Markov chain with min/max run lengths so intensity "breathes" every ~10–20 s of flight.
3. Parameters blend across one chunk at archetype transitions; no abrupt changes.
4. Global `intensity` (0..1) scales ring brightness, streak speed/density, zoom blur. Expose a setter for later gameplay/music control.

**Accept:** a 3-minute flight feels varied without ever looking like a different environment.

### Phase 12 — Debug tooling, presets, benchmarking

1. **raygui panel (F1):** seed (R = reseed), speed, time scale, pause (P), jump to `s`; grouped sliders for path, rings, streaks, ribbons, debris, core/fog, bloom/post; per-layer toggles (keys 1–9).
2. **Overlay:** FPS, frame ms, per-pass CPU ms, active chunks, builds/uploads this frame, vertex count, draw calls (count them yourself), `sShip`, `renderOrigin`.
3. **Parameter registry:** every tunable registered once with name, default, range; drives both the UI and ini save/load (`presets/*.ini`, simple `key=value`).
4. **F12 screenshot** named `vortex_seed{seed}_s{s}.png`.
5. **CLI:** `--bench SECONDS` (auto-fly at fixed speed and seed, print avg/p50/p99 frame time and max chunk build ms, exit); `--shot S` (fly to `s = S`, warm up 2 s, screenshot, exit) for visual regression.
6. **Tests:** RNG determinism, path determinism and continuity, chunk build determinism (order-independent hash), parameter ini round-trip.

**Accept:** all tests pass; bench numbers recorded in `NOTES.md`.

### Phase 13 — Integration preparation

1. **Public API** `cybertunnel.h`: create/destroy with config; `set_environment(id)`; `update(ctx)`; `render(target, ...)`; queries: `path_sample`, `tunnel_radius_at`, debris in an `s` range, vanishing point. Config chooses whether the module owns the camera or the game supplies it, and whether the module's post pipeline is used or bypassed (game may have its own).
2. The harness `main.c` must use only the public API.
3. `env_template.c`: a minimal skeleton environment (empty chunks, simple path style) showing exactly what a new environment implements.
4. `INTEGRATION.md`: init order, GL resource ownership, per-frame calls, how to plug into the game's render target, how to disable internal post, how to add an environment step by step, full parameter list with defaults.

**Accept:** the template environment can be switched to at runtime and runs without crashing; a reader can integrate from `INTEGRATION.md` alone.

---

## 5. Default parameters (starting points, tune by eye)

| Param | Default |
|---|---|
| baseRadius | 12 |
| radiusNoiseAmp / scale | 0.12 / 60 |
| control point spacing L | 40 |
| maxYawStepDeg | 14 |
| maxPitchDeg / pitchSpring | 20 / 0.15 |
| twistRateMax | 0.015 rad/unit |
| chunk length C | 48 |
| viewDist | 400 |
| ring spacing | 5–10 |
| ring layers | 2–4 |
| ring layer radii | 0.95R–1.4R |
| streak count | 6000 |
| ribbons | 3 |
| debris per chunk | 0–6 |
| ship speed base / boost | 40 / 80 u/s |
| steer limit | 0.55R |
| camera distance / height | 6 / 1.6 |
| FOV base / boost add | 70° / 15° |
| bloom threshold / intensity | 1.0 / 0.8 |
| bloom mips | 6 |

---

## 6. Performance budget

- 1920×1080 at 60 fps on a mid-range discrete GPU (GTX 1660 class) with all layers on.
- 1280×720 at 60 fps on integrated graphics (Iris Xe class) with bloom starting at quarter resolution.
- CPU (sim + streaming + draw submission) < 2 ms average; single chunk build < 3 ms worst case.
- < 150 draw calls per frame; zero steady-state per-frame heap allocations.

---

## 7. Known pitfalls checklist

- raylib `Mesh.indices` is `unsigned short` → ≤ 65,535 vertices per mesh.
- `DrawLine3D` / `DrawCubeWires` are 1 px immediate mode; not for content.
- `FLAG_MSAA_4X_HINT` does not apply to custom framebuffers; antialias lines in shaders (`fwidth`) instead.
- `BeginMode3D` uses raylib's default near/far planes (far is 1000 by default); if view distance changes, check `rlSetClipPlanes` (or equivalent) in the pinned version.
- Verify raylib's default attribute names (`vertexPosition`, `vertexTexCoord`, `vertexTexCoord2`, `vertexColor`, `vertexNormal`, `vertexTangent`) and bind custom uniform locations explicitly.
- Additive emissive pass: depth test on, depth write off; draw opaque first.
- Use rotation-minimizing frames, not Frenet.
- No global RNG; no time baked into vertex buffers (pass time as a uniform).
- Frame-rate independent smoothing everywhere (`1 − exp(−k·dt)`).
- Wrap large scalars (`s`, time) before converting to float for shaders.
- Uniform arrays: prefer `vec4` arrays; check the GL uniform limits if counts grow.

---

## 8. Deliverables

1. `sandbox/cybertunnel/` building and running standalone.
2. Environment #1 "Data Vortex" matching the reference look and feel.
3. Shared core (path, chunks, flight/camera, render pipeline, params, debug UI) environment-agnostic.
4. `cybertunnel_tests` passing.
5. `NOTES.md` with per-phase reports, screenshots, bench results, deviations.
6. `INTEGRATION.md`, `cybertunnel.h`, `env_template.c`.
7. At least 3 saved presets (e.g. `default.ini`, `calm.ini`, `intense.ini`).

---

## 9. Future environments (do not build; design for them)

- **Env #2 "Neon Grid City"** (reference 0–3.5 s): open space, no tunnel walls. Infinite cyan grid floor, dark monolith towers with purple/magenta circuit traces and pink glyph text, light-streak rain. Needs `PathStyle.hasWalls = false`, low altitude, gentle curvature, a ground plane.
- **Env #3 "Server Canyon / Crystal Field"** (reference 3.5–7.5 s): orange wireframe polyhedra clusters; server pillars with green code rain; circuit-lattice ceiling; thick white edge outlines; holographic rings.
- **Transitions** between environments: portal/whiteout using the `whiteout` uniform and core flare, with the path style blending over a transition zone. The core must allow two environments' chunks to coexist during a transition window.

If any core decision would make these hard (e.g. assuming walls exist, assuming a cylindrical cross-section in the camera or streak systems), make it a `PathStyle` or environment parameter instead.
