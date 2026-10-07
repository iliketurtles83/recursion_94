# DATA // RUN — Tunnel and Grid City vertical slice

This revision turns the flight sandbox into a short, replayable data courier mission.
The existing C11/raylib engine, deterministic path, chunk streaming and HDR rendering
remain the foundation. Launching the executable opens the original Data Vortex
tunnel mission by default. Its three-dimensional curves, twisting rings, ribbons,
streaks, peripheral debris and bright core are retained. The menu offers **1 /
Tunnel** and **2 / Grid City**, selectable by keyboard or mouse.

Fly through at least eight amber data gates, avoid security hazards and enter
the cyan uplink portal within 90 simulation seconds. There are 25 gates on a
3,000 metre route. Three hazard hits disable the ship. Consecutive collections
increase the score multiplier. Near misses award bonus points.
The finish requires both enough data and passing through the portal opening.

Holding Shift or Space accelerates from 32 to 65 m/s and sustains afterburner
speed, including when gates are missed. Releasing boost returns to cruise.
The hidden fuel cutoff has been removed: a regression using real flight physics
previously slowed to 32.06 m/s without pickups; it now stays above 64.7 m/s
after acceleration with zero unrequested boost interruptions. Long frames now
consume their full elapsed time in small simulation steps instead of discarding
time beyond 50 ms. A 120 ms regression advances the full 120 ms.

## Controls

| Key | Action |
| --- | --- |
| Enter / Start button | Start or retry |
| 1 / 2 in the menu | Tunnel / Grid City |
| WASD / arrows | Horizontal and vertical steering |
| Shift / Space | Boost |
| Q / E | Barrel roll |
| Escape | Pause/resume; back to menu after a result; quit from menu |
| R | Restart with the same seed |
| Backspace | Return to menu |
| M | Mute/unmute |
| F11 | Fullscreen |
| F12 | Save a screenshot in the working directory |

Losing window focus automatically pauses a run. The mission clock and movement
stop while paused. Music transport freezes as well. The window opens at 1600 ×
900, supports resizing down to 800 × 450, and renders at the current window or
fullscreen resolution.

## Techno, afterburners and challenges

The soundtrack is synthesized live at 48 kHz in stereo. Its sequencer builds a
16-bar arrangement of four-on-the-floor kicks, claps, closed/open hats, sub/acid
bass, detuned arpeggios, minor chord stabs, fills, risers and stereo delay. The
oscillators use band-limited sawtooth edges and the mix uses soft limiting.
No recorded music or external sound files are required.

Tempo eases from **140 BPM at cruise to 166 BPM at full boost**, following actual
flight speed rather than the key state. The bass filter opens, hats grow and the
lead layer becomes more prominent as speed rises. Kick, clap and offbeat hats
are stronger in this revision. Notes keep their pitch while
the sequencer tempo changes. The title/result screen mix is quieter; pause
freezes the musical transport and M mutes it. The audio callback receives atomic
controls and performs no allocation, file access or game-state reads.

Twin 24-petal metal exhaust nozzles widen from a 0.115 to 0.210 metre aperture.
Separate tessellated jet meshes flare from short blue exhaust to long blue/orange
afterburner flames, with animated turbulence and shock diamonds. Aperture, plume
width/length, trail width, music and engine pitch all respond to actual speed.

There are 25 data gates and 27 hazards, introduced after the first two gates:

- Seven armored security drones occupy fixed lanes.
- Five sweepers move horizontally and vertically, requiring timing and steering.
- Ten mines appear in pairs that close the two outer lanes.
- Five laser walls leave a cyan circular opening that you must fly through.

Gate lanes alternate horizontally and vertically. Amber data, red enemies and
cyan safe openings have distinct, unlabeled brackets. Rendering, HUD markers,
bot prediction and swept collision share the
same enemy-motion function; collisions sample the exact crossing time. Openings
include a 0.55 metre ship-clearance margin. Hazards retain the three-hit shield
and 1.2 second damage immunity. Successful laser openings award bonus points.

## Visual changes

