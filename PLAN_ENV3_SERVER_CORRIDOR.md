# Plan — Environment #3 "Server Corridor"

Status: built (Phases A–F, see NOTES.md); CRYSTAL archetype left out (decided 2026-10-03, §6). Reference frames: `reference/t_4.5s.png` … `reference/t_7.25s.png`
(every 0.25 s, extracted from `reference/video.mp4`), with `t_5.0s`, `t_6.0s`, `t_6.5s` and `t_7.0s` as the key frames.

## 0. Scope

The brief (§9) groups 3.5–7.5 s as Env #3 "Server Canyon / Crystal Field". The frames split it in two:

| Time | Look | Verdict |
|---|---|---|
| 3.5–4.5 s | Barrel roll through orange wireframe polyhedra | Not the environment. Becomes a rare **prop/set piece** inside it (§3.7, open question §6) |
| 4.5–7.5 s | Corridor of server pillars with green code rain, white stepped outlines, circuit beams overhead, holo rings, same grid floor | **Build this** |
| 7.0–7.5 s | The corridor ends in the vortex | Transition. **Out of scope** until the transition design is settled |

Like the city, this environment is open space over the shared grid floor with the shared reflection,
but the framing is tight: walls of pillars right next to the lane and structure overhead.
Where the city is wide and purple-pink, the corridor is enclosed and green-white.

## 1. Visual breakdown (t_4.5 – t_7.25)

- **Grid floor**: the same cyan grid as the city, a bit brighter and with a similar cell size.
  Reflections show green smears under the pillars and the white outlines.
- **Server pillars**: tall dark slabs, about 5–9 wide, 3–6 deep and 30–45 high. They stand in two
  rows just outside the lane, 1–3 per side visible at a time, with gaps between them.
  Bodies are dark slate with faint rack panels.
  More rows stand behind at lower contrast, and some pillars further down the corridor stand
  almost in the lane's line of sight, framing the vanishing point (t_5.0, t_6.0).
  - **Code rain** (`#39FF6A`, HDR 1.5–3): vertical columns of short rounded dashes that scroll
    downward. Brightness varies per column, with a brighter head, and the gaps between dashes vary.
    The columns cover most of the lane-facing face and continue around the side faces. Close up
    the dashes are big pills (t_6.25). From a distance they become fine vertical hatching.
  - **Stepped white outlines** (white with a pale cyan halo, HDR 4+, very thick): the signature
    element. Each one is a castellated polyline in the pillar's lane-facing plane: it climbs the
    pillar's edge in 2–4 right-angle steps (the pillar's front profile has setbacks) and at the
    base it turns **onto the floor**, running a short distance along the lane edge (t_6.25, t_7.0, t_7.25).
    Not every pillar has one. About half do, and they always face the lane.
- **Overhead circuit beams**: wide, flat, dark beams 20–30 above the floor. They cross the
  corridor at right angles and also run along it, forming a broken lattice. Their undersides and sides
  are densely covered in cyan circuit traces, much denser than the city's. Gaps in the beams show sky.
- **Sky lattice**: above the beams, a sparse grid of thin, faint white Manhattan line segments (a far,
  high "ceiling" plane) with parallax. The rest is near-black navy with dust.
- **Holographic rings**: thin cyan rings, 2–3 stacked or concentric per cluster, horizontal or
  slightly tilted, hovering at pillar tops or beside the lane at mid-height. They rotate slowly.
- **Light rain**: sparse white-cyan streaks, fewer than in the city, mostly in the upper half.
- **Props**: occasional wireframe cubes (white) and orange polyhedra (t_5.0, t_5.25).
- **Camera / ship**: unchanged. The ship flies low, about 2 above the floor, as in the city.

## 2. Architecture fit

Almost everything is already in the core from Env #2: floor/ceiling clamp, yaw spring, `pathReach`,
the atmosphere hook, the reflection pass, `gridfloor`, `streaks` (slab + centre gap), `shapes`,
`linegeo`, the decor trace router and the glyph shader pattern. No core change is required for the look.

