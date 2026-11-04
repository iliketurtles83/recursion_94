# RECURSION_94 — Project Design Reference (v2)

> **Status (2026-11-03):** this describes the original game, kept in git
> history (tag `r94-floppy`; paths below are relative to its root). The game is being rebuilt on the imported
> cybertunnel engine. The audio (§6), gameplay (§4, §7) and boss (§8) sections are
> the spec being ported. The environment (§5) and the 1.44 MB target are
> superseded. §9 (Story) is new design for the rebuild. See `AGENTS.md`.

**Developer:** Dream Software
**Target:** 1.44MB Game Development Contest (UPX-packed executable)
**Core Philosophy:** Bare-metal performance, zero external assets, procedural generation, synesthetic flow-state.

## 1. Introduction

RECURSION_94 is a rail shooter in the Rez / on-rails tradition: a 3–5 minute escalating run through a procedurally generated cyberspace trench. Every element the player sees or hears — geometry, textures, music, sound effects — is generated at runtime from a single 32-bit seed. There are no `.png`, `.wav`, `.obj`, or font files anywhere in the build; 100% of the game's content is mathematically derived in RAM or on the GPU. The entire executable must fit within the 1.44MB contest limit after UPX compression, which shapes every design decision: fixed-capacity arrays instead of heap allocation, hand-written GLSL instead of imported shader libraries, and procedural synthesis instead of sampled audio.

One seed compiles one coherent run identity — its own key, tempo, palette, enemy timing, and structural rhythm — so the same seed always reproduces the same "authored-by-algorithm" experience.

## 2. Tech Stack

- **Language / Engine:** C11 on a custom loop built with Raylib 6 (unused subsystems stripped), no scene graph or entity framework beyond hand-written fixed-size pools.
- **Shaders:** Hand-written GLSL 330 (`shaders/*.vs`, `shaders/*.fs`), converted into one generated C translation unit by `tools/embed_shaders.sh` and loaded from memory at runtime.
- **Audio:** A single Raylib `AudioStream` driven by a native callback (`NativeAudioCallback` in [src/audio_synth.c](src/audio_synth.c)) that synthesizes every drum, bass, pad, arpeggio, and SFX sample-by-sample in real time — no sequencer, no samples.
- **Build:** `build.sh` compiles with `-O2 -s -ffunction-sections -fdata-sections -Wl,--gc-sections`, links Raylib statically, and packs the result with UPX. `build_windows.sh` cross-compiles via MinGW.
- **Memory model:** No gameplay-time heap allocation. Enemies (48), enemy projectiles (128), player projectiles (32), and combat particles (96) all live in fixed C arrays in [src/gameplay.h](src/gameplay.h); the environment caps at 1024 active structures in [src/environment.h](src/environment.h).
- **Determinism:** All procedural systems are driven by stateless integer hash functions (`HashUint`, `SeededHash`, `HashFloat`, `PaletteHash` in [src/environment.c](src/environment.c); `SynthHash`/`Hash01` in [src/audio_synth.c](src/audio_synth.c)) rather than `rand()`, so a given seed always reproduces the same run.

## 3. Influence

- **Rez / on-rails shooters:** fixed forward path, escalating audio-reactive intensity, weapon feedback tied to the beat.
- **Demoscene production discipline:** the 1.44MB limit is treated as a generative constraint rather than an obstacle — everything is procedural because nothing else fits, and that constraint becomes the aesthetic (raymarched backdrops, hash-based "data" motifs, CRT-style post effects).
- **Classic trance/EDM structure:** four-on-floor kick, off-beat open hats, sub-dominant bass, filtered supersaw pads, and a fixed BPM per seed instead of a dynamically tempo-shifting soundtrack.

## 4. Gameplay Loop

