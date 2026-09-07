# REC_94 — Game Core Plan

**Date:** 2026-09-07
**Status:** Intended design, recorded to the record. Each point below was verified against the current codebase; the delta between intent and implementation is called out explicitly.

---

## 1. The Core Vision

1. **Fractal black hole destination.** In the distance there is a fractal black hole that the player is flying towards. The run goes **5 loops**; at the end of the fifth loop the player reaches the black hole, where the **big boss** fight happens.
2. **Relentless escalation.** The overall feel must be that the player keeps speeding up through the 5 loops — the game gets more intense, enemies more difficult, and the sound more intense, continuously, until the hole.
3. **Boost must have a purpose.** Boosting (`SPACE`) needs a positional payoff: the player should be able to boost past enemies and/or get to where they are going faster. Currently it reads as serving no purpose.

---

## 2. Verification Against the Codebase

### 2.1 Black hole + 5 loops + final boss — **NOT implemented (loops are endless)**

What exists today:

- A loop structure does exist, but it is **endless**. Defeating the boss core sets `loopTransitionTimer = CORE_TRANSITION_DURATION` (5.2 s) and increments `boss.encounterIndex` (`src/gameplay.c:414-422`). `main.c:362-388` then derives a new seed via `DeriveLoopSeed` (`main.c:46-54`), reseeds environment + audio + demoscene, and `AdvanceGameplayLoop` (`src/gameplay.c:874-888`) reschedules the *next* boss at `virtualPlayerZ + 1900..2300`.
- **There is no cap on `encounterIndex`, no fifth loop, no victory/ending state, and no black hole.** The run simply continues forever with a new theme per loop.
- The boss (Recursive Core / Leviathan) does not culminate — it gets *harder forever*: core health `34 + encounterIndex * 14` (`src/gameplay.c:164`), nodes `2 + 0.5*n` (`src/gameplay.c:160`), reinforcements 1→2 from loop 2 (`src/gameplay.c:177`).
- Fractal visual vocabulary exists but is decorative only: raymarched fractal landmark cores (`shaders/tower.fs:376`), the recursive-cage backdrop sky (`shaders/backdrop.fs`), and the boss's slow fractal core glow (`shaders/craft.fs:67`). Nothing at the horizon reads as a destination.

**Delta:** the loop/boss rhythm the vision is built on is present, but the *terminus* (5th loop → black hole → final boss) does not exist.

### 2.2 Escalation feel — **partially implemented, and it caps out at 3 loops**

Speed (`GetGameplayTravelSpeed`, `src/gameplay.c:998-1012`):

```
cruise = 18 + 12 * min(virtualPlayerZ / 1800, 1) + min(encounterIndex * 2, 6)
```

- Within a loop: 18 → 30 u/s over the first 1800 units, then **flat** (progress clamps at 1.0).
- Per loop: +2 u/s, **clamped at +6** — so full speed (42 u/s) is already reached after loop 3. Loops 4 and 5 would feel no faster than loop 3.
- Boss fights always drop to 6 u/s regardless (`src/gameplay.c:1009`).

Difficulty:

- `game.difficulty = clamp(log1p(z / 350) * 0.434, 0, 1)` (`src/gameplay.c:1035-1036`) — because `virtualPlayerZ` accumulates across loops, this keeps rising over the whole run (hits 1.0 around ~3,150 cumulative units). This is the one escalation that does not cap early.
- `LoopPressure = min(encounterIndex * 0.12, 0.36)` (`src/gameplay.c:46-48`) drives enemy fire timers, projectile speed, and spawn spacing — **also caps at loop 3**.

Sound (`main.c:397-400`, `src/audio_synth.c`):

