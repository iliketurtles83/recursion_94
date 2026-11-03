# RECURSION_94 — Decisions Log

This log is the source of truth for architectural choices, visual uplift milestones, feature statuses (Confirmed, Provisional, Rejected), and binary size tracking. The 1.44MB (1,474,560 bytes) contest limit applied to entries before 2026-11-03.

## [Story and menu] Scroller, title cards, curve timeline, pause menu

- Date: 2026-11-03. Status: PROVISIONAL: checked in shots and tests;
  hand-play (timing, readability, menu feel).
- Curve timeline (`src/game/timeline.h`, header only): GNU Rocket-style
  tracks of const keys `{t, v, interp}` evaluated at the music clock step.
  Interpolations: step, linear, smooth, ramp (u^2), hermite (Catmull-Rom
  tangents from the neighbouring keys). Clamped before the first and after
  the last key. `game_combat` checks key hits, midpoints and continuity.
- Story (`src/game/story.c`, §9.3-9.4): the §9.4 texts as a sine scroller
  along the bottom (above the meters). A scroll starts on a bar line and
  moves 0.75 font heights per 16th (~25 s for the first text at 129 BPM);
  its wave rolls once a bar and lifts on the beat, and the colour runs
  primary -> secondary lifted toward white midway (a straight RGB blend went
  grey). Level-1 text plays once the controls legend has faded; after each
  boss the next round's text follows two bars later; a Leviathan clear plays
  the collapse line first. No scroll starts during a boss or after game over.
  Until levels 2-5 exist those texts play over the corridor.
- Depth corruption (`story_corrupt`): hash of (seed, depth) only; depth 1 is
  the stored text. Each depth adds glitch glyphs (2.5%/depth, cap 18%),
  stutters ("w-w-will", 5%/depth, cap 28%) and word-length holes (4%/depth,
  cap 25%). Depth 8 is barely readable, as intended. Not done: §9.3's
  "later lines that seem to answer the ship" (needs writing first).
- Boss title card replaces the old `// NAME //` + `ROUND n` flash:
  archetype (`THRESHOLD GUARDIAN`) typed out over 6 steps with a flickering
  glyph cursor, letter spacing closing in on hermite keys, a rule drawing
  out under it, then `// RECURSIVE CORE //` and `STACK FRAME n // DEPTH n`,
  on a dark band; two bars long, all from timeline tracks. The archetype is
  corrupted at depth too.
- Pause menu: Esc or P pauses (Esc used to drop the run to the selector
  without asking). Resume, fullscreen, recompile run, seed select, quit;
  the last three ask "ABANDON THIS RUN?" / "QUIT RECURSION_94?" (Enter/Y
  yes, Esc/N no). Esc in the selector asks before quitting. After game over
  Esc still goes straight to the selector. F11 or Alt+Enter toggles
  borderless fullscreen anywhere (no video mode change); not testable in
  hidden shots, check by hand.

---

## [Ship and trail] Dart with a data trail

- Date: 2026-11-03. Status: PROVISIONAL: checked in shots; hand-play.
- The dart is the game ship (`shipDart=1`); the courier (fighter jet) does
  not suit cyberspace. The original game's probe ship and exhaust were not
  ported (dropped with the old graphics at the engine merge); the code is in
  `git show 1a7c6ca:legacy/src/gameplay.c` (`DrawPlayerShip`, lines 1546-1787).
- Engine trail: cyberspace look only, no fire. `trailData` draws each wake
  with `trail.fs`. It starts inside the dart's engine glow at its width and
  brightness and settles within about 0.03 s into a ribbon (intensity 1.8,
  width 0.035), white -> cyan -> deep blue. Packets run back along it at
  twice the world rate, and the tail dissolves in blocks keyed to emission
  time, so they stay put instead of flickering.
- At cruise the 0.6 s wake is ~19 units long and the camera sits 4.2 behind
  the ship, so only the first metre was ever on screen. `trailStretch=0.2`
  squeezes it along the path toward the ship (sideways history kept), so the
  whole weave shows as a light-snake ending just before the camera, where a
  near fade (1.2-3.2 units) removes it. Sandbox defaults are unchanged
  (sandbox shot byte-identical).

---

## [Movement box] Steering rectangle and barrel-roll dodge

- Date: 2026-11-03. Status: PROVISIONAL: built and checked in shots and
  tests; hand-play and adjust.
- Box: open environments hold the ship's centre in a rectangle in the path
  plane, world units from the centre line: `r94_corridor` uses x +-5.2 (the
  lane is 6.5 wide each side, so the walls cap it), up 5.8, down to the floor
  clearance (-1.6). F4 (or `--steer-box` in shots) draws it with the ship's
  centre dot. The arena now comes from `ct_steer_box` (taller: 26 x 18.5
  arena units instead of 26 x 17).
- Hand-play: top and bottom edges are right; the side margins were too wide
  (a 4.2 attempt went the wrong way). Half width 5.2 -> 6.4 (arena 32 x
  18.5): at the side limits the dart sits at about 5% / 93% of the screen
  width. Wall pillars start at 7.5 (lane 6.5 + clearance 1); the dart's
  wingtip reaches 7.0, so it never clips them. The F4 overlay is
  drawn in the game plane, so its edges slide with the camera and are not all
  on screen at once; it shows the flight limit, not a screen frame.
- Camera: `camFollowLateral` 0.85 -> 0.55. At 0.85 the camera chased the ship
  so the box slid off screen and the ship barely moved in the frame; at 0.55
  the box roughly fills the screen and the ship stays visible in all four
  corners (tightest at the bottom, ~40 px; up 6.0 hit the top edge and the
  score, so 5.8).
- Roll: Q/E turns a full 360 in 0.7 s, kicks 18 u/s sideways (about 2.4 m of
  dash, still held by the box), ignores hits for the whole roll and then
  cools down 0.35 s, so rolls can't chain into permanent invulnerability. The
  camera now leans into the roll and back (half a sine, `cameraRollFollow` x
  180 deg peak) instead of following 12% of the full turn and springing back
  from 43 deg when the roll ended.

---

## [Boss push-in] Camera tightens on the bars

- Date: 2026-11-03. Status: CONFIRMED by hand-play: the push-in feels right
  and the release after the kill is smooth. Keep the current values.