1. **Seed selection:** the opening screen previews a 32-bit run seed (`Left/Right` ±1, `Up/Down` ±100), showing its minor key, BPM, palette, and demoscene identity before `Enter` compiles the run. `--seed N` / `--boss N` bypass the selector for testing.
2. **Flight & combat:** `WASD`/arrows move the probe on X/Y across a 30-unit-wide lateral corridor (`x=-15..15`) while the camera leads the same transform. The unified weapon (`J` / left mouse) fires one straight ball along the reticle lane on releases ≤120ms, or acquires up to 4 (later 6) locked targets on longer holds for a homing volley.
3. **Enemy pressure:** Drifters strafe and fire, Chasers telegraph a terminal dive, Splitters fire spreads and fracture into two Chasers on death. Regular enemies settle into a close `z=-22..-28` engagement band. Encounter size, spawn rate, archetype mix, and fire cadence scale with a capped `log1p(distance / 350)` difficulty curve, increasing pressure without inflating enemy health or erasing attack telegraphs.
4. **Risk/reward loop:** boosting (`SPACE`) consumes one Core Energy per second, accelerates regular-enemy firing and arms enemy shots for mild bounded homing. Hits while actively boosting drain 20.7 Energy versus 18 normally; a timely maneuver can evade a committed missile. Taking a hit breaks the kill combo; kills restore Energy (charge multi-kills the most). Energy reaching zero from a hit ends the run (`R` resets). There is no separate hull meter.
5. **Chain weapon evolution:** the live kill combo gates weapon tier — tap balls gain speed and damage, x8 raises charged lock capacity to six, and x12 boosts charged damage. Tap fire always remains one straight, non-homing ball. Any hit or an expired chain drops the tier back to Pulse.
6. **Five-round descent:** each round ends in a visually distinct boss: Recursive Core, Iron Bastion, Prism Wraith, Siege Mantis, then the Fractal Leviathan at the enlarged black hole (see §8). Ordinary clears continue in the same world; a Leviathan clear alone collapses and reseeds it.
7. **Endless recursion:** after round 5, the campaign advances to the next recursion depth and begins another harder five-round cycle. Progress reveals more rhythmic/visual layers and pressure, but each seed's BPM stays fixed while that universe is active.

**Pause and exit:** P or the lower-right on-screen pause button freezes gameplay and music. Esc asks for Yes/No confirmation instead of closing immediately; No resumes, while Yes exits. The same Esc confirmation works on the seed selector.

## 5. Procedural Environment

### 5.1 High-Level Design

- **Non-collidable dressing:** the environment is pure background; the CPU tracks no terrain hitboxes. All collision tension comes from enemies, keeping the trance-like flow uninterrupted by cheap wall crashes.
- **Stationary treadmill:** the camera/craft never truly translate on Z; they stay near the origin. Forward motion is driven by a double-precision `virtualPlayerZ` accumulator while visible transforms are narrowed to float only after subtracting their nearby sector origins.
- **JIT horizon:** the full-detail draw distance (384 units, [src/environment.h](src/environment.h)) is a diegetic feature — structures fade in via a `compileScale` factor that ramps from 0 at the far clip to 1 near the player, while sparse terrain and silhouettes continue to 640 units.

### 5.2 Hierarchical Generation Architecture

Environment generation is fully derived from `virtualPlayerZ` through nested layers, computed fresh every frame in `GenerateEnvironmentStructures` ([src/environment.c](src/environment.c)):

