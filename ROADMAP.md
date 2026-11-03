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
  - [x] Boss push-in timed to the music (replaces the original §8.3
        pull-back): from the first bar line after the boss spawns, `fovBase`
        tightens one notch per bar over 4 bars to 0.8x, eased over each bar's
        first beat; it eases back after the fight (`update_push`, `game.c`).
        Hand-played: feels right, release is smooth; values kept.
- [x] **Step 4: one song** tuned for the corridor (`SYNTH_SONG_CORRIDOR`).
- [ ] **Step 5: playtest and tune.**
  - [ ] Hand-play: audio by ear, beat bloom (`beatBloom` 0.35), flight and
        combat feel, enemy readability, camera framing.
  - [ ] Listen to the corridor song (`song_render` WAVs): progression, bass,
        lead motif, section lengths, mix trims, Dorian seeds.
  - [ ] Movement box and barrel rolls. Built: steering rectangle
        (`steerBoxHalfWidth/Up/Down`, F4 overlay), camera follow 0.55, roll
        = full turn + dash + i-frames + cooldown. Hand-play and adjust.
  - [x] Backlog: steering-box bottom limit (`steerBoxDown`).
  - [x] Engine trail join: data trail (`trailData`) out of the dart's
        engine glow; the dart is the game ship (`shipDart=1`).
  - [ ] Test resetting the noise seed on every snare (and clap/hat) hit so
        each hit is identical; compare by ear against the free-running stream.
        Re-bless the audio hashes only if kept.
- [x] **Step 6: music clock.** Sync the game to the audio clock instead of
      frame `dt` (`src/game/music_clock.h`).
  - [x] Synth telemetry reports a sample-accurate musical position
        (`SynthTelemetry.musicStep`, 16ths since the synth started).
  - [x] `--game-shot` (no audio thread) free-runs the clock at the seed's BPM,
        so shots stay byte-reproducible.
  - [x] Gameplay reads it: formation spawns and boss reinforcements on beats,
        boss volleys and enemy shots on 16ths, average rates as tuned.
        (Phrase-boundary formations dropped: a phrase is about 7 s, far longer
        than the spawn interval.)
  - [x] Visuals read it: boss firewall nodes and tethers flash on the
        downbeat, unlit HUD meter segments pulse on each bar.
  - [ ] By ear: tune `MUSIC_LATENCY_CALLBACKS` (`game.c`) so shots sit on
        the kick, not ahead of or behind it.

## After the slice

- [ ] The other four levels (environment, song, boss each). Boss chassis
      reference: `git show 1a7c6ca:legacy/src/gameplay.c`, `Draw*Boss`.
- [x] Apply Leviathan gravity (`player.pull`) to flight (level 5 boss):
      engine drift input (`CtInput.driftX/Y`), soft core near the boss,
      `--boss 5` to reach it. Hand-play the strength.
- [ ] Ship selection. The dart is the primary ship; the courier (fighter
      jet) does not suit cyberspace. The original RECURSION_94 probe
      (`DrawPlayerShip`/`DrawPlayerExhaust`) was not ported: see
      `git show 1a7c6ca:legacy/src/gameplay.c`, lines 1546-1787.
- [ ] Font and logo design (not the DATA // RUN Rajdhani font).
- [ ] Demoscene presentation layer.
  - [x] Curve timeline (GNU Rocket style): `src/game/timeline.h`, const
        key tracks (step/linear/smooth/ramp/hermite) evaluated at the music
        clock step. Used by the boss title card so far.
    - [x] Use it for the intro, title and level transitions
          (`src/game/titles.c`): run intro (letter drop-in over copper
          bars, frame and loader lines, flies into the corner logo), the
          `> CALL FRAME n` transition after each boss, the selector title.
          Hand-play: length, legibility over the corridor.
  - [x] Story scroller (`recursion_94.md` §9.3–9.4): sine scrolltext
        over each level transition, in the song's quiet section, with the
        §9.4 texts and the collapse line after the Leviathan. At recursion
        depth 2+ the text is corrupted procedurally (hash of seed and
        depth: glitched characters, stutters, dropped words), with no
        stored variants. Also archetype title cards for boss arrivals
        (`THRESHOLD GUARDIAN // RECURSIVE CORE`). Built (`src/game/story.c`).
    - [ ] Hand-play: scroll speed, placement, timing against the song's
          quiet section, corruption rates.
    - [x] Depth 3+: lines that seem to answer the ship (`recursion_94.md`
          §9.5, `story_compose`). Clean, steady near-white voice; own
          answer from depth 3, cutting in from 4, a pool line from 5.
          `--depth N` to see them. Hand-play: wording, placement.
- [ ] Raymarched SDF bosses and effects. Triangles and ribbons stay, but not
      every boss in that style: give some of the remaining bosses (and maybe
      the selector art) raymarched SDF forms (metaballs, fractal folds, a core
      that really recurses), drawn on a camera-facing quad through
      `ct_set_scene_draw` and bloomed by the engine. Use the cheap tricks:
      SDF soft shadows (min k·d/t), few-tap normal-offset AO, over-relaxed
      stepping in open space, coarse bounding pass so screen tiles exit
      together, iteration caps on secondary rays.
- [x] Pause menu with quit confirmation; fullscreen toggle (F11 /
      Alt+Enter, borderless). Check fullscreen by hand.
- [ ] Period-look decision.
- [ ] Delete `legacy/` files once their content is ported.