- Replaces the original pull-back idea. When the boss spawns, the push waits
  for the next bar line, then narrows the engine's `fovBase` one notch per bar
  for four bars (65 -> 52 with `r94_corridor`), each notch eased over the
  bar's first beat. It holds for the fight and releases over about 2.5 s after
  the kill; the preset value is restored on exit. At 20 s with `--boss` the
  core reads about a third bigger. The ship grows too and reaches the
  lower-left HUD at the edge of its weave: check in play.

---

## [Music clock] Gameplay and flashes locked to the song

- Date: 2026-11-03. Status: Step 6 CONFIRMED by tests and shots; not yet
  heard or hand-played.
- The synth counts triggered steps and publishes its position in 16ths
  (`musicStep`) with its telemetry. The game's `MusicClock` follows it minus
  one callback of buffered audio, easing out the per-callback stair steps and
  never running backwards except on a restart. Without audio it free-runs at
  the seed's BPM, so `--game-shot` stays byte-identical.
- Gameplay gating: a due timer fires on the first grid crossing within half a
  division of zero, i.e. it rounds to the nearest grid line instead of always
  waiting, so rates hold: 120 s headless runs give formations x0.97, enemy
  shots x0.98, boss-fight shots x1.02 against untimed, with zero off-grid
  events. Beats for formations and boss reinforcements; 16ths for boss volleys
  and enemy shots (telegraph end).
- Formations on phrase boundaries (the plan) was dropped: four bars is about
  7 s, longer than every spawn interval.
- Visuals: boss nodes and tethers flash on the downbeat; HUD meter segments
  pulse each bar. The audio golden hashes are unchanged.

---

## [Demoscene notes] Music clock, curve timeline, SDF bosses

- Date: 2026-11-03. Status: PLANNED (roadmap), nothing implemented yet.
- Source: review of a modern-demoscene note against the codebase. The synth
  already exceeds the note's softsynth (ladder filter, seeded noise, lock-free
  queues); size-coding tricks no longer apply.
- Accepted: sync gameplay and visuals to a sample-accurate music clock
  (Step 6), then a Rocket-style curve timeline on top of it for the
  presentation layer.
- Accepted: raymarched SDF forms for some later bosses, so not every boss is
  triangles and ribbons; the SDF cheap tricks are noted in the roadmap.
- Boss arrival: a slow push-in timed to the music is preferred over a plain
  pull-back.
- To test by ear: resetting the noise seed on every snare hit.
- Seed line and control hints are hard to read in shots; accepted for now,
  the HUD gets its own pass later.

---

## [Corridor song] First level song on the synth

- Date: 2026-11-03. Status: Step 4 CONFIRMED by offline measurement and
  determinism tests; not yet heard.
- New `SynthSong` layer (`src/game/songs.c`, ~40 bytes of pattern data per
  song). The seed keeps BPM, mode and arp timbre, so the selector's key and
  tempo stay true. The song fixes an eight-bar progression (i i VI VII iv VI v
  VII in minor), a rolling octave bass and a half-time break bass with slides
  and accents, a two-bar chord-relative lead motif (replacing random lead
  degrees), and a 64-bar section plan: A groove 16, B with lead and open hats
  16, kickless break 8, B 16, turnaround 8. A boss holds B.
- Structure fix: the original build reached the full beat at 1,250 units and
  the pre-boss hush began around 1,150, so the full groove was never heard
  before the first boss. The corridor song builds over 300 to 750 units.
- Mix fix, measured with the new `song_render` tool (per-layer RMS): in the
  original balance the sub was the loudest layer (-16 dB, above the kick),
  hats and snare sat about 30 dB under the kick (-47 / -52 dB, effectively
  silent), pad and arp were buried (-43 / -39 dB). Song trims bring the full
  groove to kick -19, bass -22, sub -24, pad -33, arp -31, lead -27, hats -31,
  snare -30 dB; peaks stay under -2.7 dBFS, nothing clips.
- The original arrangement is unchanged without a song (golden hash intact);
  the corridor song has its own golden hash in `game_audio`.
- Dorian seeds turn the VI chord into a diminished one; left as seed flavour
  pending a listen.

---

## [Boss port] Recursive Core in the corridor

- Date: 2026-11-03. Status: Step 3 CONFIRMED on Linux by tests and scripted
  screenshots of every phase; not yet hand-played or heard.
- Bosses enabled in the game. Boss logic unchanged; its geometry now scales
  with the arena width (centre line 0.2 above mid-height, node orbit radii,
  sway, reinforcement and Leviathan pattern offsets). The original box gives
  the original numbers, so the 18 `game_combat` checks still pass.
- Engine slowdown: `baseSpeed` is scaled by the original travel speed over
  cruise (`GetGameplayCruiseSpeed`), so the engine eases to about a quarter
  speed over the 0.75 s approach and holds there for the fight.
- Presentation: turbine of eight pitched kite petals around three nested,
  counter-spinning octahedra (the outer one matches the core's hit radius;
  the innermost is the eye, dim while shielded), a segmented halo, a hex
  shield while nodes live, tethers to the four firewall nodes (octahedron,
  inner cube, bracket square, drawn at their hit radius). Exposed: petals
  spread. Enraged: rotation, colour heat and flicker. Arrival: portal rings
  and spokes compile around the core over the 2.6 s approach. Defeat:
  expanding rings and spokes.
- HUD: boss name, core integrity meter (arrival progress while compiling),
  phase label (COMPILING / FIREWALL xN / CORE EXPOSED / OVERLOAD) on a dark
  panel; arrival title card and CORE COLLAPSED card.
- Music: boss layer by phase, pre-boss hush over the last 350 units, boss
  intensity bonus, riser and collapse SFX, all as in the original main loop.
- `--boss` starts runs 40 units short of the first boss.
- Not ported: the original camera pull-back and FOV widening on arrival, and
  the post-process hex rift (the portal rings stand in). The other four boss
  chassis. Leviathan gravity.
- Game shots before the boss are byte-identical to Step 2; sandbox shots
  are byte-identical to Step 0.

---

## [Combat port] Original combat on the engine, in neon vector style

- Date: 2026-11-03. Status: Step 2 CONFIRMED on Linux by tests and scripted
  screenshots; not yet hand-played.
- `gameplay.c` moved from legacy (history kept). Its rendering, camera and
  keyboard reads were removed; logic is unchanged. `UpdateGameplay` takes a
  `GameplayInput` (ship position from the engine, fire/beam/bomb buttons).