- **Sectors** (16m each, `SECTOR_DEPTH`) are the atomic unit of generation; only sectors within the draw distance are emitted.
- **Zones** are 4-sector groups per district, but instead of a fixed rhythm, each district picks one of six `ZONE_PATTERNS` (e.g. `{4,4,4,4}`, `{3,5,3,5}`) that always sum to 16 sectors — giving variable-length zones while keeping lookup stateless (`DescribeZoneForSector`/`DescribeZoneByKey`).
- **Districts** (16 sectors / 256m) carry a `DistrictTheme` (Memory City, Conduit Exchange, Processor Cathedral, Antenna Garden, Open Void) plus smoothly-correlated `density`, `openness`, and `heightScale` derived from 1D value noise (`ValueNoise1D`), so districts form readable visual phrases instead of independent random samples.
- **2D field site placement:** `GenerateFieldSites` places each sector's sites in one correlated pass across the *full* visible width — there is no left/right seam. Sites take stratified X slots between the corridor edge (`CORRIDOR_EDGE_X`) and the far-flank reach, so a sector continuously covers the whole width instead of clustering into two bands; both sides are samples of the same distribution. Height allowance scales inversely with distance from the flight path (low, short profiles near the corridor, megatower-scale mass at the edges). `zone->density` scales the site count, `zone->openness` gates it directly, and `IsBreathingZone` (a local-minimum roll modeled on the landmark spacing rule) drops whole zones into a low-density "breathing room" mode; adjacent zones can never both be breathing, so open stretches stand out against dense ones.
- **Rare features:** `IsLandmarkZone` and `IsGantryZone` use local-maximum spacing (a zone wins only if its hash score beats its neighbors within a window) instead of periodic placement, guaranteeing blue-noise-like rarity without a minimum-distance search.
- **Shape vocabulary per site:** each field site rolls its own profile — a low memory slab, a tall prism tower, or a thin antenna spike with a grounding collar — with width/height jitter derived from the zone hash (`JitterMult`), so a small vocabulary of extruded forms yields varied silhouettes without additional meshes. Landmark zones additionally emit the suspended `STRUCT_LANDMARK` beacon (core monolith, four stabilizer pylons, skybridge feeder) through the same field pass.
- **Priority-tiered emission:** every structure is added through `AddStructurePriority` with an `EmitPriority` (Critical / Structural / Connection / Detail). When the 1024-structure cap is close, lower tiers are dropped first so the corridor spine and landmark geometry are always protected.
- **Continuous voxel landscape:** the old shader floor is replaced by deterministic stepped rock columns across a 640-unit terrain range. A widened low central trough fills the lower frame while correlated terraces rise into broad shoulders, giving the 30-unit flight corridor depth without exposing a flat baseline or the backdrop beneath it.
- **Shared support query:** terrain, flank grounding, low center objects, corridor rails, gantry foundations, and distant silhouettes all consume `SampleTerrainSupport`. Generated structures either emerge from an emitted support cell or are excluded by validation; terrain LOD is not allowed to remove load-bearing cells.
- **Independent horizon silhouettes:** a fixed 128-item far pool places selected grounded towers between the 384-unit full-detail boundary and the 640-unit terrain horizon. These objects skip expensive architecture-detail passes, keeping the distance populated without increasing foreground clutter.
- **Terrain-owned horizon:** the former screen-space city silhouette and axial spire are removed from `DrawDemosceneBackdrop`; only sky effects remain behind the 3D pass, so generated terrain and grounded architecture define the lower frame and skyline.
- **Determinism audit:** `ValidateEnvironmentGenerator` walks 2048 zones for composition invariants (no adjacent breathing zones, landmark spacing), then sweeps six representative seeds across the complete near/far/terrain pipeline. It tracks independent pool peaks, dropped structures, unsupported flank/ravine/far objects, and field X-coverage: a per-side histogram over the corridor-edge-to-far-reach band where each side must populate both the innermost and outermost bins and no bin may exceed 60% of the total — guarding against a regression to the old two-X-band layout. `VALIDATE_GENERATOR=1 ./build.sh` runs this headless and exits with a pass/fail code.

### 5.3 Techniques, Tricks & Effects

- **Chamfered instancing meshes:** two procedural meshes — a chamfered cube (`GenMeshChamferedCube`, octagonal ring cross-section) and a centered 8-sided prism (`GenMeshCenteredPrism`) — replace razor-edged stock primitives so every tower/slab/conduit catches light like manufactured hardware. All structures of a type are drawn in one `DrawMeshInstanced` batch.
- **World-stable procedural detail:** secondary detail (roof caps, façade pilasters, glazing bays, antennas, service floors, HVAC units, etc.) is derived entirely from already-placed structures via `DrawArchitectureDetailBatch`, keyed off reconstructed world coordinates (`worldXKey`/`worldZKey`) rather than array index or scrolling position — so details stay temporally stable instead of re-rolling as sectors cycle through the pool.
- **Distance-gated cost:** expensive small detail (mode ≥ 6) is culled beyond a distance threshold so the horizon stays cheap and composed while the near field gets full fidelity.
- **Shader-side "kind" dispatch:** `uStructureKind` lets `tower.fs` render different surface behavior per structure type from one material — hash-glyph "data rain" decals, sine-wave plasma accent panels, and a fully raymarched fractal core for landmark beacons (kind 11).
- **Raymarched backdrop:** `shaders/backdrop.fs` renders a box-fold recursive-cage sky as a full-screen pass behind the sprite-based moon/aurora/star layer, replacing a flat gradient.
- **Restrained palette discipline:** bright emissive trim, analytic signage, and dense window grids are deliberately excluded; secondary masses stay in subdued metal/glass/shadow so the rare landmark and gameplay silhouettes keep visual priority.