What to extract or generalise so it doesn't get copied:

| Item | Where now | Change |
|---|---|---|
| Lattice-cell ownership (nearest path point → owning chunk) | `city_towers.c` (`city_towers_plan`, `nearest`, `Poly`) | Move it to `env/lattice_place.c/.h`: a generic "cells in a band around the path, owned by exactly one chunk" iterator with a callback. The city and the corridor both use it. City output must stay byte-identical |
| Trace router (bus routing with an occupancy grid, vias) | `city_decor.c` (static) | Expose `decor_route_face(...)` with colour/pitch/density params so beam sides can reuse it near the lane |
| Archetype Markov chain + blend | `env_city.c` (`city_archetype_of_chunk`, `city_blend`) and `env_vortex.c` | Share it as `core/pacing.c` (table of run min/max + transition matrix → archetype per chunk, blend window). Both envs keep their tables |
| Grid floor | `render/gridfloor.c` | Reused as is. Add a `pattern` mode for the sky lattice (§3.5): same ray/plane trick, a different line function, and the plane is above the camera |

Path style for the corridor: level path (pitch 0, twist 0), like the city. A stronger yaw spring
(heading within about ±12° of an axis) keeps the corridor readable while still drifting gently.
Lane half-width (`baseRadius`) about 6.5, `floorDepth` 2.2, `ceilingHeight` about 14 (the ship never
reaches the beams). Per the treadmill rule, the camera and flight model stay shared.

New files:

```
src/env/env_corridor.c          environment: params, archetypes, chunk build, draw
src/env/corridor_pillars.c/.h   pillar placement + stepped bodies + outline polylines (pure CPU)
src/env/corridor_beams.c/.h     overhead beam lattice (pure CPU)
src/env/lattice_place.c/.h      shared ownership iterator (extracted from city_towers.c)
src/core/pacing.c/.h            shared archetype chain (extracted)
src/render/shaders/pillar.fs    rack body (opaque)
src/render/shaders/coderain.vs/.fs   code rain panels (emissive)
src/render/shaders/beam.fs      beam body with procedural circuit pattern
```

## 3. Key techniques

### 3.1 Pillar placement
Use the city's world-lattice approach so pillars line up with the floor grid, but with a finer
lattice (8 = 2 grid cells) and a narrow band:
- **Wall rows**: cells whose distance to the path is in `[laneHalfWidth + clearance, + 14]`. Occupancy is high
  (0.55–0.85), and runs of adjacent occupied cells merge into one wider pillar (up to 2 cells), so the
  walls read as distinct slabs with gaps, not a solid fence.
- **Back rows**: `[+14, bandMax ≈ 90]`, lower occupancy, darker, little or no outline, cheaper decor.
- Keep-out and order-independent ownership come from `lattice_place` (same tests as the city).
- On curves the world-aligned wall rows step in and out against the lane. With the stronger yaw spring
  this should look like a natural stagger. Check it in Phase B before considering path-aligned placement.

### 3.2 Pillar bodies and stepped profiles
- The front profile is a stepped silhouette: from the base up, the half-width changes at 1–3 heights by
  ±1–2 units (snapped to 0.5), always within the cell. The body is extruded from that profile as stacked
  boxes (`CITY_MAX_TIERS` style, but the steps are on the lane side, not setbacks at the top).
- `pillar.fs`: dark slate, rack panel seams every ~2 units, faint recessed slots, a slight cyan
  underglow near the floor, fog. It reuses `tower.vs`.

### 3.3 Code rain
- One emissive quad per lane-facing face and per visible side face, 0.02 off the surface (as glyph panels).
- `coderain.fs` uses face uv in world units. Columns are every ~0.45. For each column a hash gives speed (1–4 u/s),
  dash length/gap distribution, brightness and head position. Dashes are rounded rects
  (SDF), and the head and the 2–3 dashes behind it are brighter. Scrolling uses `shaderTime`.
