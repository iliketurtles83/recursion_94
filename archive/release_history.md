# RECURSION_94 — Release History (archived)

Per-edition release notes formerly kept at the top of `README.md`, newest first.
Byte counts are as recorded at the time. Current state lives in `README.md`,
`recursion_94.md` and `RECURSION_94_decisions_log.md`.

## Runtime material atlases — 2026-11-02

The detail (256x256) and facade (192x192) RGBA atlases are no longer embedded
as 409,600 bytes of baked data. `src/material_atlas.c` runs the same integer-hash
recipes once at startup into static buffers, producing byte-identical textures
(FNV-1a `456b2ed1` / `13bcae23`, checked by `VALIDATE_GENERATOR`). The look is
unchanged. On a Linux test build, the LZMA-compressed binary dropped from
465,704 to 116,904 bytes. The real Linux UPX release went from 1,230,908 to
798,620 bytes, freeing 432,288 bytes.

The audio validation golden hash was corrected to `a2b83674276419d1`. Synth
output has not changed since commit `57966d2`; the previously stored value was
never produced by that code. `VALIDATE_AUDIO=1 ./build.sh` now passes.

## Tuned boost-risk combat — 2026-09-16

Core Energy is the ship's original health meter again: when it reaches zero
from a hit, the run ends. There is no separate hull or five-hit system. While
actively boosting, a hit costs **20.7 Energy** instead of the normal **18**
(15% more), with the same brief invulnerability afterward. Continuous boost
drains **1 Energy/second** instead of 10, because the old drain alone brought
the ship close to zero before the first missile arrived. Low Energy can still
make any hit fatal, so the HUD is worth watching during long boosts.

Boosted enemy missiles now steer mildly rather than sharply: lateral turning,
lead, maximum strafe speed, and collision radius have all been reduced. They
commit to their path before impact, leaving room for a deliberate dodge.
Regular enemies still fire more often during boost.

Holding **Space** still makes forward travel faster, with these additional
risks:

- Enemy projectiles launched or encountered during boost acquire a mild homing
  lock. The lock persists on that shot after boost ends, but steering is
  bounded and stops before impact.
- Regular enemies advance their firing timers twice as fast while the player
  boosts, and their post-shot cooldown is reduced to 62% of normal. The existing
  boost-related formation pressure and faster shots remain.
- Armed shots gain a warm amber core, and the HUD warns
  `BOOST // GUIDED FIRE // HIT DAMAGE +15%`. No new light sources are added.

The standalone Windows EXE is **992,256 bytes**, leaving **482,304 bytes** below
the strict 1,474,560-byte floppy limit. Projectile and enemy pools stay fixed.
Windows gameplay validation, OpenGL HUD rendering, and a headless seed-94
natural-spawn run pass. In that fixed no-input run, the fifth boosted hit ends
the game; this is not a guarantee of five hits at every starting Energy level.
The pre-existing audio golden-hash mismatch is unchanged.

## Five distinct bosses and pause/quit controls — 2026-09-16

Each round of the endless five-round cycle now has its own boss silhouette and
HUD name: Recursive Core (rotating turbine), Iron Bastion (armored fortress),
Prism Wraith (three swept blades), Siege Mantis (jointed claws), and Fractal
Leviathan (open black aperture and twelve truss arms). These are render-only
assemblies: shield nodes, damage rules, difficulty, and endless progression are
unchanged. The same five visual identities repeat at greater recursion depths.

Press **P** or click **II PAUSE** at the lower-right to freeze the run and music.
The pause panel has Resume and Quit buttons. **Esc** opens a quit confirmation
instead of closing immediately; **No / N / Esc** resumes the run, and
**Yes / Y / Enter** exits. The seed selector also confirms Esc. Gameplay input,
world travel, combat timers, transitions and audio stay frozen while a menu is
open. Clicking the window's native close button still exits normally.

The standalone Windows EXE is **990,720 bytes**, below the strict 1,474,560-byte
floppy limit, with no external runtime assets. Five boss render probes,
Windows generator/gameplay validation, two 210-frame production-loop runs, and
automated keyboard/mouse pause-confirmation tests pass. The pre-existing audio
golden-hash mismatch is unchanged.

## Side-environment detail edition — 2026-09-16