- Arena mapping: uniform 0.4 world units per arena unit, riding the path.
  The corridor box becomes an arena about 26 x 17 (original: 30 x 6.2).
  Formation placement is bounds-relative and reproduces the original numbers
  in the original box. The original VALIDATE_GAMEPLAY suite is now the
  `game_combat` ctest: all 18 checks pass.
- Presentation (`combat_draw.c`): wireframe polyhedra with dark bodies
  (drifter octahedron + ring, chaser dart, splitter nested cubes), light
  ribbons for shots, debris streaks and shockwave rings, telegraph and lock
  rings, segmented beam lance and bomb rings. One embedded shader; vertex
  alpha encodes HDR gain for the bloom. Objects compile in over the spawn band.
- HUD: energy and chain-link meters, score, weapon tier, chain, lock count,
  boost warning, edge-only hit flash, game over/restart.
- Framing: the mission camera made the arena a narrow cone far ahead. The
  game preset now uses courierScale 0.55, camDistance 4.2, camHeight 1.5,
  lookAhead 16, camFollowLateral 0.85.
- Music now follows the original mix (difficulty, chain, boost; glitch on
  hit) and the original distance accumulator, so the song structure matches.
- Bosses disabled until Step 3. Leviathan gravity not applied yet.

---

## [Game shell] Seed selector, corridor run and ported synth

- Date: 2026-11-03. Status: Step 1 CONFIRMED on Linux; audio verified only
  by a crash-free 8-second run (not listened to).
- `src/game/`: seed selector (legacy `DrawSeedSelector`, Left/Right +-1,
  Up/Down +-100) and a run loop on the engine API. The game is the default
  mode; `--sandbox`, `--shot`, `--bench` and the param tools keep the harness.
- Synth ported unchanged (`git mv`, history kept). Boost adds 0.48 music
  intensity as in the original. Elapsed time x 30 units/s feeds the song
  structure, preserving its original intro/buildup pacing (the engine's
  32-65 u/s would otherwise rush it). The PCM hash test is now the `game_audio`
  ctest and passes (`a2b83674276419d1`).
- Engine: `ct_set_beat()` + `beatBloom` parameter (default 0). Sandbox shots
  are byte-identical to Step 0. `presets/r94_corridor.ini` = corridor look +
  the mission flight/camera/ship tuning + `beatBloom=0.35`.
- `--game-shot S` gives reproducible game frames (no audio thread).
- Not yet: pause menu with quit confirmation, fullscreen toggle, ship choice.

---

## [Unified engine] RECURSION_94 rebuilt on the rec_tunnel engine

- Date: 2026-11-03. Status: Step 0 CONFIRMED on Linux (GCC, Raylib 6.0).
- Decision: one game. The rec_tunnel sandbox engine (path, chunk streaming,
  flight/camera, HDR pipeline, environments) becomes the base. The original
  synth, combat logic, bosses and seed identity are ported onto it. The old
  environment and ship/enemy/shot graphics are dropped. Planned structure:
  five levels, each with its own environment, song and boss. First milestone:
  a server-corridor vertical slice.
- The 1.44 MB limit is retired (contest missed). Procedural and demoscene
  spirit stays; generated assets are allowed where they earn their place.
- Imported rec_tunnel's `sandbox/cybertunnel` at the repo root with history
  (46 commits). Reference video, keyframes and screenshots were filtered out
  (repo `.git` is 6.5 MB, not +118 MB); they remain in the rec_tunnel repo.
  Original code moved to `legacy/`, unbuilt.
- Removed DATA // RUN (an unapproved game layer by another contributor) and
  its font and tests. Kept `presets/mission*.ini`: they hold the flight
  tuning (thrust vectoring, courier scale) the slice builds on. Kept the
  courier jet (by the same contributor, who approved reuse) for a future
  ship selection.
- Raylib 5.5 -> 6.0 via FetchContent. Warning-free build, `ctest` 3/3 pass.
  `--shot` is byte-reproducible on 6.0. Against 5.5, about 0.2-0.3% of pixels
  differ in vortex/city/corridor/crystal shots, mostly by 1 level; larger
  differences (max 69) are confined to the jet's mipmapped texture.

---

## [Runtime atlases and audio golden] Generate instead of embed

- Date: 2026-11-02. Status: CONFIRMED on Linux (GCC, static Raylib 6.0, UPX 5.2.0).
  Windows release not rebuilt in this pass (MinGW toolchain unavailable).
- Moved the `tools/bake_detail.c` / `tools/bake_facade.c` recipes into
  `src/material_atlas.c`, run once at startup into static BSS buffers. Removed
  `src/detail_atlas.h`, `src/facade_atlas.h` and both bake tools.
- Output is byte-identical to the former embedded arrays at -O0 and -O2
  (detail FNV-1a `456b2ed1`, facade `13bcae23`). `powf(broad, 2.0f)` became
  `broad * broad` to avoid libm variance across glibc/MinGW; bytes unchanged.
  `VALIDATE_GENERATOR` now also checks both atlas hashes.
- Size, Linux unpacked: 699,712 -> 294,208 bytes; xz -9e proxy for UPX LZMA:
  465,704 -> 116,904 bytes. Real Linux release (`./build.sh`, `upx --best`):
  1,230,908 -> 798,620 bytes (432,288 saved; headroom 243,652 -> 675,940).
  Generator, gameplay and audio validation all pass with the release toolchain.
- Audio golden: synth output has been `a2b83674276419d1` since `57966d2` (the
  last synth change). That commit stored `e5b8d4ab7e1fd136`, which its code never
  produced. GCC/glibc and MinGW agree on `a2b8…`. Golden updated; repeat-run
  and seed-difference checks pass at -O0/-O2/-O3. This resolves the long-standing
  "pre-existing audio golden-hash mismatch".

---

## [Tuned boost-risk combat] Energy health restored, guidance softened

- Date: 2026-09-16. Status: CONFIRMED for Windows x64.
- The separate five-hit hull was rejected. Core Energy is again the sole
  health meter: zero Energy from a hit ends the run. Normal hits cost 18;
  hits while actively boosting cost 20.7 (15% more). The existing post-hit
  invulnerability and boss Energy refill remain.
- Continuous boost Energy drain is reduced from 10 to 1 per second. In a
  no-input seed-94 run, the prior drain left the ship nearly empty before the
  first missile; smaller hit damage alone could not prevent that one-shot.
