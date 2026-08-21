# RECURSION_94 — Project Design Specification (v2.4)

**Developer:** Dream Software
**Target:** 1.44MB Game Development Contest (UPX-packed executable)
**Core Philosophy:** Bare-metal performance, zero external assets, procedural generation, synesthetic flow-state.


## 1. Pillars

- **Genre:** Rail shooter in the Rez / on-rails tradition. Single-zone prototype, 3–5 minute escalating run.
- **Aesthetic:** Authentic demoscene — constraint-driven scope, computer-hardware-as-architecture. Environment geometry uses procedural vertex/fragment shaders and instanced chamfered meshes; ships, enemies, and the boss use connected runtime-built low-poly forms with a dedicated material-lighting shader. Hand-written GLSL, zero external assets.
- **Target experience:** Escalating trance. Every seed compiles one coherent run identity. Boost drives visual displacement, rhythmic density, filter brightness, sidechain depth, and effects intensity while the musical clock remains fixed.
- **Scope discipline:** The 1.44MB constraint is a generative constraint, not an obstacle — guarded against creep across procedural generation, audio, and performance targets.


## 2. Scope & Technical Constraints

* **Engine / Language:** The project utilizes a custom C11 engine built on Raylib 6 (with unused subsystems stripped via `config.h`) alongside hand-written GLSL 330 shaders.
* **Size Constraint:** The final UPX-packed executable must be $\le$ 1,474,560 bytes to meet the 1.44MB contest limit. The uncompressed C binary provides roughly 2.5MB of actual pre-packed code headroom.
* **Asset Pipeline:** The project enforces a strict zero external assets policy. No `.png`, `.wav`, or `.obj` files are permitted; 100% of the game's geometry, textures, and audio are mathematically generated at runtime in RAM or on the GPU.
* **Compiler Flags:** The build pipeline uses C11 with warnings enabled plus `-O2 -s -ffunction-sections -fdata-sections -Wl,--gc-sections`, followed by UPX packing on the final binary.

## 3. Global State & The Procedural Environment

* **Collidability Ruling:** The procedural environment is strictly non-collidable background dressing. All collision tension, dodging, and gameplay hazards are 100% enemy-driven. The CPU will not track or calculate terrain hitboxes, preserving the player's trance-like flow-state and preventing cheap wall-crashes.
* **The Stationary Treadmill:** The camera and the player's craft never physically translate on the Z-axis, remaining locked near the origin ($Z = 0$) to eliminate floating-point precision loss at extreme distances.
* **World Momentum:** Forward motion is a mathematical illusion driven by a globally tracked `virtualPlayerZ` variable. Boost accelerates this variable and reveals additional visual/audio layers, but never changes the seed's BPM. This avoids pitch/time instability and keeps the four-on-floor trance groove danceable.
* **Procedural Architecture (Hash2D Socketing):** The background hardware environment (monoliths, memory banks, and chips) is generated using a deterministic Hash2D socketing grammar applied to discrete grid coordinates. Because the terrain is non-collidable, this generation serves purely to create a structured, architectural visual aesthetic without incurring CPU physics overhead.
* **Just-In-Time (JIT) Horizon:** The draw-distance is treated as a diegetic visual feature rather than a limitation. The environment dynamically compiles and extrudes its geometry at the far clipping plane, allowing the player to physically watch the wireframe grid and hardware shapes continuously generate out of the void as `virtualPlayerZ` advances.

## 3.1 Implementation Notes
Preventing Heap Allocation: We must explicitly dictate the use of a fixed C-array (e.g., MAX_ACTIVE_OBSTACLES 128) for the Ring Buffer to stop the agent from hallucinating dynamic memory allocations like malloc and free.

Raylib Shader Boilerplate: Local agents often fail at shaders by hallucinating the graphics API. We must explicitly instruct it to use uniform mat4 mvp; so it correctly accepts the Model-View-Projection matrix passed automatically by Raylib.

Enforcing Determinism: We need to spoon-feed the agent the exact Hash1D function directly to prevent it from trying to pull in <stdlib.h> and relying on rand().

## 3.2 Hierarchical Generation Architecture

The environment is derived through four deterministic layers: 256 m districts, variable-length zones, coordinated left/right archetype pairs, and sector-level modular detail. District-scale value noise correlates openness, density, and height so the landscape forms readable visual phrases instead of independent random samples.

Each district contains four zones whose lengths are selected from fixed partitions summing to 16 sectors. This preserves stateless random access while removing the previous fixed 64 m rhythm. Adjacent zones use disjoint archetype families, rare landmarks and gantries use local-maximum spacing, and both flanks are composed together to preserve negative space.

