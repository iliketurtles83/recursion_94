# DATA // RUN — Endless tunnel courier

This revision turns the flight sandbox into an endless data courier game.
The existing C11/raylib engine, deterministic path, chunk streaming and HDR rendering
remain the foundation. Launching the executable opens the original Data Vortex
tunnel mission by default. Its three-dimensional curves, twisting rings, ribbons,
streaks, peripheral debris and bright core are retained. The menu offers **1 /
Tunnel** and **2 / Grid City**, selectable by keyboard or mouse.
Both choices start the selected route. **Tab** switches the live preview before
starting; Enter or the Start button launches the route currently on screen.
The menu background is the actual streaming world, with the ship cruising and
gently steering on autopilot. Menu text and its gradient fade over 0.75 seconds.
Changing previews blends scenery over 0.55 seconds while keeping journey distance.
Starting the previewed route does not rebuild the world or reset the camera,
speed, position, trails or soundtrack. Countdown autopilot keeps the ship moving
until player controls become active.

In mission one, fly through at least eight amber data gates, avoid security hazards and enter
the cyan uplink portal within 90 simulation seconds. There are 25 gates on a
3,000 metre route. Three hazard hits disable the ship. Consecutive collections
increase the score multiplier. Near misses award bonus points.
The finish requires both enough data and passing through the portal opening.
A successful delivery triggers a 2.4-second super-warp with widening afterburners,
a speed surge, expanded field of view, radial blur and a rising music mix.
The next mission loads automatically with a protected five-second countdown.
The ship cruises forward during the countdown; its mission clock, collection,
scoring and damage remain inactive. Cumulative score
carries forward, one shield point recovers up to three, and the payload resets.
There is no final win screen: the run continues until a mission is lost.

Each tunnel mission uses a new deterministic route seed and palette. The opening
themes are cyan, violet, emerald, gold with blue accents, and ice blue. Later
missions continue varying the hues. Gameplay colors stay amber for data, red
for hazards and cyan for safe openings. The optional city route also supports
endless missions and music changes, retaining its city palette.

| Mission | New enemy types | Total kinds |
| --- | --- | --- |
| 1 | Guard drone, moving sweeper | 2 |
| 2 | Paired mines, laser wall | 4 |
| 3 | Orbiter, moving shutter | 6 |
| 4 | Interceptor, laser rotor | 8 |
| 5 | Pulsar, splitter | 10 |
| 6 onward | All ten kinds, increasing difficulty | 10 |

After mission five, cruise/boost speed, gate density, required payload and enemy
motion increase while deadlines, pickup tolerance and safe openings tighten.
`mission_difficulty()` supplies a piecewise multiplier: missions 1–5 use 1.00×,
6–10 add 0.08 per mission, 11–20 add 0.14, and 21 onward add 0.18. This makes
mission 10 1.40×, mission 20 2.80× and mission 21 2.98×. The protected countdown
shows the multiplier and marks missions 11–20 HARD, and 21 onward VERY HARD.
The factor drives smoothly bounded pressure `1 - exp(-(difficulty - 1) / 2)`:
cruise approaches 57 m/s, boost 104 m/s, gates 40, required payload 30, and the
deadline 60 seconds. Pickup radius approaches 1.45 metres and laser openings
2.05 metres, including a separate 0.55 metre collision clearance margin.
After mission ten, every fourth leg adds a second attack wave; after twenty,
every second leg does; after thirty, all legs do. Secondary attacks use existing
drones, orbiters, interceptors and splitters, separated in depth from the first
wave. Two full laser walls are never chained within one leg.

| Mission | Multiplier | Boost (m/s) | Hazards | Required / available data | Deadline (s) |
| --- | --- | --- | --- | --- | --- |
| 10 | 1.40× | 72.1 | 28 | 12 / 28 | 84.6 |
| 11 | 1.54× | 74.2 | 34 | 13 / 29 | 82.9 |
| 20 | 2.80× | 88.1 | 42 | 20 / 34 | 72.2 |
| 21 | 2.98× | 89.5 | 51 | 21 / 35 | 71.1 |

Every layout includes each unlocked enemy kind and leaves the first two gates
clear for orientation. Score uses 64 bits for long runs.