- Boosted enemy shots still acquire a persistent homing flag, but lateral
  acceleration is 18 units/s² (formerly 44), lateral speed caps at 8
  (formerly 19), lead is reduced, guidance commits at 4 units, and collision
  radius matches ordinary shots. Regular enemies still fire more often while
  boosting; boss fire remains unchanged.
- HUD warns `BOOST // GUIDED FIRE // HIT DAMAGE +15%`. Tests check ordinary
  and boosted damage, low-Energy death, and five successive boosted hits
  from full Energy with invulnerability reset between hits.
- In the seed-94 natural-spawn test with boost held and no evasive input,
  four landed hits leave 3.4 Energy and the fifth ends the run at Z=544.6.
  This scenario does not guarantee a fixed hit count for all play styles.
- Release: **992,256 UPX-packed bytes**, 482,304 bytes below the strict
  1,474,560-byte floppy limit. UPX integrity, Windows gameplay validation,
  and OpenGL HUD render pass. EXE SHA-256:
  `7a6bb7ee3b20472b6688d518da494d6cb6f2f3d9e7a527e5fb5d3faae9190e39`.

---

## [Boost-risk combat] Guided enemy fire and fivefold hit damage (superseded)

- Date: 2026-09-16. Status: SUPERSEDED by tuned boost-risk combat.
- Existing shots acquire a persistent `homing` flag when the ship boosts;
  newly fired shots are armed immediately. Guidance leads current lateral
  velocity by 0.16 seconds, clamps aim to the flight corridor, and limits
  lateral acceleration to 44 units/s² and lateral speed to 19 units/s.
  Steering ends within 2.5 units of the player, creating a narrow committed
  trajectory that a hard lateral reversal can cross. Guided shots have a
  roughly 1.05-unit collision radius versus 0.85 for ordinary fire. Projectile
  collision is swept across each frame so the faster shots cannot tunnel.
- Regular-enemy hover firing timers drain at 2x during boost; their reset
  cooldown is 0.62x. Existing faster formations, shorter telegraphs and shot
  speed under boost remain. Boss firing is unchanged because boosting is
  already disabled during boss encounters. No pool sizes or allocation rules
  change.
- The same `HitPlayer` path now applies 18 Core Energy normally and 90 while
  boosting. Its existing invulnerability, combo reset and effects remain.
  Homing shots have an amber core and the HUD explicitly warns of 5x damage.
- Balance check, seed 94: a headless natural-spawn run with held boost and no
  evasive input takes a fatal boosted hit at forward Z=343.7, well before its
  first boss. A single-shot maneuver fixture catches continuous straight
  lateral flight but permits a timed full-direction reversal. These are
  deterministic test scenarios, not difficulty guarantees for all players.
- Windows generator and gameplay validations pass, including new hit-damage,
  homing-persistence, terminal-commit, increased-fire-rate and swept-hit
  assertions. Two 210-frame production-loop weapon/audio smoke runs and the
  pause-menu regression fixture pass. NVIDIA/OpenGL 3.3 boost rendering passes
  without GL errors.
- Release: **992,768 UPX-packed bytes**, up 2,048 from the five-boss edition;
  **481,792 bytes** remain below the strict 1,474,560-byte limit. UPX 4.2.4
  integrity passes. EXE SHA-256:
  `b6aa0aa03c843366de144d6e40ebc68be55d6c9e9cd06aab3417aa7469cfd348`.
  The pre-existing audio golden-hash mismatch is unchanged.

---

## [Five boss identities and pause menu] Distinct silhouettes and deliberate exit

- Date: 2026-09-16. Status: CONFIRMED for Windows x64.
- The boss index modulo five selects separate render assemblies: round 1 retains
  the turbine Recursive Core; round 2 is the broad, layered Iron Bastion;
  round 3 is the triangular Prism Wraith; round 4 is the jointed-claw Siege
  Mantis; round 5 replaces the shared core with an open-aperture Fractal
  Leviathan. Boss HUD titles match. The modulo mapping repeats at every depth.
  Gameplay hit targets, nodes, health, attack timings and pools are untouched.
- Raylib's default Escape-to-close key is disabled. Esc from gameplay or seed
  selection opens a Yes/No confirmation. No, N or Esc resumes; Yes, Y or Enter
  exits. P and the lower-right mouse button open pause; Resume/P/Enter resumes,
  and Quit opens the same confirmation. Gameplay, camera, world motion,
  transitions and music stream freeze while a menu is open.
- Release: **990,720 UPX-packed bytes**, up 3,584 from the side-detail edition;
  **483,840 bytes** remain below the strict 1,474,560-byte floppy limit.
  UPX 4.2.4 integrity passes. EXE SHA-256:
  `ca801d07085f3f5b2b63c185f7a4084943217b0e9f8d77827bb4d90f79700713`.
- Verification: five individual boss captures on NVIDIA/OpenGL 3.3; all render
  cleanly with distinct silhouettes. Windows generator and gameplay validations
  pass; two 210-frame production-loop runs fire beam/bomb/pulse and exit cleanly.
  A hidden-window input fixture exercises keyboard and mouse pause/resume,
  Esc-to-confirm, No-to-resume and Yes-to-exit, both from gameplay and selector.
  Game time remains unchanged through pause/confirmation intervals, and audio
  pause/resume calls match the state transitions. Existing audio golden-hash
  mismatch remains unrelated and unchanged.

---

## [Side-environment detail] Stamped facades and upper wall assemblies

- Date: 2026-09-16. Status: CONFIRMED for Windows x64.
- Requested at least 100,000 bytes of useful visual improvement to the side
  environment. The new packed EXE is **987,136 bytes**, up **133,632 bytes** from
  the 853,504-byte campaign release, with **487,424 bytes** of floppy headroom.
  UPX 4.2.4 `--best --lzma` integrity and strict size gate pass.
- `tools/bake_facade.c` deterministically bakes a 192x192 RGBA atlas into the
  included `src/facade_atlas.h`: 147,456 raw bytes. Its four consumed channels
  store stamped depth, roughness, oxidation and exposed metal/edge wear. Plate,
  louver, hatch/grip and perforated-casing tiles vary by structural material.
  Generated raw-data FNV1a: `13bcae23`; header SHA-256:
  `4b5ea669feed56d47e50cf09e5ee3a6a81e43fcc262d963d4f759c8b0be89e2c`.