The standalone Windows EXE is **987,136 bytes**, strictly below the 1,474,560-byte
floppy limit. This adds **133,632 useful packed bytes** over the previous 853,504-byte
campaign build, exceeding the requested 100,000-byte improvement without padding or
external runtime assets.

- A new embedded 192x192 RGBA facade atlas contains 147,456 raw bytes of stamped
  relief, roughness, oxidation and edge-wear data. Four motifs distinguish armor
  plates, louver cassettes, grip/hatches and perforated machine casings. All four
  channels affect the side-building and service-hardware shaders; mipmaps suppress
  fine grain at flight distance.
- Taller side structures gain matte, three-section upper wall assemblies with
  recessed plates, rails, fasteners and load-bearing edge ribs. The existing
  corridor clearance and 8,192-instance service cap are preserved.
- No new lights, glow or bloom. Gameplay, weapons, enemy spawning, audio and the
  endless five-round campaign are unchanged.

The facade atlas is now generated at startup by `src/material_atlas.c`
(see "Runtime material atlases" below).

Windows generator/gameplay checks, two 210-frame production-loop smoke runs,
OpenGL 3.3 render probes, repeatability, resize, fallback and texture-reload checks
pass. See the decisions log for exact measurements. The pre-existing audio
golden-hash mismatch remains unchanged.

## Endless singularity campaign — 2026-09-11

The game now has an endless five-round campaign rhythm instead of reseeding after
every boss. Rounds 1–4 culminate in progressively stronger Recursive Cores. Round 5
reaches the enlarged fractal black hole and summons the Fractal Leviathan. Defeating
it collapses and reseeds the universe at the next recursion depth; score and chain
links persist, pressure rises, and the cycle continues without a victory ending.

- The black hole grows subtly across the five rounds, with subdued filament and
  lens detail held high behind the architecture so it remains a backdrop.
- The Leviathan has a much larger dark mechanical chassis, extended horizon anchors,
  substantially more health, three alternating projectile formations, additional
  reinforcements and an arena gravity pull.
- At 52% health it becomes Enraged. At 18% it enters a unique Singularity phase with
  faster rotating machinery, ten-projectile patterns and the strongest gravity.
- A permanent HUD panel shows recursion depth, round and event-horizon proximity.
  Ordinary boss clears restore energy and continue the current universe; only the
  Leviathan launches the full collapse transition.
- Difficulty continues rising across recursion depths through enemy density, fire
  cadence, projectile speed and boss durability, with logarithmic/capped components
  keeping an endless run from immediately degenerating into impossible values.

The standalone Windows EXE is **853,504 bytes** (833.5 KiB), leaving **621,056 bytes**
below the strict 1,474,560-byte floppy limit. It remains a single-file release with
no external runtime assets.

## Surroundings and weapons edition — 2026-09-11

The current standalone Windows EXE is **848,384 bytes** (828.5 KiB), safely below
the 1,474,560-byte floppy limit. This adds **224,768 useful bytes** over the previous
edition, exceeding the requested 0.10 MB minimum without padding or external assets.

- Denser roadside coolant trunks, flanges, supports, handrails, cabinets and heatsinks.
- Building-front and corridor-facing ducts, collars, ladders, grated maintenance
  landings, railings and fan/louver bays. The flight corridor remains clear.
- Embedded height/roughness/oxidation/wear detail, plus access covers and faded
  hazard paint. No extra environment lights or increased post-process bloom.
- Pulse and charged shots have shaped cores, split fins, annular shells and
  velocity-aligned spiral wakes, replacing the old simple balls.
- Beam attacks have six braided filaments, compression collars, fine striations,
  a compact muzzle iris and escaping sparks. They no longer draw as an opaque wedge.
- Radial bomb discharge has four rippling wavefronts, elevated belts and fragments.

Weapon costs, damage, hitboxes, speeds and timers are unchanged. Rendering uses
bounded geometry; environment hardware is capped at 8,192 instances, using static
CPU staging and a persistent GPU buffer. The atlas has mipmaps and is shared by
terrain and building materials, with one owner responsible for cleanup.

Generator/gameplay validation and two 210-frame input-driven production-loop/audio
smoke runs pass. Both runs actually fire pulse, beam and bomb attacks. NVIDIA and
Mesa rendering, repeated seeds, resizing and texture reload/reseed cycles were tested.
The pre-existing audio golden-hash mismatch is unchanged; details are in the log.