Holding Shift or Space accelerates from 32 to 65 m/s and sustains afterburner
speed, including when gates are missed. Releasing boost returns to cruise.
The hidden fuel cutoff has been removed: a regression using real flight physics
previously slowed to 32.06 m/s without pickups; it now stays above 64.7 m/s
after acceleration with zero unrequested boost interruptions. Long frames now
consume their full elapsed time in small simulation steps instead of discarding
time beyond 50 ms. A 120 ms regression advances the full 120 ms.

## Steering surfaces, damage and repair kits

Both wings bank and pitch in response to steering. Each winglet has additional
lean/yaw movement, and each trailing flap deflects for turning and vertical
steering. Commands settle with a damped servo response and return to neutral
when steering is released. These surfaces also move during menu autopilot.
Hull plating, engines and canopy stay rigid. GPU articulation rotates positions
and normals around physical hinges; attached navigation strips use the same
pose in the main camera and floor reflections. Rest-space material coordinates
keep the metal texture and damage marks attached to the moving surfaces.

At two shields, plating on one wing and the aft hull becomes scorched and the
canopy cracks. At one shield, damage spreads to the other wing, with darkened
plating, rough exposed metal and a few dim glowing tears. The effect adds no
smoke or large bloom that obscures targets. Repairs reduce the damage stage.

Each deterministic mission has a **12.5% chance of one green repair kit**, with
an independent random stream that preserves existing enemy/data layouts.
The kit sits just beyond a data gate after several encounters, aligned with
that gate's lane. Its solid green crate has metal corners and a luminous
medical cross; green brackets appear when a damaged ship approaches it.
Flying within 1.35 metres restores one shield point, capped at three, with a
short rising sound. Full-shield pickups are consumed harmlessly. Missing a kit
keeps the combo intact. Repairs preserve speed, data, score and hit immunity,
and cannot revive a ship already disabled earlier in the same frame.

## Controls

| Key | Action |
| --- | --- |
| Enter / Start button | Start or retry |
| 1 / 2 in the menu | Choose Tunnel / Grid City and start |
| Tab in the menu | Switch the live preview |
| WASD / arrows | Horizontal and vertical steering |
| Shift / Space | Boost |
| Q / E | Barrel roll |
| Escape | Pause/resume; back to menu after a result; quit from menu |
| R | Restart at mission one with the same seed |
| Backspace | Return to the moving menu, continuing the current journey |
| M | Mute/unmute |
| F11 | Fullscreen |
| F12 | Save a screenshot in the working directory |

Losing window focus automatically pauses a run. The mission clock and movement
stop while paused, including during warp and countdown. Music transport freezes
as well. The window opens at 1600 ×
900, supports resizing down to 800 × 450, and renders at the current window or
fullscreen resolution.

## Techno, afterburners and challenges

The soundtrack is synthesized live at 48 kHz in stereo. Its sequencer builds a
16-bar arrangement of four-on-the-floor kicks, claps, closed/open hats, sub/acid
bass, detuned arpeggios, minor chord stabs, fills, risers and stereo delay. The
oscillators use band-limited sawtooth edges and the mix uses soft limiting.
No recorded music or external sound files are required.

Tempo eases from **140 BPM at cruise to 166 BPM at full boost**, following actual
flight speed rather than the key state. Five mission arrangements vary the
bass chord progression, arpeggio offset and percussion; their tempo offsets
range from zero to eight BPM. Warp adds a riser, extra kicks and a 22 BPM lift.
The arrangements cycle after mission five without resetting the musical beat.
The bass filter opens, hats grow and the
lead layer becomes more prominent as speed rises. Kick, clap and offbeat hats
are stronger in this revision. Notes keep their pitch while
the sequencer tempo changes. The title/result screen mix is quieter; pause
freezes the musical transport and M mutes it. The audio callback receives atomic
controls and performs no allocation, file access or game-state reads.

Twin 24-petal metal exhaust nozzles widen from a 0.115 to 0.210 metre aperture.
Separate outer-gas and hot-core jet meshes flare from short blue exhaust to long
blue/orange afterburner flames, with animated turbulence, contracting shock
diamonds, soft silhouettes and filtered wisps. Metallic nozzle lips and external
actuator links follow the changing aperture; local blue exhaust light illuminates
the aft hardware. Aperture, plume
width/length, trail width, music and engine pitch all respond to actual speed.

Enemy waves begin after the first two gates. The ten kinds have distinct behaviors:

- Guard drones occupy fixed lanes; sweepers move horizontally and vertically.
- Mines appear in pairs; laser walls leave a cyan circular safe opening.
- Orbiters travel in elliptical loops; shutters move their safe opening.
- Interceptors zigzag across lanes; laser rotors spin a long beam through the tunnel.
- Pulsars expand and contract a damaging ring; splitters swing two linked pods
  around a clear center gap.

Gate lanes alternate horizontally and vertically. Amber data, red enemies and
cyan safe openings have distinct, unlabeled brackets. Rendering, HUD markers,
bot prediction and swept collision share the
same enemy-motion function; collisions sample the exact crossing time. Openings
include a 0.55 metre ship-clearance margin. Hazards retain the three-hit shield
and 1.2 second damage immunity, reduced to 0.9 seconds from mission 21.
Successful laser openings award bonus points.

## Visual changes

- Smooth procedural courier mesh: 64-sided cross-sections with interpolated
  hull/canopy/pod profiles, smooth surface normals and 32,900 hull vertices. Beveled
  wing armor, intake slats, fitted canopy rails, fins, twin engine pods,
  articulated nozzles, recessed dark rear bulkhead and navigation lights.
  The preceding pass added four raised service hatches, sixteen cooling louvers,
  eight cross-slot fasteners, two machined engine collars, nozzle rim thickness,
  sixteen external actuators and a second exhaust layer. This adds **182,256
  bytes (178 KiB)** of actual vertex/index data across the hull, nozzle and flame
  meshes, generated at startup. The new pass adds ten solid engine cooling fins,
  two tubular canopy arches and two curved engine hoses, adding another
  **203,952 bytes (199.2 KiB)** of geometry. This revision adds six articulated
  surfaces with solid bevels, flush rivet rows, dark panel seams, wing-root pivot
  barrels, flap/winglet hinge pins, actuator links and fin spines: another
  **179,868 bytes (175.7 KiB)**. The existing native texture is retained.
- Native 1254 × 1254 generated aircraft-metal albedo texture, with panel seams,
  rivets, machining and worn paint. Triplanar mapping, mipmaps and anisotropic
  filtering keep detail stable at different distances. A GGX lighting shader
  gives paint, glass, carbon and bare metal distinct roughness and reflections.
  The production prompt and native dimensions are saved in
  `assets/ship-metal-prompt.txt`; `assets/ship-metal-v1.png` is the unscaled asset.
- Blue city palette with glazed panels, lit windows, warmer occupied rooms,
  specular highlights, haze, distant skyline and a crescent moon.
- Road edge lights and regularly spaced overhead service bridges, with a clear
  flight lane. A quieter grid, reflections, bloom and chromatic aberration.
- Amber gates and data cores, red drones and a cyan uplink. The flight HUD has
only mission number, payload count, time, shield bars, speed, a thin progress line and small
  afterburner chevrons. Instructions, score and buttons belong to the menu,
  pause and outcome screens. Flight labels, black panels and pickup toasts
  have been removed.
- Original procedural techno, engine, collection, damage, repair and success sounds.
  The Rajdhani font is included under the SIL Open Font License.
- Speed streaks are sparse, thin and faint, restricted to the tunnel perimeter
  and faded out over the center of the screen. Comet flashes, boost zoom blur,
  chromatic aberration and grain are disabled during ordinary flight. Radial
  blur appears only during the protected warp transition. Boost changes the
  camera field of view by only three degrees; camera shake and scenery bloom
  are reduced. Tunnel rings, ribbons and the core remain visible.
- Data gates have a stronger amber band; security drones have red hulls and
  rings. Both receive crisp, unlabeled brackets after post-processing.
  Engine pitch follows actual speed, avoiding abrupt audio changes when boost
  input switches. The speed display rounds to the nearest metre per second.
- The tunnel retains its original rotating rings and helical ribbons. Curves
  use 2.5 times the original tessellation; sparse solid wall panels add recessed
  vents, rivets, grooves and cyan edge lights without filling the flight lane.
  Edge anti-aliasing is enabled for missions, and font atlases are larger for
  crisp HUD rendering at high resolutions.
- Fine ring detail is full strength up to 45 metres, then fades smoothly to zero
  by 125 metres. Sparse, smooth ring silhouettes retain distant tunnel shape.
  Rails, decorative debris and holograms also fade; ribbons become softer and
  distant wall panels lose their fine seams, rivets and vents. Nearby geometry
  retains its tessellation and detail. Gameplay targets remain clear at distance.
  `detailNear` and `detailFar` in the tunnel preset control the transition.
  The tunnel chase camera is slightly closer to show the ship hardware.