- Tower/service shader applies filtered, non-emissive microrelief and restrained
  paint variation to side facades. Tall structures gain broad three-section
  upper wall armor, structural rails and fasteners. No extra glow, bloom, runtime
  asset dependency, gameplay-time allocation, collision or spawn changes.
- Facade texture is uploaded once with eight mip levels, bound to the tower
  material and to texture slot 2 for the persistent service batch. Environment
  teardown clears material references and unloads it exactly once.
- Windows validation: generator 2,048 zones, zero violations/drops, peaks
  790 structures / 891 terrain / 9 far. Gameplay checks, including endless
  round/Leviathan/recurse, all pass. Two 210-frame production-loop smoke runs
  (`--seed 94`, `--boss 2`) fire pulse/beam/bomb and exit cleanly. Observed
  service peaks 4,236 and 3,974, below the 8,192 cap. NVIDIA/OpenGL 3.3
  close-up, normal-flight and side-on scenes compile/render without GL errors;
  resize, renderbuffer fallback, environment recreate/reseed, and seeds 0/94/1337
  also pass. An identical frozen scene repeated byte-for-byte.
- EXE SHA-256:
  `9bd1e40eb2f3058eb0f4e142a152b5a376b42295ad7eacabd6099b5696247af2`.
  The pre-existing audio golden-hash mismatch is unchanged; audio source was
  not modified in this release.

---

## [Endless singularity campaign] Five-round descent and Fractal Leviathan

- Date: 2026-09-11. Status: CONFIRMED for Windows x64.
- Campaign structure:
  - `encounterIndex % 5` defines rounds 1–5; integer division defines recursion
    depth. Rounds 1–4 clear into the next combat stretch without changing seed.
  - Round 5 is always the Fractal Leviathan at maximum black-hole proximity.
    Its defeat alone starts the existing 5.2-second collapse/reseed transition.
  - After reseeding, the HUD returns to round 1 at the next depth. Score and chain
    links persist. There is no victory/end state; energy depletion remains the only
    run-ending condition, and `R` still restarts from depth 1.
- Leviathan encounter:
  - 12.8-unit target chassis with three segmented outer shells, 12 articulated
    truss arms and enlarged four-node horizon anchors, rendered procedurally.
  - Health starts above 180 and scales sublinearly with recursion depth. The boss
    alternates aimed fans, orbital crossfire and descending convergence patterns.
  - Enraged begins at 52% health; a new Singularity phase begins at 18%, firing
    ten-shot volleys as fast as 0.24 seconds while its gravity field pulls the craft
    toward the moving core. Reinforcements increase to three per wave in this phase.
- World progression:
  - The analytic black hole scales from 0.48x in round 1 to 1.02x in round 5,
    with nested warped filament shells and restrained lens darkening. Its center is
    held high behind the architecture and its accretion/corona contrast is reduced,
    preserving depth without competing with the combat lane. It resets in apparent
    distance after each Leviathan while depth adds a small persistent scale.
  - Permanent HUD telemetry exposes depth, round and event-horizon range.
  - Loop pressure now rises beyond the former three-boss cap, while logarithmic and
    explicit caps keep long-running sessions bounded. Later depths may add a fifth
    regular enemy to a formation.
- Release:
  - **853,504 packed bytes** (833.5 KiB), 3,072,000 bytes before UPX.
  - Increase from the surroundings/weapons edition: **5,120 bytes**.
  - Floppy headroom: **621,056 bytes**. UPX 4.2.4 integrity passed.
  - EXE SHA-256: `6584c5dbdf4b05df236feeb36d8021fcd08fcad004547d55ab7f5581c9da4e83`.
- Verification:
  - Gameplay validation passes old weapon/economy/speed checks plus ordinary-round
    continuation, Leviathan classification/health, Singularity entry and depth reset.
  - Environment validation: 2,048 zones, zero repeat/spacing/coverage/clearance/
    support violations and zero dropped structures; peaks remain 790/891/9.
  - NVIDIA/OpenGL 3.3 round-1, Leviathan and Singularity captures compile and render
    cleanly. Warm-frame means were 2.108 ms (round 1) and 2.838 ms (Leviathan) in
    the frozen probe; these are not end-to-end or cross-hardware guarantees.
  - Two 210-frame Windows production-loop smoke runs (normal and direct-boss) pass:
    beam=1, bomb=1, pulse-peak=20, audio initialization/cleanup, exit 0.
  - Audio source is unchanged. The pre-existing golden mismatch remains observed
    `a2b83674276419d1` versus stored `e5b8d4ab7e1fd136`.

---

## [Surroundings and weapons] Dense industrial scene and layered attacks

- Date: 2026-09-11. Status: CONFIRMED for Windows x64.
- User requested much more detail on surroundings, pulse and beam attacks, with
  at least 0.10 MB of useful additions (corrected from an initial 10 MB typo).
  Retained the earlier strict floppy-size requirement; no artificial padding.
- New surroundings:
  - Supported twin coolant trunks, five flange/support sets per trunk, handrails,
    grated equipment cabinets, heatsinks, door recesses, handles and fasteners.
  - Building-front and corridor-facing ducts/collars, ladders, grated maintenance
    landings, guardrails and upper fan/louver bays. Stable seeded world identities.
  - Floor access covers, recessed circular seals, handles and muted caution paint.
  - 256x256 RGBA atlas, 262,144 raw bytes: height, roughness, oxidation and wear.
    Baked reproducibly from `tools/bake_detail.c`, embedded as `src/detail_atlas.h`.
    All channels affect shading; it is not unused data or executable padding.
    Runtime-generated mipmaps give nine levels (349,524 bytes including base).
  - The atlas is shared by terrain/tower materials. Startup upload, single-owner
    cleanup, no per-frame allocation. Disabled-atlas capture differs visibly.
  - Hardware cap increased from 2,048 to 8,192: 512 KiB static CPU staging and
    512 KiB persistent GPU transform buffer. Gameplay/environment generator pool
    sizes unchanged. No additional environment light sources or bloom increase.