The detail atlas is now generated at startup by `src/material_atlas.c`
(see "Runtime material atlases" below). All four channels are used by the environment shaders.

## Mechanical-detail edition — 2026-09-11 (previous release)

That standalone Windows EXE is **623,616 bytes** (609 KiB), leaving 850,944
bytes below the 1,474,560-byte floppy limit. This is 6,656 bytes more than the
surface-detail edition. No external images, models or sound files are required.

- Open boss reactor housings expose turbine blades, concentric rings, bolt heads
  and telescopic actuators; the old spherical shell no longer hides the machinery.
- Raised, beveled ship intake housings, cooling ribs, wing armor pads and service pipes.
- Seeded building service cassettes, radiator blades, weather hoods, structural
  braces and rooftop plant, made from bounded non-emissive geometry.
- Depth-based contact shading gives overlapping parts and recesses more depth,
  without increasing bloom or adding lights. This is local screen-space occlusion,
  not global illumination or ray-traced shadows.

The building-detail pass reuses a 2,048-instance static staging buffer and a GPU
buffer allocated at startup. Existing gameplay pools, spawn rules, collision and
audio are unchanged. A sampleable-depth scene target enables contact shading;
the standard renderbuffer fallback keeps the game playable without that effect.

Windows generator/gameplay checks and two 180-frame production-loop smoke tests
pass. NVIDIA and Mesa rendering, resizing and a forced no-depth fallback were
checked. The pre-existing audio golden-hash mismatch remains; see the decisions
log for exact results, limitations and release checksum.

## Surface-detail edition — 2026-09-11 (previous release)

That Windows EXE is **616,960 bytes** (602.5 KiB), only 3,072 bytes larger than
the initial graphics edition and still strictly below the 1,474,560-byte limit.

This pass emphasizes physical-looking material detail, not extra neon:

- Staggered armor panels, recessed seams, bevel shading, slotted fasteners and access hatches.
- Cooling vents, drainage grilles, anti-slip deck texture, subtle scratches and varied roughness.
- Actual ship cooling-louver, fastener and engine-collar geometry; dark metal weapon housings.
- Object-local and uncurved world coordinates keep procedural surface patterns attached to
  the models/world. Actual camera position drives specular lighting.
- Building outlines, facade data streams and floor-grid emission are reduced. Fine detail
  is derivative-filtered and fades when too small on screen.

No image textures, extra runtime assets or gameplay-time heap allocations were added.
Gameplay and generator validation pass, as does a 180-frame Windows production-loop/audio
smoke harness. The pre-existing audio golden-hash mismatch described below is unchanged.
See the decisions log for exact release checks and timing scope.

## Initial graphics edition — 2026-09-11 (previous release)

The Windows graphics build is **613,888 bytes**, below both 1.44 million bytes and the
1,474,560-byte floppy capacity. Only `bin/recursion94.exe` is needed to play; screenshots
and source files are optional and are not runtime assets. Both release scripts enforce
a strict less-than size check after UPX compression and integrity validation.

- Procedural eclipse, animated accretion disc, nebula, stars and layered skyline.
- Faceted metallic player ship, smoked canopy and additive engine plumes.
- Lit armor normals, colored rim lighting, energy-bolt halos and expanding impact rings.
- World-anchored floor panels, controlled neon edges and atmospheric distance haze.
- Restrained bloom and tone mapping, with clamped sampling to eliminate screen-edge wrapping.
- Overhead conduits retain dark faces instead of glowing solid white; building attachments
  remain rigidly aligned.

This is a rendering upgrade, not a gameplay or difficulty change. Pool limits, seeded
environment generation and audio synthesis are unchanged. The eclipse is a backdrop,
not a new end-game mechanic.

Native Windows startup, shader compilation, five fixed rendering scenes, generator
validation and gameplay validation were checked. The pre-existing audio golden-hash
test fails identically in the original and graphics versions with the tested GCC/MinGW
toolchains (`a2b83674276419d1` versus stored `e5b8d4ab7e1fd136`); the synth was not
modified to hide this mismatch. See `RECURSION_94_decisions_log.md` for measurements.