- **Anti-aliasing**: as with the grid, when a column gets narrower than ~2 px the pattern fades toward
  its average luminance (fwidth-based), so there is no shimmer at distance. Same rule for dash rows.
- Colour: mostly `#39FF6A`, with a few paler mint columns and an occasional white flicker.

### 3.4 Stepped outlines and floor kerbs
- For about half of the wall pillars (and only lane-facing ones), the CPU builds a polyline: base corner
  on the floor → up the lane-side edge following the stepped profile → across the top (or stop partway).
  At the base, a floor segment of 2–6 units runs along the lane edge (Manhattan, world-aligned) and
  sometimes links to the next pillar.
- Draw: camera-facing thick lines (`linegeo`, width 0.18–0.3, HDR 4, `whiteCore` high). Floor segments
  lie flat on the floor (planar strips, like vias) so they look painted on the grid. Both are drawn in the
  reflection pass.
- Keep the centre clear: outlines live at the lane edges. Floor kerbs stay outside the lateral range the ship
  can reach, so they never run under the ship's path.

### 3.5 Overhead: beams and sky lattice
- **Beams**: a second, coarser world lattice (cell 16) at height 22–30. Beam lines are hash-selected lattice
  rows and columns (about 1 in 3), filled cell by cell within `bandMax` and broken by hashed gaps. Each
  beam cell is owned through `lattice_place`, so the beams stay deterministic and order-independent. Cross-section
  6–10 wide, 2–3 thick.
- `beam.fs` draws the circuit pattern procedurally on the underside and sides (hashed Manhattan traces on a
  small lattice, vias at ends, pulses), with distance filtering. CPU-routed traces (`decor_route_face`)
  only on beam cells within ~30 of the camera path, if the procedural ones look too flat up close. This keeps
  the vertex budget sane: beam undersides are big, and the reference density would cost far more than the city's.
- **Sky lattice**: `gridfloor` in pattern mode on a plane at y ≈ +90. Instead of a full grid it draws hashed
  sparse segments (a cell edge is lit with probability ~0.3, as thin, faint white lines) and fogs out quickly.
  It is one fullscreen pass, drawn before the beams, with depth.

### 3.6 Holo rings
- Clusters of 2–3 rings (radius 2–5) built with `lg_planar_arc`, drawn with the existing ring shader
  (pulses, gaps). They sit at pillar tops (owned with the pillar) or float at mid-height beside the lane
  (owned per chunk, like the city props). Slow spin via a per-cluster uniform or vertex time.

### 3.7 Rain and props
- Rain: the shared slab streaks with a wide centre gap, ~250 visible, white-cyan, mostly upper half.
- Props: white wireframe cubes (`shapes.c`). Orange polyhedra (icosa, octa, prism) only if the user wants
  them here (see §6). If yes, a rare `CRYSTAL` archetype adds a cluster of them beside and above the lane.
  Never in the centre band.

## 4. Phases

Each phase ends with its checks passing, a commit, and a short `NOTES.md` section with `--shot`
screenshots and side-by-side comparisons against the reference frames.

### Phase A — Extraction and stub (no visual change)
- Extract `lattice_place` from `city_towers.c`, `core/pacing` from city/vortex, `decor_route_face` from `city_decor.c`.
- Register `ENV_CORRIDOR = 3` with a stub `env_corridor.c`: path style, grid floor, sky, reflection on.
- **Accept**: all existing tests pass. City and vortex `--shot` regressions are byte-identical (same hashes as
  before). New tests: corridor path stays level with N = up over 20 000 units, and the heading stays within ±12° of an axis.

### Phase B — Pillars: placement and bodies
- Wall and back rows, merging, stepped profiles, `pillar.fs`.
- **Accept (tests)**: ownership is unique and complete (as for the city), shuffled build order gives the same
  result, no pillar intersects the lane plus clearance at any ship position, build time < 3 ms. Visual check
  of curved stretches (stagger acceptable?).