Floor gaps and submerged structures use the same integer cell hash on the CPU and GPU. Generated elements are emitted through priority tiers, with critical corridor and landmark geometry protected from decorative-detail pressure. `VALIDATE_GENERATOR=1 ./build.sh` builds and runs a headless deterministic audit covering adjacent repetition, landmark spacing, peak structure count, and dropped structures.

Shader files remain separate during development. The contest release build must embed the GLSL source into the executable to satisfy the single-binary, zero-external-assets requirement.

## 3.3 Deterministic Run Compiler

The opening screen selects a 32-bit run seed with the arrow keys (`Left/Right` changes by one; `Up/Down` changes by 100). The screen previews the seed's minor key, fixed 128–136 BPM tempo, palette, and demoscene identity before `Enter` compiles the run. That value seeds district composition, floor gaps, enemy formations and timing, combat particles, synthesizer patterns/noise, tonality, tempo, and screen effects. The same seed therefore reproduces the same authored-by-algorithm run.

For automated tests or exact run sharing, `recursion_94.exe --seed 94` bypasses the selector. Normal launches always show it.

## 3.4 Procedural Trance and Demoscene Layer

The runtime synth uses a seed-selected natural minor tonality, four-on-floor kick, sub-dominant root/fifth/octave bass, off-beat open hats, closed hats, claps on beats two and four, and filtered arpeggios. Detuned saw-and-sine pads glide through seeded two-bar chord phrases while a triangle-forward arpeggio, moving resonant filters, decorrelated noise bed, and multi-tap stereo cross-delay supply the ambient field. Softer transients and continuous saturation keep the mix warm instead of brittle or General MIDI-like. Boost increases arrangement density and timbral energy without changing tempo.

The visual layer adds a seeded eclipsed data moon, star strata, slow aurora ribbons, distant megacity silhouettes, scanlines, oscilloscope borders, floor moire interference, traveling raster energy, structure circuitry, and palette-linked fog. A full-scene GLSL pass adds edge-aware antialiasing, high-threshold bloom, restrained chromatic separation, filmic highlight rolloff, very subtle anamorphic highlights, speed-weighted radial echo, horizon haze, color grading, vignette, and grain. Architecture uses two procedural instancing meshes: chamfered rectangular hardware for memory slabs/conduits and centered eight-sided prisms for cache towers/landmarks. Buildings gain scale and purpose from physical podiums, twin load-bearing façade pilasters, single service floors, stepped elevator/service annexes, maintenance balconies, recessed mullioned glazing bays, louvered ventilation banks, loading apertures, rooftop plant rooms, paired HVAC housings, roof caps, and sparse antennas. Elevated conduits gain manufactured couplers and paired support pylons. Expensive small detail is distance-gated so the horizon remains composed and the near field receives the fidelity. Detail variants are keyed from reconstructed world coordinates rather than scrolling positions or pool indices, making every building temporally stable. Roof caps penetrate their parent volume slightly, while penthouses, HVAC units, and antennas occupy separated height bands to prevent depth fighting. Architecture remains rigid at the vertex stage; temporal motion is reserved for materials, atmosphere, and world scrolling. All secondary masses use subdued metal, glass, slats, and shadow rather than emissive trim. Bright analytic signs, luminous façade rails, repeated collars, and dense window grids remain excluded to protect negative space and visual hierarchy. Floor guides, shoulder ticks, directional chevrons, traces, and conduit energy are deliberately restrained. Gameplay forms use a separate directional-lighting shader with explicit player/enemy/effect classes. Enemies use clean dark silhouettes, small seed-linked color accents, restrained shadow lift, and modest rim lighting instead of luminous outer shells, preserving contrast without turning targets into glare. These are generated from geometry and shader math with no external assets.

## 4. Implemented Gameplay Slice

The playable run uses fixed-capacity pools for 48 enemies, 128 hostile projectiles, and 96 combat particles. It performs no gameplay heap allocation and adds no external art or audio assets, keeping the implementation compatible with the 1.44MB target.