- Smooth procedural courier mesh: 64-sided cross-sections with interpolated
  hull/canopy/pod profiles, smooth surface normals and 25,298 vertices. Beveled
  wing armor, intake slats, fitted canopy rails, fins, twin engine pods,
  articulated nozzles, recessed dark rear bulkhead and navigation lights.
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
  only payload count, time, shield bars, speed, a thin progress line and small
  afterburner chevrons. Instructions, score and buttons belong to the menu,
  pause and outcome screens. Flight labels, black panels and pickup toasts
  have been removed.
- Original procedural techno, engine, collection, damage and success sounds.
  The Rajdhani font is included under the SIL Open Font License.
- Speed streaks are sparse, thin and faint, restricted to the tunnel perimeter
  and faded out over the center of the screen. Comet flashes, boost zoom blur,
  chromatic aberration and grain are disabled for missions. Boost changes the
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

The tunnel and city are procedural and the gameplay is an arcade vertical slice.
Peripheral wireframe debris and architecture are scenery; the authored hazards are the
damaging actors.
There is one mission type, no campaign, and no saved progression.

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
cybertunnel --hidden --autoplay --seed 17
cybertunnel --hidden --autoplay --env 2
cybertunnel --hidden --autoplay --bot-idle
cybertunnel --hidden --autoplay --audio-check
cybertunnel --hidden --startup-check
cybertunnel --hidden --mission-shot 0 --out title.png
cybertunnel --hidden --mission-shot 25 --out gameplay.png
cybertunnel --hidden --mission-shot 7.3 --w 1920 --h 1080 --out full-hd.png
cybertunnel --hidden --mission-shot 7.3 --w 3840 --h 2160 --out 4k.png
cybertunnel --music-preview techno-preview.wav --seed 83
```

Autoplay uses actual flight physics and swept gate/drone tests, then checks pause,
resume, restart and switching between tunnel and city. A winning bot returns 0; the idle bot intentionally returns 1.
`--audio-check` opens the device and verifies the stereo synthesis callback and
four generated effects while muted. It fails if no music frames were generated.
`--music-preview` writes a 24-second stereo WAV before opening any window. It
starts at cruise and ramps to boost from second eight, using the live synthesizer.
Mission screenshots render into an offscreen texture, so requested resolutions
larger than the physical monitor remain complete. Small-window, Full HD and
3840 × 2160 gameplay captures were visually inspected.
Mission unit tests cover deterministic layout, swept crossings, scoring, missed
gates, damage immunity, near misses, continuously available boost, deadline
ordering, portal entry, loss conditions, sustained boost with and without
pickups, enemy motion at crossing time, laser openings and restart.
The original 90 core checks, 136 mission checks and 11 music checks pass, along
with the dynamic ship mesh test. On Windows there are six CTest suites, including
interactive startup with a forced zero-time first frame, and standalone startup
from a folder containing only the EXE. These exercise actual rendering, audio
and embedded-resource extraction. Music checks cover non-silent bounded output,
tempo endpoints, pause/mute, and identical synthesis across different device
buffer sizes. The ship test checks finite geometry, stable dynamic buffer size
and exhaust aperture expansion. Autoplay also checks held boost without any
pickup events and verifies full movement time across a 120 ms frame. It reports
speed and boost interruptions after acceleration. Seeds 1, 17 and 83 finish
successfully with all data collected.

Use `--sandbox --env 0`, `--sandbox --env 2 --preset city`, or
`--sandbox --env 3 --preset corridor` for the original development environments.
Existing `--shot`, `--bench`, parameter and preset tools continue to use sandbox
mode. The ship and shared city shaders also improve the sandbox rendering.

## Integration

`game/mission.c` contains pure mission rules. `game/game.c` handles input,
presentation, procedural actors and audio. `game/techno.c` is an independent
audio synthesizer shared by the live callback, WAV preview and tests.
`ct_set_scene_draw()` registers a
callback drawn in both opaque and emissive passes, including floor reflection.
The camera is already active; mesh transforms must subtract the supplied render
origin. Shader output uses linear HDR color. The callback receives the reflected
camera when `view->reflection` is true; HUD projection uses the normal camera.