- Solid tunnel hardware adds segmented structural ribs, framed recessed vents
  with five raised louvers each, and conduits with enlarged coupling ends.
  `render/tunneldetail.c` builds 3,328 vertices and 4,992 indices per 48 metre
  chunk: **209,664 bytes (204.8 KiB)** beyond the previous panel geometry.
  Parts follow the curved path and remain outside the flight lane. Separate
  machined-metal, dark-recess, louver and conduit materials receive filtered
  highlights; fine shading fades between 45 and 130 metres. No additional neon
  outlines are added. The existing fixed-size chunk pool bounds memory use.

The tunnel and city are procedural and the gameplay is an endless arcade run.
Peripheral wireframe debris and architecture are scenery; the authored hazards are the
damaging actors.
Restart or returning to the menu resets progression; runs are not saved.

## Build

Use CMake 3.20+ and a C11 compiler. CMake fetches pinned raylib 5.5 and raygui 4.0.
For CMake 4, pass `-DCMAKE_POLICY_VERSION_MINIMUM=3.5` for the pinned dependencies.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build build --parallel
ctest --test-dir build --output-on-failure
cmake --install build --prefix package --component Runtime
```

The Windows MinGW build links the runtime statically. Keep `shaders`, `presets`
and `assets` beside the executable when editing them. The Windows EXE also
embeds the complete shader, preset, texture, font and license set. If adjacent
resources are incomplete, it extracts its own files into a unique temporary
directory and cleans them up on normal exit. It can run as a single file,
including when an archive viewer extracts only the executable. Launch does
not depend on the working directory. The packaged executable is named
`DATA-RUN.exe`; the CMake target produces `cybertunnel.exe`.

The normal first frame can have zero elapsed time. Resetting the world now
initializes the render context without advancing simulation, and zero-time game
frames still refresh that context. This fixes the startup access violation in
the tunnel background renderer that autoplay's positive fixed timestep masked.

## Reproducible verification

```sh
cybertunnel --hidden --autoplay
cybertunnel --hidden --autoplay --missions 6 --seed 17
cybertunnel --hidden --autoplay --env 2
cybertunnel --hidden --autoplay --bot-idle
cybertunnel --hidden --autoplay --missions 6 --audio-check --seed 83
cybertunnel --hidden --startup-check
cybertunnel --hidden --ship-check --audio-check --seed 83
cybertunnel --hidden --ship-check --w 3840 --h 2160 --out ship-detail --seed 83
cybertunnel --hidden --mission-shot .2 --bot-idle --shot-shield 1 --out critical.png --seed 83
cybertunnel --hidden --mission-shot .2 --shot-steer 1,0 --out right-turn.png --seed 83
cybertunnel --hidden --mission-shot 0 --out title.png
cybertunnel --hidden --mission-shot 25 --out gameplay.png
cybertunnel --hidden --mission-shot 7.3 --w 1920 --h 1080 --out full-hd.png
cybertunnel --hidden --mission-shot 7.3 --w 3840 --h 2160 --out 4k.png
cybertunnel --hidden --mission 5 --mission-shot 12 --out mission-five.png
cybertunnel --hidden --transition-shot 1.15 --out warp.png --seed 83
cybertunnel --hidden --transition-shot 3.2 --out countdown.png --seed 83
cybertunnel --music-preview techno-preview.wav --mission 2 --seed 83
```

Autoplay uses actual flight physics and swept gate/drone tests, then checks pause,
resume, restart, warp/countdown transitions and switching between tunnel and city.
`--missions N` limits the number of deliveries for that automated run only;
interactive play has no mission limit. `--mission N` selects a starting mission
for QA, and `--transition-shot` captures seconds after the first successful
delivery. A bot that clears its requested missions returns 0; a loss returns 1.
The idle bot intentionally returns 1.
`--audio-check` opens the device and verifies the stereo synthesis callback and
five generated effects while muted. It fails if no music frames were generated.
`--music-preview` writes a 24-second stereo WAV before opening any window. It
starts at cruise and ramps to boost from second eight, using the live synthesizer.
Mission screenshots render into an offscreen texture, so requested resolutions
larger than the physical monitor remain complete. Small-window, Full HD and
3840 × 2160 gameplay captures were visually inspected.
Mission unit tests cover deterministic layout, swept crossings, scoring, missed
gates, damage immunity, near misses, continuously available boost, deadline
ordering, portal entry, loss conditions, sustained boost with and without
pickups, enemy motion at crossing time, laser openings and restart.
The 96 core checks, 23,329 mission checks and 32 music checks pass, along
with the dynamic ship mesh test. Mission checks include progression through
thirty-two transitions, difficulty bounds through mission 100, all ten collision
shapes, deterministic layouts, a mission 10,000
layout, score above five billion, shield recovery, and the full protected countdown.
On Windows there are nine CTest suites, including a six-mission engine run,
interactive startup with a forced zero-time first frame, and standalone startup
from a folder containing only the EXE, live-menu checks for both modes, and
the ship/repair rendering check. Repair tests sample 2,048 deterministic
missions (273 contained a kit), verify the one-kit limit, swept pickup position,
health cap, missed-kit combo behavior, immunity, and damage/repair ordering.
The engine test renders four steering poses and three damage states, freezes
collection during pause, and collects two kits without data/score changes or
boost interruptions.
The live-menu regression waits through long previews, renders scenery blending,
verifies unchanged ship coordinates, camera, speed and seed when starting,
and observes about 160 metres of protected movement during the five-second
countdown. It checks menu fading, countdown pause/resume, cancel/re-enter at
speed and handing over at mission distance zero without camera recentering.
These exercise actual rendering, audio
and embedded-resource extraction. Music checks cover non-silent bounded output,
tempo endpoints, pause/mute, and identical synthesis across different device
buffer sizes, five arrangements, and warp tempo. The ship test checks finite geometry, stable dynamic buffer size
and exhaust aperture expansion, both exhaust layers, six surface tags, attached
lights, rigid fuselage regions, servo settling/reversal and interpolated controls.
It preserves the previous geometry upgrade and the single-mesh index limit. Core tests check the tunnel hardware budget, determinism, finite
unit normals, index limits, materials and clearance from the player lane.
Autoplay also checks held boost without any
pickup events and verifies full movement time across a 120 ms frame. It reports
speed and boost interruptions after acceleration. Seeds 17 and 83 clear six
consecutive missions. Seed 17 clears mission 10 with two shield points; the same
simple QA bot loses its shield at missions 11, 20 and 21. Those are expected
gameplay losses, rather than executable crashes. Late-mission flight and loss
paths, warp, multiplier countdown, and 800 × 450 / 3840 × 2160 captures were
also exercised and visually inspected. Difficulty still needs human playtesting
to judge its feel; the automated bot is a regression tool.

Use `--sandbox --env 0`, `--sandbox --env 2 --preset city`, or
`--sandbox --env 3 --preset corridor` for the original development environments.
Existing `--shot`, `--bench`, parameter and preset tools continue to use sandbox
mode. The ship and shared city shaders also improve the sandbox rendering.

## Integration

`ct_set_ship_damage()` accepts a cosmetic 0..1 severity. Gameplay maps shield
values 3/2/1 to 0/.5/1 after each frame. `FlightState` and `FlightView` carry
smoothed/interpolated effective controls; `ship_articulation.glsl` transforms
the signed wing, winglet and flap tags in UV2.y. The fixed hull uses tag zero.
The shared line shader enables articulation only for attached ship lights,
then disables it before drawing world-space trails.

The game stores `missionOrigin` independently of the streaming world's distance.
Starting a countdown predicts its cruise-in distance, including the decay from
any existing boost velocity. Event positions, the uplink and progress indicators
add this origin; mission rules receive samples relative to it. This lets a new
3,000 metre mission begin anywhere in the live world. Menu and countdown flight
share the normal fixed-step simulation. The pure mission model still protects
the countdown from elapsed mission time, collection and damage.

`game/mission.c` contains pure mission rules. `game/game.c` handles input,
presentation, procedural actors and audio. `game/techno.c` is an independent
audio synthesizer shared by the live callback, WAV preview and tests.
`ct_set_scene_draw()` registers a
callback drawn in both opaque and emissive passes, including floor reflection.
The camera is already active; mesh transforms must subtract the supplied render
origin. Shader output uses linear HDR color. The callback receives the reflected
camera when `view->reflection` is true; HUD projection uses the normal camera.
