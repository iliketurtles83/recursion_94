# Plan — Environment #2 "Neon Grid City"

Status: Phases A and B done (see `NOTES.md`); Phases C–F open. Reference frames: `reference/t_0.5s.png`,
`reference/t_2.5s.png` and the first row and a half of `reference/contact_sheet.png` (0–3.5 s).

## 0. Why this one

The first 7.5 s of the reference contain three looks:

| Time | Look | Verdict |
|---|---|---|
| 0.0–3.5 s | Low flight over an infinite cyan grid floor, dark monolith towers with purple circuit traces and pink "code" text, cyan light-streak rain | **Build this** |
| 3.5–4.5 s | Barrel roll through orange wireframe polyhedra | Only about 1 s on screen. It works better as a set piece or transition than as a whole environment |
| 4.5–7.5 s | Server-pillar corridor: green code rain, circuit-lattice ceiling, white outlines | Good candidate for Env #3. It has the **same grid floor**, so it can reuse what we build here |

Why the Grid City:

1. It is the opening shot of the video and the look people remember. It is also very different from the vortex: open space instead of a tube, horizontal instead of radial, purple and magenta instead of white-cyan.
2. The brief (§9) already reserves `PathStyle.hasWalls = false` for it, so the core was designed with this case in mind. Building it tests those assumptions now, while fixing them is still cheap.
3. Its main building blocks are the grid floor with reflections, the world-aligned structure placement and the shared streak system. Env #3 needs all three, so most of this work counts toward Env #3 as well.

## 1. Visual breakdown (t_0.5, t_2.5)

- **Grid floor**: infinite, world-axis-aligned square grid, roughly 4 units per cell at ship scale. Lines are thick cyan (`#4FEAF5`, HDR 2–3) with a crisp core and a wide bloom halo. Intersections are slightly brighter. Between the lines the floor is near-black, glossy teal, and **reflects** the towers (purple and pink smears) and the ship's engine trails (the bright stripe under the ship). The grid stays visible all the way to the horizon, where it merges into a soft cyan band.
- **Monolith towers**: tall, dark, axis-aligned boxes, about 8–16 wide and 25–90 high, some with setbacks. The bodies are dark blue-grey with faint panel and greeble relief and a little cyan underglow near the floor. They stand in loose rows on both sides of a clear flight lane, with taller and denser towers further out.
  - **Circuit traces** (purple `#8A6CFF`, HDR 1–2): buses of 2–4 parallel traces that run vertically with 45° jogs and end in vias (small rings or dots). They appear on the faces that face the lane.
  - **Glyph text** (pink and magenta `#FF4FD8`, some pale cyan): rows of dashes of varying length, like lines of code, on panels near tower edges. Close up (t_2.5) they become the dominant element on screen.
  - Faint cyan vertical edge lines.
- **Light rain**: a few hundred long (5–15 units), bright cyan streaks with hot heads. They come from the vanishing region and slant slightly downward, filling the space above the floor rather than a cylinder.
- **Small floating props** near the floor: wireframe cubes or prisms, glass spheres, small holo rings (t_2.5).
- **Sky**: near-black navy, faint dust, a faint cyan haze at the horizon. There is no core flare.
- **Camera / ship**: identical to the vortex (same chase cam, same ship). The ship flies about 1.5–2.5 units above the floor.

## 2. Architecture fit

What can be reused as is: path, chunks, floating origin, flight, HDR pipeline, linegeo, shaderlib, params, debug UI and tests infrastructure.

Things in the core that assume a tunnel and need small changes:

| Assumption | Where | Change |
|---|---|---|
| Steering limit is symmetric `steerLimit · R` | `flight.c:130` | Add `PathStyle.floorDepth` (distance from centerline down to the floor, 0 = none) and `PathStyle.ceiling`. Flight clamps `y ≥ −floorDepth + shipClearance` when a floor exists. R now means the lane half-width |
| Atmosphere (fog toward core color, far fade) comes only from core params | `cybertunnel.c:~472` | Add an optional `Environment.atmosphere(env, Atmosphere*)` hook so the city can fog to near-black and turn off fog-to-core |
| Streaks, background and core are private to `env_vortex.c` | `env_vortex.c:662, 741, 777, 819` | Move streaks into a shared `render/streaks.c` with a volume shape (see Phase C). Background and core stay per-env |
| No planar reflections | pipeline | Add an optional core-driven reflection pass (Phase D), enabled when `floorDepth > 0` and the env implements `drawReflected` |
| Debris builders (cube, octa) are `static` in vortex | `env_vortex.c:531–560` | Move them into `render/shapes.c` so they can be shared |

Path style for the city: `maxPitchDeg = 0`, `twistRateMax = 0`, `radiusNoiseAmp = 0`, `maxYawStepDeg ≈ 4`, `baseRadius` (lane half-width) about 9, and `floorDepth ≈ 2.2`. With zero pitch, every control point has y = 0, so the spline stays exactly in the y = 0 plane. Rotation-minimizing frames on a planar curve keep `N` equal to world up, which means the camera never rolls with the path and the floor stays horizontal. A test will check this (Phase A).

Proposed new files:

```
src/env/env_city.c          environment: params, archetypes, chunk build, draw
src/env/city_towers.c/.h    tower placement + body/trace/glyph geometry (pure CPU)
src/render/gridfloor.c/.h   shared infinite grid floor (also for Env #3)
src/render/streaks.c/.h     streak system extracted from env_vortex.c
src/render/shapes.c/.h      cube/octa/prism wire+face builders extracted from env_vortex.c
src/render/shaders/grid.vs/.fs       floor
src/render/shaders/tower.vs/.fs      opaque tower bodies
src/render/shaders/glyph.fs          code-text panels (with face.vs)
src/render/shaders/sky.fs            city background
```

## 3. Key techniques

### 3.1 Grid floor (`render/gridfloor.c`)
- A single quad of 2 × viewDist, re-centred under the camera every frame and drawn in render space. It is opaque and writes depth, so towers occlude it correctly.
- **Precision**: the fragment shader needs world xz for the grid pattern. The CPU sends `uGridOffset = fmod(renderOrigin.xz, majorPeriod)` in double precision, so the shader only ever sees small numbers, even at s > 50 000.
- **Line AA**: use a filtered grid ("pristine grid" approach). Line width is expressed in world units and clamped to at least 1 px with energy conservation, which matches `minPixelWidth` in linegeo. Once grid cells drop below a few pixels, the lines fade toward their average luminance. This removes moiré near the horizon, which is the main risk with grids.
- Profile: crisp core plus a Gaussian halo, the same as `line_facing.fs`. Intersections get a brightness boost. Optional "data packets" run along some lines (per-line hash, cheap).
- Base floor colour: dark teal with a Fresnel term, mixed with the reflection texture (3.3).

### 3.2 Towers on a world lattice (`city_towers.c`)
The floor grid is world-aligned, so towers have to be world-aligned too. A tower rotated along the path frame would look wrong against the grid. Placement uses a world xz lattice (cell = 16 = 4 grid cells, so tower edges land on grid lines). Each chunk claims the cells it owns:

1. Walk the chunk's path samples (Δs = 2) and collect the lattice cells within the lateral band `[laneHalfWidth, bandMax ≈ 180]`.
2. For each cell, find the arc length `s*` of the closest path point by searching over `[s0 − M, s1 + M]`, with M = bandMax.
3. The chunk keeps the cell only if `chunk_index_at(s*) == k`. Because every chunk computes the same argmin, each cell is built exactly once, in any build order. This requires the path's minimum turn radius to be well above `bandMax`, which `maxYawStepDeg ≈ 4` guarantees. `path_curvature_limit` lets us assert it.
4. Keep-out: drop the cell if the distance from its center to the path, minus the tower's half-diagonal, is less than the lane half-width.
5. `hash(seed, cellX, cellZ)` decides occupancy (driven by archetype and distance), footprint (snapped to 2-unit steps within the cell), height (it grows with distance from the lane, with rare landmark towers), setbacks (0–2) and decoration flags.