## 6. Procedural Sound

### 6.1 High-Level Design

The soundtrack is not a music file — it is a single native audio callback (`NativeAudioCallback`, [src/audio_synth.c](src/audio_synth.c)) that synthesizes drums, bass, pads, arpeggio, hats, atmosphere, delay, and reverb sample-by-sample, every frame, entirely from oscillators/noise/filters. `GetSynthSeedBpm` maps the seed to one of 16 integer BPM values from 120 through 135; `GetSynthSeedKeyName` selects D Aeolian or D Dorian. Tempo is fixed per seed once the intro ramp completes — progress and boost reveal more layers and density, never a faster or slower clock.

### 6.2 Song Structure

Rather than reacting purely to instantaneous game state, the arrangement now follows a distance-driven song structure over `virtualPlayerZ`:

- **Intro ramp (0–650 units, `SONG_INTRO_END_DISTANCE`):** BPM eases from `ambientBpm` (`baseBpm − 6`) up to the seed's `baseBpm` via a smoothstep (`introEase`); only a sparse downbeat melody plays (`introMelodyStep`, steps 0 and 8).
- **Buildup (650–1250 units, `SONG_BUILD_END_DISTANCE`):** `songBuildProgress` ramps 0→1 and directly drives `targetDrumGate` — a slow-smoothed multiplier (`drumGate`) that scales kick/bass/hat amplitude, so the full beat gradually "arrives" rather than snapping on.
- **Pre-boss hush:** in the final 350 units before the boss's `nextSpawnDistance`, `main.c` computes `preBossHush` (0→1) and both dampens `musicIntensity` and cuts `targetDrumGate` by up to 94%, creating a quiet drop-in moment.
- **Boss arrival drop:** the hush cancels the instant `BOSS_APPROACH` begins, so the arrangement snaps back to full density under the boss riser.
- **Reactive arp:** once the buildup completes, `reactiveArpStep` triggers a denser, intensity-driven arpeggio pattern in place of the ambient intro melody.

### 6.3 Implemented Synthesis Techniques

- **Kick:** sine tone with an exponential 38→150Hz pitch sweep plus a fast decaying noise "click," combined and driven through `tanhf` soft saturation. Gated by `drumGate` so it fades in with the buildup.
- **Bass:** blended saw/pulse/sine oscillator through a 4-pole Moog-style ladder filter (`LadderFilter`/`ProcessLadder`, tanh-saturated stages, 2x oversampled) with a resonance LFO and kick-triggered sidechain ducking (`bassSidechain`).
- **Pads:** a supersaw of `PAD_UNISON_VOICES` (5) detuned saws per chord note across `PAD_CHORD_NOTES` (5) notes, each voice individually panned and independently drifting in pitch, gliding through seeded two-bar modal chord progressions (`SetPadChord`) chosen from six progression templates.
- **Arpeggio:** triangle/FM-style tone stepping through chord tones, with a per-seed modulation ratio (`arpModRatio` from `{1, 1.5, 2, 3}`) and its own resonant filter.
- **Hats / clap:** a TR-909-style inharmonic square-oscillator bank (`MetallicTone`, three detuned ratios) blended with filtered noise for metallic transients instead of plain hiss.
- **Atmosphere:** a decorrelated noise bed run through independent left/right ladder filters for a stereo ambient field.
- **Delay & reverb:** a stereo cross-delay with subtly mismatched left/right tap lengths (`0.750` vs. `0.755` beats) so echoes spread across channels, feeding into a small Freeverb-style tank — four damped feedback combs in parallel (`ProcessComb`) into two series allpasses (`ProcessAllpass`), with a width tap read off the last allpass buffer for stereo spread.
- **Humanization:** each step gets seeded (not real-time random) micro-timing jitter (`stepJitter`) and velocity drift (`hitVelocity`), keeping the grid deterministic per seed while avoiding a quantized/robotic feel.
- **SFX pool:** six-voice pool (`MAX_SFX_VOICES`) covering laser tap/charge, glitch hit, explosion (swept ladder-filtered noise + pitch-dropping sub thump + crackle), power-up chord, and a 2.55-second boss riser (48→720Hz eased sweep with rising noise lift) that resolves into the boss arrangement.