- `musicIntensity = 0.24 + difficulty * 0.20 + combo term + 0.48 (boost) + 0.14/0.24 (boss)` — rises with distance/combo, not with loop index.
- Each loop **re-seeds the synth with a new seed**, so BPM (one of 16 values, 120–135, `GetSynthSeedBpm` `src/audio_synth.c:93`) and mode (Aeolian/Dorian) change *arbitrarily* per loop — the new loop can be *slower and calmer* than the old one. Per-loop "more intense" is currently a coin flip, not a guarantee.

**Delta:** escalation exists in pieces (cumulative difficulty curve, per-loop +2 speed, boss health scaling) but (a) the speed/pressure escalators flatten exactly when the vision needs the run to peak, and (b) audio intensity per loop is uncontrolled. Nothing in the current code drives off an explicit loop index 0–4.

### 2.3 Boost — **speed only; no positional purpose (the point is largely correct)**

Facts as implemented:

- Boost requires `CanGameplayBoost` (`src/gameplay.c:993-996`): not game over, not in a loop transition, **no active boss**, energy > 1. Energy drains 10/s while boosting, regenerates 1.6/s (`src/gameplay.c:627-628`).
- Boost multiplies travel speed by **1.85×** (`src/gameplay.c:1011`). Since `virtualPlayerZ` accumulates at that rate, the player *does* cover ground faster and reaches the (fixed, absolute-distance) boss spawn point sooner in wall-clock time.
- **But you cannot boost past enemies.** Regular enemies live in a player-relative engagement band (`holdZ` = −22…−28, `src/gameplay.c:653-656`); they re-spawn ahead regardless of travel speed and their approach speed (13 + difficulty·3, `src/gameplay.c:660`) is independent of it. There is also no destination to arrive at — the track is an endless treadmill, so "getting to where you are faster" has no payoff.
- What boost actually buys today: **2× score multiplier** (`src/gameplay.c:437-438`), **+1 bonus chain link per kill** (`src/gameplay.c:444`), and visual/audio garnish (FOV +5°, `src/gameplay.c:1091`; music intensity +0.48, `main.c:399`).
- What boost costs: energy, *plus increased enemy pressure* — spawn timer ×1.8 with spawn interval ×0.72 (`src/gameplay.c:1039-1044`), telegraph delay −0.10 s, chaser dive +6 u/s, spread bolts +4 u/s (`src/gameplay.c:695-712`).

**Delta:** boost is currently a pure risk/reward *score* multiplier, not a traversal tool. The intuition "you can't boost past the enemies, you don't get to where you are faster" is correct in effect: the 1.85× speed changes nothing positional because enemies are player-relative and there is nowhere to arrive.

---

## 3. Consequences / Open Decisions for Implementation

1. **The 5-loop frame.** Need a `loopIndex` (0–4) concept driving: the speed schedule, `LoopPressure`, enemy health/fire schedules, music intensity floor, and environment theme selection. Today every one of these is either distance-logged (caps early) or seed-arbitrary.
2. **The terminus.** After the 5th loop (loop index 4) the run must end at the black hole. Two candidate shapes, undecided:
   - The black hole is a **visible object on the horizon** from loop 1, growing/approaching as the run progresses, and the final boss fight happens *at* it (the Recursive Core becomes the hole's event horizon / the hole is the boss's arena).
   - The 5th boss **is** the black hole (fractal core, singularity phases), defeating it = being consumed / ending the run (win state + end screen; currently the only run-ender is energy = 0).
3. **Escalation must not cap at 3.** Rework the `min(encounterIndex * x, cap)` clamps so loops 4 and 5 are meaningfully faster/harder/louder than 3, and make per-loop audio escalation monotonic (e.g. BPM floor rises with loop index) instead of re-seeding arbitrarily.
4. **Boost needs a positional payoff.** Options on the table:
   - Boost genuinely outruns the engagement band (enemies can be left behind / skipped), at the cost of foregone kills (and chain links).
   - Boost is the means of *arriving*: it closes the distance to the hole/boss faster, making the 5-loop run a real race with a finish line.
   - Keep the risk/reward score identity but pair it with one of the above so it stops feeling purposeless.