The cell hash depends on world coordinates, not the chunk, so towers stay stable when parameters change, and building in a shuffled order produces identical chunks.

### 3.3 Planar reflection (core, optional but important for the look)
- A second HDR target at half resolution. The camera is mirrored about the plane `y = floorY` (in render space) and the env's emissive content is drawn again: traces, glyphs, streaks, plus the core's ship trails. Fragments below the plane are discarded (`uClipY` in `common.glsl`). Face culling is flipped or disabled because mirroring reverses winding.
- Blur vertically only (2–3 taps per pass, two passes). That gives the stretched, smeared look of a wet floor seen in the reference.
- The floor shader samples it at screen UV, offset by a small procedural normal, and weights it by Fresnel.
- Cost: one extra emissive pass at ¼ the pixel count. The existing frame time is about 0.4 ms, so there is plenty of headroom. The pass sits behind a toggle and a quality param (`reflectRes`: off / ¼ / ½).

### 3.4 Tower bodies, traces and glyphs
- **Body**: boxes with setbacks, built as fill triangles. `tower.fs` is opaque and uses face uv in world units for procedural panel seams, recessed vertical channels and a few dim window slots. It adds a cyan underglow near the bottom and fogs toward the sky colour with distance.
- **Circuit traces**: CPU routing on a lattice per face (column pitch about 0.7 units), with an occupancy bitmap so traces never cross. A bus of 2–4 traces starts at the base or top and climbs in runs of 3–12 units. At each run end it either jogs one or two columns at 45° (all traces of the bus together, as in t_0.5) or stops. Each trace ends in a via (small ring built with `lg_planar_arc`) or a pad. Output is planar strips 0.02 units off the face, reusing the ring fragment shader for pulses.
  - Vertex budget: traces only on faces whose normal points toward the lane (dot with the direction to the nearest path point > −0.2). Bus count drops with distance from the lane. The target is ≤ 60k vertices per chunk, to be measured in Phase B.
- **Glyph panels**: one quad per panel (`lg_fill_quad`). `glyph.fs` draws hashed rows of dashes in pink, magenta and pale cyan, with per-row slow scroll and flicker. Panels go on tower edges facing the lane, 30–60 % of the tower's height.
- **Edges**: camera-facing segments on the vertical edges and roof rim, faint cyan.

### 3.5 Light rain (shared `render/streaks.c`)
Extract the current streak system unchanged and run the vortex through it first, with the requirement that the vortex output stays pixel-identical. Then add:
- `volume = CYLINDER | SLAB`. A slab is `x ∈ ±halfWidth`, `y ∈ [floor + 1, floor + height]` in the path frame. Streak positions are still derived from the id in `streak.vs`, using the same 16 path frames.
- A per-streak direction tilt (`uFallTilt`), so the streaks slant down instead of running parallel to the path.
- City defaults: about 500 streaks, long (`uLenMul ≈ 3`), thick, cyan, 3 % comets, no purple and no helix.

## 4. Phases

Each phase ends with its checks passing, a commit, and a short `NOTES.md` section with a screenshot from `--shot`, the same as for the vortex.

### Phase A — Core generalisation (no visual change)
- Add `PathStyle.floorDepth` and `ceiling`, the asymmetric flight clamp and the `Environment.atmosphere` hook.
- Extract `render/streaks.c` and `render/shapes.c` from the vortex.
- Register `ENV_CITY` with a stub `env_city.c`, copied from the template, that has the city path style.
- *(Done — all 41 tests pass, 8 regression renders byte-identical.)*
- **Accept**: all 35 existing tests pass. The vortex `--shot 3000` render is pixel-identical before and after (compare hashes). New tests: with the city style, `N·up > 0.999999` and `|pos.y| < 1e-9` over 20 000 units, and the ship never goes below `−floorDepth + clearance` under full-down input.

