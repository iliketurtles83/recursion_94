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
    - [x] Hand-play fixes: the edge cushion no longer pushes back from a
          corner when the stick is released (it fades outward stick and
          damps outward motion only); the dash follows the roll's curve
          (2.6 units, ends with the turn).
  - [x] Backlog: steering-box bottom limit (`steerBoxDown`).
  - [x] Engine trail join: data trail (`trailData`) out of the dart's
        engine glow; the dart is the game ship (`shipModel=1`).
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
        the kick, not ahead of or behind it. F5 in a run flashes the beat
        from the game clock; `[` `]` move it live (0.25 callback steps).

- [ ] **Step 7: readability (playtest reviews, 2026-11-04).**
  - [x] Visual roles, fixed on every seed: player fire = white-core
        streaks with one cool tint; enemy fire = one reserved hot colour
        used by nothing else, filled round orbs with a dark rim (bullet-hell
        rule), sized to the hit radius. Enemies and bosses keep the seed
        palette (shifted off the enemy-fire hue).
  - [x] Brightness order: enemy fire > enemies > environment; dim the
        corridor's emissives in the middle of the view during combat.
        (Combat focus dim, `FOCUS_DIM` 0.6; hand-play the strength.)
  - [x] Hit feedback: enemies flash white when damaged, sparks at impacts.
  - [x] Sound, first pass: shot sound out of the kick/bass register and
        louder; enemy-fire sound (the dodge cue), panned to the shooter;
        stereo SFX; per-shot pitch variation. `song_render sfx` demo.
        Listen and adjust.
  - [ ] Sound, next: telegraph, hit-tick, lock-on, chain-link, low-energy
        and roll sounds; pitch SFX to the seed's scale (decide by ear);
        check the drumless first ~25 s by ear.
  - [ ] Barrel roll: i-frames only in the middle of the roll (no
        near-permanent invulnerability by spamming); reward a near miss
        (graze) with chain links.
  - [ ] Boost risk/reward: it speeds up spawns 1.8x; give it a score
        multiplier.
  - [x] Boss arrival window: no formations once the boss is due; the boss
        lands on a bar line (field clear, or 2 bars at most) and pulls the
        survivors in, where they burst (no reward, no boss damage).
  - [x] Core desperation lattice (from `886ba1b`): enraged Core alternates
        fans with a wall of ~55-90 shots that opens into a lattice on the
        player's plane, one clear lane marked by white gate brackets about
        1 s ahead. Hand-play: wall density, lane width, warning time.
  - [ ] Port from collaborator commit `886ba1b` (decisions 2026-11-05):
        sniper, carrier, support roles (reworked: distinct depth, shape and
        group), charge feedback on the ship (no crosshair), Core lattice as
        a low-health desperation attack (later: consider moving it to a
        later boss, it suits the barrel roll), the other bosses' patterns
        (with their chassis), upgrade weapons as pickups only (dropped by a
        wiped group or a harder enemy; limited shots, carried between
        stages, reset on death; no salvage cards), minimal HUD (energy,
        specials ready, ammo, boss).
  - [ ] Variety: procedural formation grammar (lines, V, rings, snakes,
        mirrored pairs) spawned on phrase lines from the seed; 3-4 new enemy
        roles (weaver, mine, shielded = charge only, wall turret); corridor
        hazards on the bar (closing racks, laser gates); difficulty that
        follows the song's sections, not only distance.

## After the slice

- [ ] The other four levels (environment, song, boss each). Boss chassis
      reference: `git show 1a7c6ca:legacy/src/gameplay.c`, `Draw*Boss`.
- [x] Apply Leviathan gravity (`player.pull`) to flight (level 5 boss):
      engine drift input (`CtInput.driftX/Y`), soft core near the boss,
      `--boss 5` to reach it. Hand-play the strength.
- [x] Ship selection in the seed selector (Tab / 1-3, `--ship N`): dart
      (default), the original RECURSION_94 probe (ported from
      `DrawPlayerShip` as `src/render/shipprobe.c`), the courier jet.
  - [ ] Hand-play the probe and jet in combat (size against the arena,
        glow strength); the jet has afterburner flames and no trail.
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
    - [x] Floor dot scroller (playtest: the 2D sine scroller broke the 3D
          feel): word-wrapped lines of glowing dots (the default font's
          pixels) on the corridor floor, one line per bar drifting toward the
          camera; scattered beat-pulsing sparks far ahead pull into letters at
          reading distance and break up under the camera. Answers arrive
          formed and near-white.
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
- [x] Delete `legacy/` (original game kept as tag `r94-floppy`).