- **Flight:** `WASD` or the arrow keys move the probe on X/Y while the camera smoothly follows and leads the same transform.
- **Unified weapon:** Hold and release `J` or the left mouse button. Releases at or below 120 ms fire a narrow tap beam; longer holds acquire up to four targets and release a charge volley.
- **Enemy set:** Drifters strafe and fire, Chasers telegraph a terminal dive, and Splitters fire spreads and fracture into two smaller Chasers.
- **Run director:** Encounter size, spawn rate, and archetype mix scale deterministically with virtual distance.
- **Logarithmic pressure curve:** Difficulty uses a capped `log1p(distance / 350)` curve. Normal enemies establish attack positions progressively farther from the player, shorten their seeded firing cooldowns, and increase projectile velocity as the run advances. Telegraph durations and cooldowns retain hard lower bounds so the late game remains readable.
- **Risk loop:** Boost consumes Core Energy, hits drain Energy and break the combo, and successful attacks restore it. Charge multi-kills provide the largest recovery.
- **Chain weapon evolution:** A live chain now changes combat capability: x4 unlocks dual-link tap beams, x8 raises charged targeting to six simultaneous locks, and x12 enables four-way overdrive forks with stronger primary and charge damage. Taking a hit or letting the chain expire drops the weapon back to Pulse tier.
- **Recursive Core boss:** The first seed-positioned boss arrives after roughly 1,600–2,000 virtual units. Its approach is invulnerable; the player then destroys four rotating firewall nodes, attacks the exposed core, and survives a faster five-shot enraged phase below 46% health. Its runtime-built chassis has a faceted pressure vessel, eight sloped armor petals with inset panels and hardpoints, radial support spars, a segmented maintenance halo, concentric reactor housing, and four mechanically detailed firewall satellites. The armor petals physically spread during exposed/enraged phases, communicating state through silhouette instead of extra brightness. The boss compiles reinforcements during combat so node/add kills can feed the chain weapon tiers. Defeat restores full Core Energy, awards a large score bonus, and schedules a harder seeded return farther into the run.
- **Boss arrival movement:** Encounter entry is a 2.6-second audiovisual transition: normal threats clear, the camera pulls back and widens, cinematic bars frame the lane, a one-shot hexagonal post-process wave crosses the display, and rotating portal rings/spokes compile the core at the horizon. A synthesized 2.55-second riser resolves into a boss arrangement with rolling ghost bass, denser sixteenth-note hats/arpeggios, wider atmosphere, and heavier kick/bass energy while retaining the seed's fixed BPM.
- **Feedback:** Synthesized shot, impact, explosion, and glitch cues are paired with volumetric beam cores, impact flares, projectile plasma shells, velocity-stretched debris, spatial two-axis shockwaves, lock orbits, edge-only damage vignettes, camera kick, and field-of-view boost response. Damage feedback deliberately leaves the center target readable.
- **Silhouette feedback:** The player is a connected, layered twin-engine spacecraft with an armored spine, inset wing panels, canopy, swept wings, vertical stabilizer, nacelles, avionics rails, intake shoulders, trailing control surfaces, reaction-control ports, an underbody equipment rail, and nested boost-length exhaust plumes; weapon upgrades add visible wing emitters and auxiliary cannons. Drifters use flattened segmented saucers with armor petals, a front aperture, and twin engine pods; Chasers use layered interceptor wedges with a cockpit, edge spars, cannons, and paired engines; Splitters use a faceted central cage, segmented brace ring, articulated arms, joints, and axial drone pods. The enlarged boss uses nested armored machinery, radial plates, reactor housings, rotating segmented rings, firewall nodes, rear power modules, link arms, and arena-scale background rings.
- **HUD presentation:** Energy, score, chain weapons, and boss health use compact corner-bracket panels and segmented meters. The control legend fades after the opening seconds, keeping the playfield clean once the player has started moving.
- **Run flow:** Energy reaching zero ends the run; `R` resets the gameplay and procedural distance. `F3` toggles development diagnostics.

## 5. Demoscene Visual Roadmap

- **Implemented this pass:** A raymarched box-fold recursive-cage backdrop (`shaders/backdrop.fs`) now supplies the base sky as a horizon glow, layered behind `DrawDemosceneBackdrop`'s moon/aurora/star/silhouette sprites rather than the previous flat opaque gradient. The boss-arrival hex rift in `shaders/post.fs` gained a bounded raymarched tunnel warp that peaks mid-transition and fully clears before combat starts. Bus conduits in `shaders/tower.fs` now blend scrolling hash-glyphs into the existing traveling pulse using the same `streamCoord`, so conduits read as literal data streams.
- **Already present (no new work needed):** The `uStructureKind == 5` hash-glyph data-rain technique is already applied as a rare decal on flat tower faces (architecture detail mode 14) and elevated conduit supports (infrastructure detail mode 2), kind 6 already renders sine-wave plasma accent panels, and kind 11 already raymarches a fractal core for landmark structures.
- **Deferred ideas (future passes):** A multi-frequency sine plasma layer with domain warping for `floor.fs`, layered alongside the existing moire/hex-micromesh; an interior stepped voxel/checkerboard grid for memory slabs in `tower.fs` via `floor(worldPos * scale)` coordinate snapping; a true procedural 4x4/5x7 bitmapped glyph font for legible hex/alpha strings (currently intentionally hash-faked to keep the project's zero-texture, zero-font discipline).