### Phase B — Grid floor
- `gridfloor.c` plus `grid.fs`, filtered lines, horizon fade, sky shader with horizon haze. City fog/atmosphere via the hook.
- *(Done — see NOTES.md Phase B. Deviation: heading spring toward world axes added to the path.)*
- **Accept**: no visible moiré or shimmer in a 60 s flight at 720p and 1080p. Grid continuity across a floating-origin rebase (screenshot before and after an origin snap). Side by side with t_0.5 (floor only).

### Phase C — Towers: placement and bodies
- World-lattice placement with chunk ownership, keep-out lane, opaque bodies with setbacks and `tower.fs` panels, edge lines.
- **Accept (tests)**: building chunks 0..200 in order and in shuffled order gives identical hashes. **No lattice cell is claimed by two chunks**, and none inside the band is claimed by zero chunks. No tower intersects the lane (distance check against path samples). Chunk build time stays below 3 ms.

### Phase D — Traces, glyph panels, rain
- Trace router, vias, glyph shader, rain through `streaks.c` in slab mode, small floating props from `shapes.c`.
- **Accept**: side-by-side screenshots against t_0.5 and t_2.5. Vertices per chunk and draw calls are recorded in the overlay and stay within the budget in §6 of the brief (< 150 draw calls, CPU < 2 ms).

### Phase E — Floor reflection
- Mirror pass in the core, blur, Fresnel mix in `grid.fs`, ship trails included.
- **Accept**: towers and trails are visible in the floor as in t_0.5 (the purple smears under towers and the trail stripe under the ship). Toggles exist for off / ¼ / ½ resolution, and frame time is measured for each. Nothing from below the floor leaks into the reflection.

### Phase F — Pacing, params, presets, docs
- Archetypes (driven by a Markov chain over runs, like the vortex):
  - **AVENUE**: two tidy rows lining the lane, the reference framing.
  - **DOWNTOWN**: dense and tall, glyph-heavy, towers close to the lane, high energy.
  - **OPEN**: sparse and low, with long grid vistas.
  - **PLAZA**: no towers near the lane, floating props and holo rings, a breather.
- Debug UI group "City", `presets/city.ini`, and `INTEGRATION.md` and `NOTES.md` updates. A 10-minute bench at 3× speed.
- **Accept**: the same metrics as the vortex bench (no streaming hitch > 4 ms, max GL coordinate < 2048, bounded GPU meshes).

## 5. Starting parameters (tune by eye)

| Param | Value |
|---|---|
| lane half-width (`baseRadius`) | 9 |
| floorDepth | 2.2 |
| grid cell / major every | 4 / 4 cells |
| grid line half-width / HDR | 0.06 / 2.5 |
| lattice cell | 16 |
| tower footprint | 6–14 |
| tower height | 25–90 (+ rare 140 landmarks) |
| band max | 180 |
| trace pitch / width / HDR | 0.7 / 0.05 / 1.4 |
| glyph HDR | 1.5–2.5 |
| rain count / length | 500 / 5–15 |
| fog | to `#03060C`, start 120, end 420 |

## 6. Risks and open questions

- **Grid aliasing at the horizon** is the biggest visual risk. The filtered grid in Phase B is non-negotiable, and bloom makes shimmer worse.
- **Tower pop-in** at the 400-unit streaming edge. Towers are tall, so they are visible from far away. Mitigations: fog to near-black before 400, plus the existing chunk fade-in. `CT_VIEW_DIST` may need to become per-env (the chunk pool of 24 × 48 = 1152 units leaves room).
- **Trace vertex count** may be too high in DOWNTOWN. The fallback is to do traces for distant towers procedurally in `tower.fs`, and keep CPU geometry only near the lane.
- **Path heading**: the reference flies straight along a grid axis. A gentle yaw drift adds parallax, but the lane then cuts through the grid at angles. An optional `yawSnap` (long straights along the axes with rounded 90° turns) is a possible follow-up. I recommend checking how the drift looks before building it.
- **Camera height**: if the ship looks too high above the floor compared with t_0.5, we may need per-env camera overrides (`camHeight`, `fovBase`).
- **Out of scope**: transitions between environments (the brief §9 portal and whiteout blend). The city is reached with `T` or `--env 2`, like the template.