## 7. Gameplay Mechanics & Effects

- **Unified weapon system:** a single input (hold/release) produces two archetypes — one straight tap ball (≤120ms hold) or a multi-target homing volley (longer hold, up to 4–6 locks depending on tier) — rather than separate weapon-select controls.
- **Enemy archetypes:** Drifters (strafe + fire), Chasers (telegraphed dive), Splitters (spread fire, fracture into two Chasers on death) — each with a distinct silhouette (flattened saucer, layered interceptor wedge, faceted cage with articulated arms) and its own accent color so types read apart at a glance.
- **Enemy lifecycle:** regular enemies use four explicit phases (`Approach → Hover → Telegraph → Attack`). Non-Chasers return to Hover after their attack cooldown; Chaser attacks resolve by impact or by leaving the arena.
- **Collision model:** player bolts use swept segment-sphere tests to prevent tunneling. Enemy bolts and Chaser dives use bounded Z plus radial XY tests. Terrain remains non-collidable and no gameplay AABB tree is maintained.
- **Chain weapon tiers:** combo-gated Pulse → Accelerator → Six-Lock → Overdrive progression, resetting on any hit or chain expiry. Tiers increase the single tap ball's speed/damage and later expand or strengthen charged volleys.
- **Logarithmic difficulty curve:** `game.difficulty = clamp(log1p(virtualPlayerZ/350) * scale)` drives formation density, archetype variety, fire cooldowns, and projectile speed. Regular enemies stay in a close engagement band while hard floors preserve readable telegraphs.
- **Feedback layers:** synthesized shot/impact/explosion/glitch cues paired with plasma-shell projectiles, impact flares, velocity-stretched debris, two-axis shockwaves, lock-on orbit rings, and edge-only damage vignettes (center stays readable). Camera kick and FOV widen on boost/hits for kinesthetic feedback without obscuring the play field.
- **HUD:** compact corner-bracket panels and segmented meters for Energy/Score/Chain/Boss health; the control legend fades after the opening seconds.

## 8. Recursive Core and Fractal Leviathan Implementation

Boss encounters are scripted state machines layered on top of the normal enemy pool. The core and its four firewall/horizon nodes are literal `Enemy` slots (`ENEMY_BOSS_CORE`, `ENEMY_BOSS_NODE`) driven by boss-specific position and attack logic each frame. All five rounds have different render-only chassis and HUD names; every fifth encounter also changes gameplay class to Fractal Leviathan.

### 8.1 Spawn & Encounter Setup

- Triggered when `virtualPlayerZ >= boss->nextSpawnDistance` (first encounter ~1,400–1,720 units; later rounds are scheduled roughly 1,270–1,890 units apart).
- `SpawnBoss` clears all active enemies/projectiles/locks so the encounter reads as a deliberate arena, seeds an `encounterSeed` from the run seed and `encounterIndex`, and spawns one `ENEMY_BOSS_CORE` plus four `ENEMY_BOSS_NODE` shield satellites.
- Encounter 5 in each cycle sets `leviathan`, enlarges the core and node hit geometry, moves the arena closer, and uses a much larger nested mechanical chassis against the nearest black-hole state.

### 8.2 Phase State Machine

| Phase | Trigger | Behavior |
|---|---|---|
| `BOSS_APPROACH` | Spawn | Invulnerable 2.6s entrance; camera pulls back/widens, boss glides to its arena position (`Approach` toward Z = −46). No firing. |
| `BOSS_SHIELDED` | `phaseTime >= 2.6s` | Four firewall nodes orbit the core at radius ~8.35×4.65 (elliptical), rotating continuously; boss fires from whichever active node faces the player. |
| `BOSS_EXPOSED` | All 4 `shieldNodes` destroyed | Core itself becomes the fire origin; the round-1 turbine petals visually spread (`plateSpread`). |
| `BOSS_ENRAGED` | Nodes gone and core health ≤46% (≤52% for Leviathan) | Faster rotation, tighter fire cooldown, wider volleys and heavier reinforcement rate. |
| `BOSS_SINGULARITY` | Leviathan only, core health ≤18% | Strongest gravity pull, fastest machinery, ten-shot alternating patterns and three reinforcements per wave. |

