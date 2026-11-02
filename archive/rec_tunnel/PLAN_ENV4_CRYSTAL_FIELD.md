# Plan — Environment #4 "Crystal Field"

Status: planned (2026-10-21). Registry id `ENV_CRYSTAL = 5` (id 4 is the greybox; "Env #4" is the
docs name). Reference frames: `reference/concepts/env4-crystal-field_00001_.png` and `_00002_`
(the target look), plus `reference/t_4.0s.png` and `t_4.25s.png` (the orange polyhedra in the
reference video, 3.5–4.5 s, left out of Env #3).

## 0. Scope

A dark open void filled with floating orange wireframe polyhedra. Clusters line both sides of a
clear flight lane that leads to a soft warm glow at the vanishing point.

| In | Out (for now) |
|---|---|
| Polyhedra field around the lane, clusters, size spread from pebbles to giants | Collision with the polyhedra (no collision system exists; §6) |
| Faint translucent faces, glowing edges | The reference video's corridor, grid floor and beams behind the shapes (that is Env #3) |
| Warm vanishing-point glow, dust, sparse speed streaks | Transitions into or out of other environments (undecided) |
| Pacing archetypes, presets, docs | Game-only looks (none planned) |

Unlike the city and the corridor there is **no floor and no walls**: the lane is defined only by
where the shapes are not. This is the first open environment without a floor, so the steering
box (`hasWalls = false`) is the only limit on the ship.

## 1. Visual breakdown (concept frames)

- **Background**: near-black blue (`#05070D`), with sparse fine dust. A soft, wide warm glow at
  the vanishing point (`#C88A50` falloff to brown), with a small bright pinpoint core. No sky
  structure, no horizon.
- **Polyhedra**: icosahedra, octahedra, tetrahedra, triangular prisms, and stretched bipyramids
  (the tall "crystal" in t_4.0). Mostly regular solids, some stretched or squashed along one axis,
  each at a random orientation.
  - **Edges**: thin orange/amber lines (`#FF8C2E`, some `#FFB04A`), HDR about 2–3, with a soft bloom
    halo. Near shapes get slightly thicker edges in screen space, but stay thin relative to their
    size (lines never become bars).
  - **Faces**: very faint, dark, slightly warm translucent fill (about 5–10 % brightness), a bit stronger at
    grazing angles, so near shapes read as glassy volumes and not bare wire. Back edges show through.
  - **Sizes**: a heavy-tailed spread. Many small ones (0.5–2) far out, a medium mass (2–6), and a few giants
    (8–16) close to the frame edges that partly leave the screen.
- **Layout**: two loose banks of clusters, left and right, plus some above and below. The middle
  third of the screen around the vanishing point stays empty except for tiny distant shapes along
  the edges of the glow.
- **Streaks**: sparse thin white speed streaks, mostly at the frame edges.
- **Ship / camera**: unchanged (shared flight model, treadmill).

## 2. Architecture fit

Reuse:

| Need | Existing piece |
|---|---|
| Background + warm vanishing glow + dust | `background.fs` (vortex), with its own colours set every frame |
| Bright pinpoint core | `core.fs` (vortex), optional, warm tint |
| Edge lines | `lg_facing_segment` + `object.vs` / `line_facing.fs` (as greybox, vortex debris) |
| Faces | `lg_fill_tri`; a new small `crystal_face.fs` (the data pattern in `face.fs` is the wrong look) |
| Speed streaks | `render/streaks` in cylinder mode with `centerClear` |
| Archetypes | `core/pacing` |

New:

```
src/env/env_crystal.c          environment: params, archetypes, chunk build, draw
src/env/crystal_field.c/.h     polyhedron table + placement (pure CPU)
src/render/shaders/crystal_face.fs   translucent face fill
```

`render/shapes.c` gets a general polyhedron path: a shape described as vertex/edge/face tables,
built at an arbitrary position, rotation and scale (non-uniform for stretched crystals). The
existing `shape_cube` / `shape_octa` / `shape_sphere` stay byte-identical; the vortex and city
output must not change.

Shapes are baked into chunk geometry in chunk-local coordinates (one mesh per batch, no per-shape
draw calls), so hundreds of shapes cost a handful of draw calls per chunk.

**Path style**: open, no floor. Gentle yaw (about 8° per control point) and some pitch (about 8°)
so the field feels 3D, no twist. The lane half-width (`baseRadius`) sets the steering box, as in
the open environments.

## 3. Key techniques

### 3.1 Placement (pure CPU, deterministic)

- Each chunk owns the shapes whose anchor arc length lies in `[s0, s1)`, so ownership is unique
  with no lattice. RNG: `ct_hash64_3(seed, ENV_CRYSTAL, chunkIndex)` with sub-streams per feature.
- **Clusters**: per chunk, a Poisson count of cluster centres. Each centre is at an angle around
  the path (weighted toward left and right, less above and below) and a radial distance in
  `[keepOut, bandMax]`. Each cluster holds 3–15 shapes scattered around its centre with a
  size spread. Single scattered shapes fill between clusters.
- **Keep-out**: the steering box reaches `steerLimit × baseRadius` on each axis, so its corner is
  at `√2 × steerLimit × baseRadius`. No part of any shape (centre distance minus bounding radius)
  may come within that plus a clearance of the path, checked against path samples over the
  shape's extent (`pathReach`). This holds on curves, where a shape anchored in one chunk can
  swing toward the lane at another arc length. A shape that fails is pushed out or dropped.
- **Clear centre**: on top of the hard keep-out, a soft cone keeps shapes rarer close to the lane
  ahead, so the vanishing point stays clean (the memory rule: centre band clear for enemies).

### 3.2 Polyhedra

- Table of solids: tetrahedron, octahedron, icosahedron, triangular prism, cube (rare), bipyramid
  (hexagonal or pentagonal, stretched 1.5–3× along its axis).
- Per shape: random rotation (uniform quaternion from the RNG), scale, optional stretch, colour
  (orange / amber / rare pale gold), edge HDR and face alpha.
- Edges as camera-facing segments with a width tied to shape size (min pixel width from the core
  atmosphere so far shapes don't shimmer). Faces as triangles in a second batch.

### 3.3 Faces

- `crystal_face.fs`: dark warm fill, alpha-free additive (emissive pass), brightness
  `base + fresnel × k` from the face normal and view direction, fogged with `apply_atmo`. Drawn with
  depth test on and depth write off, so back edges stay visible through faces as in the concept.

### 3.4 Background and glow

- `background.fs` with crystal colours: `uBgColor` near-black blue, `uGlowColor` warm, `uDust` low.
  Every uniform set every frame (shared-program rule).
- Optional `core.fs` pinpoint at the vanishing point, warm. Off if it competes with the lane.
- Fog: toward a dark warm brown so far shapes sink into the glow instead of turning grey.

### 3.5 Streaks

- Shared streaks, cylinder volume, white, sparse (~150 visible), `centerClear` on.

## 4. Phases

Each phase ends with its checks passing, a commit, and a short `NOTES.md` section with `--shot`
screenshots and side-by-side comparisons against the concept frames.

### Phase A — Stub
- Register `ENV_CRYSTAL = 5` (`env.h`, `cybertunnel.h`, `cybertunnel.c`, `CMakeLists.txt`),
  `env_crystal.c` with path style, background and glow, streaks.
- **Accept**: all existing tests pass; vortex, city, corridor and greybox `--shot` byte-identical
  to before. New tests: build-order independence, path has no twist and stays within the pitch
  limit over 20 000 units.

### Phase B — Polyhedra and placement (edges only)
- General polyhedron builder in `shapes.c`, solids table, cluster placement, keep-out.
- **Accept (tests)**: no shape inside the keep-out at any arc length over 20 000 units; shuffled
  build order gives identical chunks; build < 3 ms per chunk; existing shapes byte-identical.
  Visual: side by side with the concept frames.

### Phase C — Faces and edge look
- `crystal_face.fs`, edge width and HDR by size and distance, colour variety.
- **Accept**: side by side with both concept frames and t_4.0. No shimmer on far shapes in a 60 s
  flight at 720p and 1080p.

### Phase D — Pacing archetypes
- **DRIFT**: medium clusters on both sides, the concept framing. Default.
- **SHOAL**: dense banks of small and medium shapes close to the lane. High energy.
- **GIANTS**: few, huge, slow-passing shapes at the frame edges. Calm.
- **VOID**: almost empty, the glow brightens. The breather.
- **Accept**: archetype sequence deterministic, blends without pops across chunk boundaries.

### Phase E — Presets, docs, bench
- Debug UI group "Crystal", `presets/crystal.ini`, `INTEGRATION.md` table, `NOTES.md` summary.
  10-minute bench at 3× speed.
- **Accept**: < 150 draw calls, streaming < 4 ms per frame, max GL coordinate < 2048, ≤ 60k vertices
  per chunk.

## 5. Starting parameters (tune by eye)

| Param | Value |
|---|---|
| lane half-width (`baseRadius`) | 12 |
| yaw step / pitch / twist | 8° / 8° / 0 |
| keep-out clearance (beyond the steering box corner) | 3 |
| band max (outer radius of the field) | 90 |
| clusters per chunk / shapes per cluster | 2–4 / 3–15 |
| size: small / medium / giant | 0.5–2 / 2–6 / 8–16 (giants ~3 %) |
| edge colour / HDR | `#FF8C2E`, `#FFB04A` / 2–3 |
| face brightness / fresnel | 0.06 / 0.15 |
| background / glow | `#05070D` / `#C88A50` |
| streaks visible | ~150, white, centre clear |
| fog | to dark warm brown, start 60, end 320 |

## 6. Risks and open questions

- **Open — tumbling**: the shapes in the concept look static; in the video they turn slowly. Baked
  geometry makes per-shape rotation expensive (it needs the shape centre per vertex, and the vertex
  attributes are full). Options: static (default), or a slow tumble for a few hero shapes per chunk
  through per-chunk uniforms. Decide after Phase C.
- **Open — fly-through giants**: a rare set piece where a huge hollow shape surrounds the lane and the
  ship flies through its interior (edges well outside the steering box). Striking, but it puts lines
  around the play area. Only if wanted.
- **Decided — no collision**: shapes stay outside the reachable box. Revisit with collision design.
- **Orientation in the void**: with no floor and no walls, pitch and yaw may be hard to read. The
  cluster banks and streaks must carry the sense of direction. If it disorients, lower the pitch.
- **Overdraw**: giants close to the camera with additive faces. Keep face brightness low and measure in
  Phase C.
- **Orange against the warm glow**: shapes near the vanishing point could merge with the glow. The fog
  colour and the clear-centre cone address this; check in Phase B.