- New attacks, without combat-rule changes:
  - Pulse/charged shots: tapered cores, four split fins, three annular shells,
    three velocity-aligned helical wakes and subtle procedural shell variation.
  - Beam: six braided strands, 19 moving compression collars, narrow core and
    translucent sheath, animated striations, muzzle iris and 24 short sparks.
  - Bomb: four ripple fronts, three elevated belts and 48 radial fragments.
  - Beam/bomb moved out of opaque armor rendering into the depth-tested,
    non-depth-writing additive pass; the old solid opaque beam wedge is gone.
    Per-object material/origin changes flush the batch. No damage, link cost,
    hitbox, speed, lifetime or audio-source changes.
- Release:
  - **848,384 packed bytes** (828.5 KiB), versus previous 623,616.
  - Increase: **224,768 bytes**. Floppy headroom: **626,176 bytes**.
  - Unpacked release: 3,060,736 bytes. UPX integrity and strict size gate passed.
  - MinGW GCC 13-win32, static Raylib 6.0, O2/section GC, UPX 4.2.4 best LZMA.
  - EXE SHA-256: `dd086e10760407ca2f9d4b100567124065a26338eddbafdc0365629c76596fd3`.
  - Atlas-header SHA-256: `5c2407646024c53039386f0c68e0d0bf623e0a4c2721da60e9c30bd0d75708ea`.
    A second bake reproduced the header byte-for-byte; raw-data FNV1a `456b2ed1`.
- Verification:
  - Windows/Linux project sources compile without warnings. Shader compile/link
    and rendering pass on NVIDIA RTX 5060/OpenGL 3.3 and Mesa/llvmpipe.
  - Windows generator: 2,048 zones; zero violations/drops; peaks 790/891/9 unchanged.
  - All eight gameplay checks pass. Audio golden-hash mismatch still identical:
    observed `a2b83674276419d1`, stored `e5b8d4ab7e1fd136`; source unchanged.
  - Two input-driven production-main smoke fixtures (normal/boss): 210 frames
    each, beam=1, bomb=1, pulse-peak=20, WASAPI initialization/clean shutdown, exit 0.
    Fixture injects key states and link credit to exercise real attack code; it
    is a separate executable, not the shipped release, and uses no desktop UI input.
  - Sampled render seeds: 94, 0, 1337, 4294967295. No hardware flight-clearance QA
    errors or GL errors; visible service-instance counts 3,222–4,950, below cap.
  - Beam early/middle/late phases, normal/charged pulse close-ups, bomb, boost,
    boss arrival, building close-ups and ordinary combat checked.
  - Native warm-frame means (120 frames, six warm-up): combat 2.178 ms, beam
    2.618 ms, pulse 1.840 ms, charged pulse 2.064 ms, bomb 2.465 ms, building 2.133 ms.
    Full 32-player-projectile/128-enemy-projectile/96-particle fixture plus both
    specials: 8.178 ms. These frozen renderer probes exclude screenshot encoding;
    they are not end-to-end FPS guarantees or measurements on other hardware.
  - Resize 960x540 -> 1600x900 -> 1280x720 and two unload/reload/reseed texture
    lifecycle cycles end in the exact same PNG as the untouched beam scene.
  - Repeated-state PNG SHA-256: `d6c5b0a5eb5e0d54f70ed28a3f6757bd4640f3141a2d7e805614ede6b594be92`.
  - Forced standard-depth fallback renders cleanly; actual hardware allocation
    failure was not available to reproduce. Atlas-disabled rendering also tested.

---

## [Mechanical detail] Constructed geometry and contact occlusion

- Date: 2026-09-11. Status: CONFIRMED for the Windows x64 release.
- User direction: significantly more detail again; retain the no-extra-neon direction.
- Rendering changes:
  - Boss pressure shell flattened along depth, preserving XY coverage and exposing
    the formerly hidden front assembly. Capped cylinders replaced by open annular
    housings with inner walls, turbine vanes, a cavity backplate and bolt heads.
  - Radial telescopic actuators and smaller open-ring node housings. Existing lens,
    node, projectile and navigation lights retained; no new light sources.
  - Player intake housings have bevels, dark mouths, raised cooling ribs, bronze
    service pipes and wing armor pads. Prior procedural fine finishes remain.
  - Buildings gain bounded near-field service racks, pipe runs, radiator fins,
    hoods, alternating diagonal braces and roof plant. Recipes use seeded stable
    world identity and stay outside the player corridor; they are non-colliding.
  - Sampleable scene depth supports a 12-tap hemisphere contact-occlusion pass,
    with depth-reconstructed normals, distance/radius limits and emissive preservation.
    This is approximate screen-space shading, not global illumination or shadow maps.
- Resource design:
  - Hardware cap: 2,048 transforms, 128 KiB static CPU staging and 128 KiB persistent
    GPU transform buffer. No enlarged gameplay pool or new per-frame allocator call.
  - Specialized untextured GLSL 330/VAO draw avoids Raylib's temporary instancing
    allocations and restores attribute divisors/state after drawing. Pre-existing
    general instanced batches elsewhere are unchanged.
  - Depth texture replaces the standard depth renderbuffer; no normal G-buffer,
    external texture or mesh asset. Raylib framebuffer teardown owns depth deletion.
  - Window-size changes recreate targets/shaders; FOV/clip uniforms follow the camera.
    Failed sampleable-depth setup falls back to the standard target with contact
    shading disabled. Off-screen/hidden geometry cannot contribute occlusion.
- Release:
  - Windows x64 MinGW GCC 13-win32, static Raylib 6.0, O2/section GC, UPX 4.2.4.
  - **623,616 packed bytes** (609 KiB); 2,387,456 before compression. UPX integrity passed.
  - Previous edition: 616,960 bytes. Increase: **6,656 bytes**. Headroom: **850,944 bytes**.
  - EXE SHA-256: `dbd2376e126e701f488b05a239db187cafe424f5248155379bee18d36d360e53`.