### Phase C — Code rain and stepped outlines
- `coderain.fs`, outline and floor-kerb polylines, both in the reflection.
- **Accept**: side by side with t_6.0, t_6.5 and t_7.0. No shimmer at distance (60 s flight at 720p and 1080p).
  Outlines never cross the lane centre band.

### Phase D — Overhead beams and sky lattice
- Beam lattice and `beam.fs`, gridfloor pattern mode for the sky.
- **Accept**: side by side with t_5.0 and t_5.25. Beams continue across chunk boundaries without seams,
  and draw calls and frame time are recorded.

### Phase E — Holo rings, rain, props
- **Accept**: side by side with t_5.0 and t_7.25. The centre band stays clear (as in the city rain retune).

### Phase F — Pacing, presets, docs, bench
- Archetypes (run lengths and transitions as in the city):
  - **COLONNADE**: regular wall pillars on both sides with gaps, sparse beams. The reference framing.
  - **SERVER HALL**: dense walls (few gaps), many outlines, dense beams overhead. Enclosed, high energy.
  - **ATRIUM**: wall rows pushed back, no beams, big holo rings, long floor vistas. The breather.
  - **CRYSTAL** (only if approved, §6): no near pillars, an orange polyhedra cluster.
- Debug UI group "Corridor", `presets/corridor.ini`, `INTEGRATION.md` §10b and `NOTES.md`.
  10-minute bench at 3× speed.
- **Accept**: same metrics as the city bench: < 150 draw calls, streaming < 4 ms per frame, max GL coordinate
  < 2048, bounded GPU meshes, ≤ 60k vertices per chunk.

## 5. Starting parameters (tune by eye)

| Param | Value |
|---|---|
| lane half-width / clearance | 6.5 / 1.0 |
| floorDepth / ceilingHeight | 2.2 / 14 |
| yaw spring | heading within ±12° of an axis |
| pillar lattice cell / wall band / bandMax | 8 / +14 / 90 |
| pillar width / depth / height | 5–9 / 3–6 / 30–45 |
| profile steps | 1–3, ±1–2 units |
| code rain column pitch / speed / HDR | 0.45 / 1–4 u/s / 1.5–3 |
| outline width / HDR / chance | 0.18–0.3 / 4 / 0.5 |
| beam lattice / height / width / row chance | 16 / 22–30 / 6–10 / 1 in 3 |
| sky lattice height / segment chance | 90 / 0.3 |
| holo ring radius / per cluster | 2–5 / 2–3 |
| rain visible | ~250, centre gap on |
| fog | to `#02050A`, start 80, end 340 (shorter than the city, the corridor is enclosed) |

## 6. Risks and open questions

- **Outline brightness vs. the clear centre**: the outlines are the brightest thing in the reference and very
  close to the lane. With bloom they could wash out the play area. Start at the reference look and be ready to
  lower the HDR and halo, or limit them to the far side of each pillar.
- **Code-rain aliasing** at distance and in the reflection: the filtered fade (§3.3) is required, like the grid.
- **Walls on curves**: world-aligned pillars against a drifting path. If the stagger looks wrong, the fallback is
  to snap the path to axes more strongly in this env, not to rotate the pillars.
- **Beam vertex/overdraw cost**: kept mostly procedural for that reason. Measure in Phase D.
- **Pop-in**: pillars are close and tall, but the corridor is enclosed and the fog is shorter, so this should be less
  of a problem than in the city.
- **Decided — orange polyhedra**: left out for now (user, 2026-10-03). Revisit when collision is designed:
  as obstacles they would need collision, which does not exist yet.
- **Open — vortex at the end of the corridor**: the reference ends the corridor in the vortex. That ties into
  the undecided transition design. Nothing here assumes or blocks it.
