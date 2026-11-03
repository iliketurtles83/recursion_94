# Roadmap

Working task list for the unified game. Branch: `unify-engine` (do not merge to
master unless asked). Decisions and results go in `RECURSION_94_decisions_log.md`;
engine-only items live in `docs/engine/BACKLOG.md`.

Target: five levels, each with its own environment, song and boss. Engine and
environments from rec_tunnel; synth, controls, combat and bosses from the original
RECURSION_94.

## Vertical slice (server corridor)

- [x] **Step 0: merge.** rec_tunnel engine imported, DATA // RUN removed,
      Raylib 6.0, old game moved to `legacy/`.
- [x] **Step 1: game shell.** Seed selector, run loop, synth, beat bloom,
      `r94_corridor` preset, `--game-shot`.
- [x] **Step 2: combat.** Original weapons and enemies ported onto the engine
      (arena mapping), neon vector look, HUD, original music mix.
- [x] **Step 3: one boss.** Recursive Core in a corridor arena.
  - [x] Turn on `bossesEnabled`; neon boss presentation.
  - [x] Slow the engine during the boss (legacy travel speed drops to 6 u/s).
  - [x] Boss music: intensity, preBossHush, boss riser.
  - [ ] Optional: camera pull-back / FOV widening on arrival (original §8.3).
        Preferred: a slow push-in on the boss timed to the music (bars), via
        the music clock and curve timeline below.
- [x] **Step 4: one song** tuned for the corridor (`SYNTH_SONG_CORRIDOR`).
- [ ] **Step 5: playtest and tune.**
  - [ ] Hand-play: audio by ear, beat bloom (`beatBloom` 0.35), flight and
        combat feel, enemy readability, camera framing.
  - [ ] Listen to the corridor song (`song_render` WAVs): progression, bass,
        lead motif, section lengths, mix trims, Dorian seeds.
  - [ ] Movement box and barrel rolls.
  - [ ] Backlog: steering-box bottom limit, engine trail join.
  - [ ] Test resetting the noise seed on every snare (and clap/hat) hit so
        each hit is identical; compare by ear against the free-running stream.
        Re-bless the audio hashes only if kept.
- [ ] **Step 6: music clock.** Sync the game to the audio clock instead of
      frame `dt`.
  - [ ] Synth telemetry reports a sample-accurate musical position (bar,
        16th step, phase within the step), derived from rendered samples.
  - [ ] `--game-shot` (no audio thread) advances a deterministic fake clock
        at the seed's BPM, so shots stay byte-reproducible.
  - [ ] Gameplay reads it: boss volleys and enemy spawns snapped to the
        16th grid, formations starting on phrase boundaries.
  - [ ] Visuals read it: boss node flashes on the downbeat, HUD bar pulse.

## After the slice

- [ ] The other four levels (environment, song, boss each). Boss chassis
      reference: `git show 1a7c6ca:legacy/src/gameplay.c`, `Draw*Boss`.
- [ ] Apply Leviathan gravity (`player.pull`) to flight (level 5 boss).
- [ ] Ship selection: dart and fighter jet.
- [ ] Font and logo design (not the DATA // RUN Rajdhani font).
- [ ] Demoscene presentation layer.
  - [ ] Curve timeline (GNU Rocket style): animated values as Hermite
        keyframe tracks evaluated at the music clock time, for the intro,
        title, boss arrivals and level transitions. Needs the music clock.
- [ ] Raymarched SDF bosses and effects. Triangles and ribbons stay, but not
      every boss in that style: give some of the remaining bosses (and maybe
      the selector art) raymarched SDF forms (metaballs, fractal folds, a core
      that really recurses), drawn on a camera-facing quad through
      `ct_set_scene_draw` and bloomed by the engine. Use the cheap tricks:
      SDF soft shadows (min k·d/t), few-tap normal-offset AO, over-relaxed
      stepping in open space, coarse bounding pass so screen tiles exit
      together, iteration caps on secondary rays.
- [ ] Pause menu with quit confirmation; fullscreen toggle.
- [ ] Period-look decision.
- [ ] Delete `legacy/` files once their content is ported.