- Verification:
  - Windows/Linux builds: no project-source compiler warnings.
  - Native generator: 2,048 zones, zero violations/drops, peaks 790/891/9 unchanged.
  - Native gameplay: funds, beam, loop, hit, bomb, boss-cap, link-cap and speed pass.
  - Two production-main smoke runs (normal and boss entry):
    180 frames each, WASAPI initialization and clean shutdown, exit 0. The wrapper
    adds hidden-window/automatic-exit behavior; it is not the distributed EXE.
  - NVIDIA RTX 5060/OpenGL 3.3: combat, ship/boss/architecture close-ups, boost,
    banked camera and boss arrival, with seeds 94, 1337, 1 and 3735928559. No warnings.
  - Mesa/llvmpipe: final shaders compile/link and render with sampleable depth.
  - Resizing through 960x540, 1600x900 and back to 1280x720 recreates the scene
    correctly; final PNG is byte-identical to the unresized ship scene.
  - Forced standard-renderbuffer fallback renders and unloads cleanly; its PNG
    matches contact shading explicitly disabled. Hardware rejection itself was
    not available to reproduce on the tested GPUs.
  - Repeated seed/state and persistent-buffer A/B captures are byte-identical.
    Combat PNG SHA-256: `70370dab4827ac85b024cef62124a09a62d0e5b177ed7a0292bfbfcb837d89a9`.
  - Final native warm-frame averages at 1280x720: combat 1.779 ms, ship 1.825 ms,
    boss 2.034 ms, architecture 1.766 ms, boost 3.032 ms, bank 2.070 ms, arrival 2.842 ms.
    120 measured frames after six warm-up frames; frozen renderer probes excluding
    screenshot encoding, not end-to-end gameplay or a guarantee on other hardware.
  - Audio golden-hash check still fails: observed `a2b83674276419d1`, stored
    `e5b8d4ab7e1fd136`, identical to prior baselines. Audio source is byte-identical;
    no expected hash was changed to conceal the known mismatch.

---

## [Surface detail] Non-emissive material and hardware pass

- Date: 2026-09-11. Status: CONFIRMED for the Windows x64 release.
- User direction: significantly finer surface detail, explicitly not more neon.
- Surface work:
  - Replaced flat craft finishes with staggered armor panels, dark seams, shallow
    bevel relief, slotted bolts, recessed vent grilles, small stencils and subtle wear.
  - Varied paint/albedo and roughness per panel; lit procedural height derivatives
    perturb the surface normal without adding an image texture or render target.
  - Architecture has meter-scaled local plating, access hatches, handles, screws,
    vent cassettes, rolled-metal variation and subdued markings.
  - Deck/cliff surfaces have recessed joints, fasteners, drains and anti-slip texture.
  - Added bounded ship louver, wing-fastener and engine-collar geometry. Changed
    large upgraded-weapon housings from emissive colors to dark metal.
  - Reduced facade streams, building-edge emission and the floor grid. Retained
    existing projectile, exhaust and navigation-light visibility.
- Implementation:
  - `uObjectOrigin` is set per craft with a batch flush; translating craft keep their
    procedural finish. `uViewPosition` is the actual camera position for all materials.
  - Instanced architecture passes local meter coordinates before silhouette deformation;
    terrain passes position before horizon curvature, with virtual scroll removed in FS.
  - Small features use screen derivatives and distance/footprint fade to reduce aliasing.
  - No gameplay rule, generator, audio-source or pool-cap changes; no new heap allocations.
- Release:
  - Windows x64 MinGW GCC 13-win32, static Raylib 6.0, O2/section GC, UPX 4.2.4.
  - **616,960 packed bytes**, 2,239,488 before compression; UPX integrity test passed.
  - Prior graphics edition: 613,888 bytes; increase: **3,072 bytes**.
  - Remaining floppy capacity: **857,600 bytes** out of 1,474,560.
  - EXE SHA-256: `d5bbe2130f99cd86bbd07e16a7e3f6e81c5c586f19f8783f242e4384fce2c7d7`.
- Verification:
  - Native and Linux builds have no project-source compiler warnings.
  - Windows generator: 2,048 zones, zero violations, peaks 790/891/9 unchanged.
  - Windows gameplay: all eight existing checks pass.
  - Windows production-main smoke harness: 180 frames, WASAPI initialization and
    clean shutdown, exit 0. Harness adds only hidden-window/automatic-exit wrappers;
    it is a separate test executable and is not included in the release.
  - Actual native rendering: normal combat, boss/ship close-ups, boost, bank and arrival,
    using seeds 94, 1337 and 0. All shaders compile/link on NVIDIA OpenGL 3.3.
  - Final shaders also compile/link and render on Mesa/llvmpipe.
  - Native 1280x720 frozen-scene warm-frame averages: combat 1.90 ms, ship close-up
    1.89 ms, boss close-up 1.99 ms, boost 3.25 ms, bank 1.90 ms, arrival 2.61 ms.
    NVIDIA RTX 5060, 120 measured frames after 6 warm-up frames. Renderer-only probes,
    excluding screenshot encoding; not an end-to-end gameplay FPS guarantee.
  - Repeated seed/state PNG SHA-256: `4fd8b2a2b693d69cafa0d634c1143e7a8598f37c4c0bdefc685f3ad846fd82b9`.
  - Existing audio test still fails its stored golden hash with `a2b83674276419d1`,
    exactly as both previous baselines; audio source is byte-for-byte unchanged.

---

## [Graphics edition] Procedural cinematic rendering pass

- Date: 2026-09-11.
- Status: CONFIRMED for the Windows x64 release. Requested as a broader visual
  upgrade while retaining the floppy-size executable constraint.
- Architecture: retained C11, Raylib 6.0, embedded GLSL 330, existing render targets
  and fixed gameplay/environment pools. No bitmap, mesh, font or sound assets added.
- Rendering changes:
  - Analytic eclipse/accretion disc, integer-hashed stars and nebula, two skyline layers.
  - Subtle multi-radius bloom, edge-aware smoothing, filmic tone mapping and clamped
    texture taps; removed heavy distortion, repeated scanlines and wavy frame borders.
  - World-locked terrain panels, colored light pools and distance haze.
  - Dark manufactured tower panels; corrected world-scroll sign in facade variation.
  - Disabled vertex breathing that separated roof attachments from their buildings.
  - Corrected conduit edge detection: an edge requires two near-boundary coordinates,
    not one; reduced broad white pulse energy on overhead spans.
  - Metallic faceted player airframe, smoked canopy and layered additive engine exhaust.
  - Supplied surface normals for immediate triangles and cylinders. Explicitly flushed
    batched geometry before changing craft material/type uniforms.
  - Depth-tested, non-depth-writing additive projectile halos, charged-shot trails,
    spark streaks and shockwaves. All geometry remains procedural and bounded.
- Scope: combat rules, spawn logic, audio source, CLI and pool caps are unchanged.
  The eclipse is visual only; a finite ending/black-hole encounter is not implemented.