- Recursive Core fire scales 2 → 3 → 5 shots. The Leviathan scales 4 → 6 → 8 → 10 and rotates among aimed fans, orbital crossfire and descending convergence formations; its cooldown floor is 0.24 seconds.
- Reinforcements (1, or 2 when enraged) spawn periodically (`reinforcementTimer`, 3.4–3.8s normal / 2.7s enraged) as Drifters/Chasers, feeding kills back into the player's chain-weapon tier during the fight.
- Any boss defeat restores Core Energy and awards a score bonus. Rounds 1–4 reschedule the next encounter in the same universe. The Leviathan alone starts the collapse, advances recursion depth, derives a new seed and restarts at round 1 without a victory ending.

### 8.3 Arrival Transition (Audiovisual)

- Camera: pulls back and widens FOV (+7°) over the 2.6s approach using an eased `sinf(progress·π)` arrival-pull curve, blending its look-at target toward the boss position.
- Visuals: rotating portal rings and radiating spokes compile at the horizon around the core, brightening as the transition progresses; a one-shot hexagonal raymarched tunnel warp crosses the screen in the post-process pass, fully clearing before combat starts.
- Audio: the pre-boss hush (§6.2) cancels the instant `BOSS_APPROACH` begins, and a 2.55-second `SFX_BOSS_RISER` (eased 48→720Hz sweep with rising noise) resolves directly into the boss's higher-intensity arrangement.

### 8.4 Procedural Chassis (No Meshes/Assets)

The five boss bodies are assembled entirely from primitive draw calls (spheres, cylinders, quads) parented to their moving anchor points. `encounterIndex % 5` chooses the visual form and matching HUD name, so identities repeat across recursion depths:

- **Recursive Core:** the original rotating turbine with eight armored petals, exposed actuators, a segmented maintenance halo and concentric reactor housing.
- **Iron Bastion:** a wide rectangular fortress with tall side bunkers, broad roof beam, service louvers and a recessed central target.
- **Prism Wraith:** three long swept triangular blades around a faceted axial hull and small triangular iris.
- **Siege Mantis:** narrow vertical body with two jointed arms and pincer blades.
- **Fractal Leviathan:** an open black aperture surrounded by broken rings and twelve long articulated truss arms; it does not reuse the ordinary turbine core.
- **Firewall node satellites:** each a small mechanically detailed enemy mesh orbiting the core, connected back to the core by faint tether cylinders drawn only while active.
- All boss geometry uses the shared `craft.fs`/`craft.vs` directional-lighting shader with restrained rim lighting — no luminous outer shells — keeping the boss legible against the busy backdrop.

## 9. Story

> **Status:** agreed direction for the rebuild (2026-11-03); scroller, title
> cards and answers implemented in `src/game/story.c`.
> Unlike §4–§8 it is new design, not a description of the legacy game.

### 9.1 Premise

The ship is a program: a small self-replicating intro left on a 1994 floppy by
a demo group that no longer exists. It was written to do one thing: reach the
bottom. Cyberspace is the call stack, each level is a deeper frame, and the
fractal black hole at the end is the **base case**, the point where a recursion
can finally return.

It never returns. Every time the ship reaches the singularity, the black hole
turns out to be another call: the world collapses, reseeds, and the ship wakes
one level deeper. The ending is the loop. The game already plays this way
(§4.6–4.7: a Leviathan clear collapses and reseeds the world and raises the
recursion depth), so the story gives the existing structure its meaning rather
than adding a new one. The seed selector "compiles the run", the recursion
depth counter is the plot's progress meter, and `RECURSION_94` reads as the
program's filename on the disk label.

What the ship is reaching for is its **authors**: whatever is left of the
group, or the last intact sector of the original disk. A final greeting.

### 9.2 Levels and archetypes

Each boss is an archetype the ship has to pass. In sequence they are close to
the hero's journey: threshold, law, shadow, devouring, self. The archetype
should drive each boss's attack style and visual language, not only its look.

