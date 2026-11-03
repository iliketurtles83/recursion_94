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
- [x] **Step 4: one song** tuned for the corridor (`SYNTH_SONG_CORRIDOR`).
- [ ] **Step 5: playtest and tune.**
  - [ ] Hand-play: audio by ear, beat bloom (`beatBloom` 0.35), flight and
        combat feel, enemy readability, camera framing.
  - [ ] Listen to the corridor song (`song_render` WAVs): progression, bass,
        lead motif, section lengths, mix trims, Dorian seeds.
  - [ ] Movement box and barrel rolls.
  - [ ] Backlog: steering-box bottom limit, engine trail join.

## After the slice

- [ ] The other four levels (environment, song, boss each). Boss chassis
      reference: `git show 1a7c6ca:legacy/src/gameplay.c`, `Draw*Boss`.
- [ ] Apply Leviathan gravity (`player.pull`) to flight (level 5 boss).
- [ ] Ship selection: dart and fighter jet.
- [ ] Font and logo design (not the DATA // RUN Rajdhani font).
- [ ] Demoscene presentation layer.
- [ ] Pause menu with quit confirmation; fullscreen toggle.
- [ ] Period-look decision.
- [ ] Delete `legacy/` files once their content is ported.