- Windows release measurements (same toolchain for original and upgrade):
  - Original: 614,912 packed bytes.
  - Graphics edition: **613,888 packed bytes**, 2,229,760 before compression.
  - Limit: strictly below 1,474,560 bytes; headroom **860,672 bytes**.
  - UPX 4.2.4 `--best --lzma`, integrity test passed.
  - Windows x64 MinGW GCC 13-win32, Raylib 6.0 static library compiled with
    `-ffunction-sections -fdata-sections`; game uses existing O2/GC-section flags.
  - EXE SHA-256: `c7868ff709f56a9cd3bd37e43086e75e349332ad98d8bf75e006fa3667132a1d`.
- Verification:
  - Windows and Linux project compilation: no project-source warnings under
    `-Wall -Wextra -Wpedantic`.
  - Native generator: 2,048 zones; zero violations/dropped structures;
    peaks 790 structures, 891 terrain, 9 far, matching the prior baseline.
  - Native gameplay: funds, beam, loop, hit, bomb, boss-cap, link-cap and speed all pass.
  - Audio: existing golden-hash test fails in BOTH original and upgrade with the same
    observed hash `a2b83674276419d1` on GCC/Linux and MinGW/Windows. Stored expected
    hash is `e5b8d4ab7e1fd136`. No audio source or expected hash changed in this pass.
  - Native NVIDIA RTX 5060 / OpenGL 3.3: combat, boss, boost, arrival and banked-camera
    scenes at 1280x720, across seeds 94, 1337 and 0. All shaders compile/link, no warnings.
  - Warm fixed-scene render averages: 1.83 ms combat, 1.90 ms boss, 3.17 ms boost,
    2.55 ms arrival, 1.82 ms banked. Each measurement excludes six warm-up frames,
    averages 120 frames and excludes screenshot encoding. These are renderer probes,
    not an end-to-end gameplay benchmark or a guarantee for other hardware.
  - Identical seed/state repeated screenshot SHA-256:
    `129b3d421adac0bd816e993f6f56ace5f77ae7189f2d2de7fe27a931b48dc104`.
  - Mesa/llvmpipe also compiled and rendered the new shader pipeline.
  - Final UPX-packed EXE launched from an otherwise asset-free directory; seed selector,
    gameplay and WASAPI audio initialization observed, then clean shutdown logged.

---

---

## [Phase 0] Orphaned Scaffolding Cleanup

- **Date:** 2026-09-05
- **Status:**
  - Raymarched-world architecture: **REJECTED** (not just unfinished; incompatible with project constraints and existing pipeline).
  - Soft-body gameplay architecture: **REJECTED** (not just unfinished; incompatible with project constraints and existing pipeline).
  - Dead code cleanup: **CONFIRMED**.
- **Description:**
  - Removed orphaned SDF-raymarch/soft-body scaffolding from prompts X/Y — never integrated, C11 project can't call TS, superseded by Phases 1-3 of the visual uplift plan.
  - Confirmed all deleted files were unreferenced across the entire repository.
- **Files Removed:**
  - `src/rendering/RaymarchPass.ts`
  - `src/physics/SDFCollision.ts`
  - `src/gameplay/SoftBodySystem.ts`
  - `src/engine/Physics.ts`
  - `src/engine/CollisionController.ts`
  - `src/kifs_physics.h`
  - `src/shaders/kifs_raymarch.frag`
  - `shaders/kifs_raymarch.frag`
  - `shaders/kifs_raymarch.fs`
  - Removed empty orphaned directories under `src/` (`src/rendering/`, `src/physics/`, `src/gameplay/`, `src/engine/`, `src/shaders/`).
- **Validation:**
  - `VALIDATE_GENERATOR=1 ./build.sh`: Passed (0 violations).
  - `VALIDATE_GAMEPLAY=1 ./build.sh`: Passed (all checks yes).
  - `VALIDATE_AUDIO=1 ./build.sh`: Passed (deterministic hash match).
- **Binary Size (Packed):**
  - Packed binary (`bin/recursion94`): **724,288 bytes** / 1,474,560 bytes limit (~49.1% of budget, 750,272 bytes headroom).

---

## [Phase 1] Environment Visual Pass

- **Date:** 2026-09-05
- **Status:**
  - Structure & Architectural Shading Enhancements: **CONFIRMED**.
  - Terrain Cliff Strata & Grid Continuation: **CONFIRMED**.
- **Description:**
  - `shaders/tower.fs`:
    - Enhanced `uStructureKind == 11` (Landmark fractal core) with full 3D octahedral box-folding (`q.yz = q.y > q.z ? q.yz : q.zy`).
    - Added `uStructureKind == 12`: Penthouse Control Chamber, embedding an internal demoscene box-fold lattice core into rooftop rooms.
    - Added `uStructureKind == 15`: Antenna Needle / Crown Transmitter, adding tip strobe flashes and upward-traveling carrier waves.
    - Added `uStructureKind == 16`: Service Floor Technical Band, adding horizontal louvers and traveling bus data pulses.
    - Added subtle silicon-die micro-circuitry to inactive faces of Memory Slabs (`uStructureKind == 2`).
    - Added subtle vertical heatsink fluting to non-streaming prism facets of Cache Towers (`uStructureKind == 1`).
  - `src/environment.c`:
    - Updated `DrawArchitectureDetailBatch` to map `detailMode == 7` to kind 12, `detailMode == 3` to kind 15, and `detailMode == 4` to kind 16.
  - `shaders/terrain.fs`:
    - Added seamless vertical world grid continuation down cliff drops (`cliff = 1.0 - upward`).
    - Added horizontal digital canyon strata lines and downward cascade pulses to vertical terrain drops.
- **Validation:**
  - `VALIDATE_GENERATOR=1 ./build.sh`: Passed (identical to baseline: 0 repeat, spacing, field, clearance, conduit violations; peak counts 790 structures / 891 terrain / 9 far identical).
  - `VALIDATE_GAMEPLAY=1 ./build.sh`: Passed (`funds=yes beam=yes loop=yes hit=yes bomb=yes boss-cap=yes link-cap=yes speed=yes`).
  - `VALIDATE_AUDIO=1 ./build.sh`: Passed (`pcm-hash=e5b8d4ab7e1fd136`).
- **Binary Size (Packed):**
  - Packed binary (`bin/recursion94`): **725,032 bytes** / 1,474,560 bytes limit (+744 bytes from baseline, 749,528 bytes headroom remaining).