| # | Environment | Boss | Archetype | Meaning and play |
|---|---|---|---|---|
| 1 | Server corridor | Recursive Core | The Threshold Guardian | The firewall. Its shield nodes are literally the gate; it tests whether the ship may enter. |
| 2 | Grid city | Iron Bastion | The Ruler | Order, law, the grid. A fortress that wants everything in its place: slow, heavy, walls of fire. |
| 3 | Crystal field | Prism Wraith | The Shadow | The ship's own reflection, rendered from the other side. Mirror attacks, decoys, the player's own patterns fired back. |
| 4 | Vortex | Siege Mantis | The Devourer | Hunger. It eats the tunnel, the light and the ship. Fast and predatory; the last thing before the drain. |
| 5 | Black hole | Fractal Leviathan | The Self | Not an enemy so much as the ship's own shape repeated without end. Beating it is accepting the loop. |

Environment assignments for levels 2–5 are provisional; the five environments
themselves are still to be built or chosen (see `ROADMAP.md`).

### 9.3 Telling it the demoscene way

No cutscenes, dialogue boxes or voice. The story is told only in forms the
scene itself used:

- **Scrolltext between levels.** A sine scroller carrying fragments left by the
  dead group: notes, greets, half-finished thoughts. Five short texts tell the
  whole story in a few hundred bytes. It plays over the level transition, in
  the music's quiet section.
- **Boss title cards** in the HUD carry the archetype:
  `THRESHOLD GUARDIAN // RECURSIVE CORE`.
- **Depth changes the text.** The base texts play at depth 1. Deeper, they are
  rewritten procedurally (hash of seed and depth, never stored variants):
  corrupted characters, stuttering repeats, missing words, and later lines that
  seem to answer the ship. The deeper it goes, the more the authors seem to
  know it is there. The authors stay unseen and never confirm anything.

### 9.4 Draft scrollers (depth 1)

Working text, to be tuned in play. The group name is a placeholder.

1. **Entering the server corridor:** "loader ok. hello, whoever is running
   this. this is our last production. it has one job: go down until there is
   no more down. the firewall will not like it. greets to everyone who ever
   swapped a disk by mail..."
2. **Entering the grid city:** "everything here has a place and a number. we
   never fit in, and neither will you. the bastion keeps the order. order is
   only a loop nobody questions. keep moving..."
3. **Entering the crystal field:** "the wraith looks like you because it is you,
   rendered from the other side. we wrote this part at four in the morning and
   we are no longer sure which of us wrote it..."
4. **Entering the vortex:** "this is where the disk got damaged. sectors
   missing. if the scroller stutters, that is not a bug. the mantis eats
   everything that falls this far. don't stop..."
5. **Entering the black hole:** "base case ahead. we never got to test this
   part. we only wanted to know what is at the bottom. if you get there, say
   hi from us. return 0;"

**On the collapse** (after the Leviathan, before the reseed), one line:
"...it called itself again. of course it did. we wrote it."

### 9.5 Answers (depth 3+)

From depth 3 the scroller starts to answer. Answer lines are spliced into the
corrupted text and are never corrupted themselves: the old notes decay, the
answers arrive clean, in a steadier, whiter voice (barely riding the wave, no
beat lift). They stay ambiguous: they may be a coincidence, a stale note, or
someone at the other end. They never say who, or where. `%d` is the current
recursion depth.

Placement, all from a hash of (seed, depth, text):

- **Depth 3:** the text's own answer, after the last line, like a reply.
- **Depth 4+:** the own answer cuts in at a sentence break.
- **Depth 5+:** also one line from the shared pool, at another break.

Own answers, one per text:

1. Server corridor: "the loader count says %d. it was only ever meant to say 1."
2. Grid city: "you kept moving, like we asked. we did not think anyone would
   listen this long."
3. Crystal field: "we checked again. none of us wrote this part. maybe you did."
4. Vortex: "the missing sectors are moving. they follow you down now."
5. Black hole: "you said hi last time. we think we heard it."
6. Collapse: "...again. we are starting to think it is not the program that
   loops."

Shared pool:

- "is that you? the drive light just came on."
- "this note was not here when we wrote this note."
- "depth %d. we only ever planned for one."
- "we can hear the disk spinning from here. wherever here is."
- "do not look for us at the bottom. we are not at the bottom."
- "keep going. we would, if we still could."
- "every time you come back, someone writes this line again."
- "we are sorry it takes so long. we are sorry it never ends."
