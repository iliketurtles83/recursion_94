# RECURSION_94 — Visual Uplift Plan (for agy)

## Why this file exists

Two prior prompts (`prompts_that_i_ran`, derived from `research.md`) asked for a raymarched-SDF
environment and a smin-blended soft-body gameplay system. Both were executed as large, single-shot
tasks. The result: plausible-looking code was written (`src/rendering/RaymarchPass.ts`,
`src/physics/SDFCollision.ts`, `src/gameplay/SoftBodySystem.ts`, `src/kifs_physics.h`,
`src/shaders/kifs_raymarch.frag`, `shaders/kifs_raymarch.frag`) that does not connect to the real
game. RECURSION_94 is C11 + Raylib; the TS files cannot be called from anywhere. None of these
files are referenced by `main.c`, `build.sh`, `tools/embed_shaders.sh`, or `shader_sources.h`.
`environment.c` and `gameplay.c` evolved independently and never adopted either approach.

**Root cause, not to repeat:** the two prompts asked for shader + pipeline + gameplay-system changes
in one pass, against a generic research spec, without first checking what already existed in this
specific repo's conventions (C11, fixed-size pools, determinism via seeded hashes, no heap
allocation, UPX size budget). This plan corrects that by forcing small, verified, repo-aware steps.

## Non-negotiable constraints (apply to every phase below)

- **Language:** C11 only. No TypeScript. Nothing in `src/` outside existing `.c`/`.h` conventions.
- **Determinism:** every visual effect must derive from `HashUint`/`SeededHash`/`HashFloat`/
  `PaletteHash`-style seeded hashing. No `rand()`. Same seed -> same frame, always.
  Skip this for the standalone C validator described in `MICRO-2`; it just checks the delete list.
- **No gameplay-time heap allocation.** Fixed-size arrays/pools only, sized in the relevant `.h`.
- **Don't touch:** terrain generation, JIT horizon system, `MAX_STRUCTURES` priority logic,
  the field-site/conduit-graph generator, or the validation harness shape, unless a phase below
  explicitly says otherwise. These are separately owned and mid-rearchitecture.
- **Binary size gate after every phase** (not just at the end): run `./build.sh`, then
  `upx --best bin/recursion94 && upx -t bin/recursion94`, record the byte count. Budget ceiling is
  1,474,560 bytes; current baseline is ~750KB. If a phase pushes past ~1.15MB, stop and flag it
  before continuing — don't let three phases silently stack up against the wall.
- **One phase fully done and verified before the next phase starts.** Order is fixed:
  `0 (cleanup) -> 1 (environment) -> 2 (backdrop) -> 3 (gameplay VFX)`. Do not interleave.
- **Every phase ends with a `RECURSION_94_decisions_log.md` update** before moving to the next
  phase: what was added, confirmed/provisional/rejected status, and the binary size after the
  phase. This log is the source of truth, not this plan file or `SPECS.md`.
- **`VALIDATE_GENERATOR=1 ./build.sh` and `VALIDATE_GAMEPLAY=1 ./build.sh` must both still pass**
  after every phase that touches `environment.c` or `gameplay.c`. Re-run plain `./build.sh`
  afterward per `AGENTS.md` before any playable check.

## How agy should work each phase (process fix, applies everywhere below)

1. **Audit first.** Before writing any code for a phase, read the relevant existing files in full
   and state in the response what already exists, what the phase will add, and what it will NOT
   touch. Do not propose changes to files not named in the phase's scope.
2. **One shader or one system at a time.** Within a phase, land and verify one effect before
   starting the next, even if the phase lists several.
3. **No new files unless the phase says to create one.** Prefer extending existing shaders
   (`tower.fs` kind dispatch, `backdrop.fs`, `post.fs`) over new translation units.
4. **Show the diff, then build, then report the binary size**, in that order, for every change.
5. **If something from the two old prompts is genuinely reusable** (e.g. a shading idea, not the
   TS scaffolding), port only the math/idea into a GLSL 330 fragment appropriate for the existing
   `uStructureKind` dispatch or `post.fs` — never re-introduce the SDF-world/soft-body architecture
   wholesale.

---

## Phase 0 — Cleanup [COMPLETED]

**Status:** Completed on 2026-09-05.

Goal: remove dead scaffolding so nothing downstream can accidentally reference it or get confused
by its presence in the repo.

1. Confirmed unreferenced across the entire repository and removed:
   - `src/rendering/RaymarchPass.ts`
   - `src/physics/SDFCollision.ts`
   - `src/gameplay/SoftBodySystem.ts`
   - `src/engine/Physics.ts`
   - `src/engine/CollisionController.ts`
   - `src/kifs_physics.h`
   - `src/shaders/kifs_raymarch.frag`
   - `shaders/kifs_raymarch.frag`
   - `shaders/kifs_raymarch.fs`
   - Orphaned empty subdirectories: `src/rendering/`, `src/physics/`, `src/gameplay/`, `src/engine/`, `src/shaders/`.
2. Verified zero active references remain in code, shaders, or build scripts.
3. Created `RECURSION_94_decisions_log.md` recording that standalone SDF-raymarched world and
   soft-body gameplay architectures are **rejected** (incompatible with project constraints, C11/Raylib
   conventions, and 1.44MB UPX budget).
4. Verified `./build.sh` and validation suites pass.
5. Baseline binary size recorded: **724,288 / 1,474,560 bytes** (750,272 bytes headroom).

---

## Phase 1 — Environment visual pass [COMPLETED]

**Status:** Completed on 2026-09-05.

Scope: `shaders/tower.fs`, `shaders/tower.vs`, `shaders/terrain.fs`/`.vs`, and additive
`uStructureKind` wiring in `src/environment.c`. Generator algorithms, field-site placement,
and terrain geometry were untouched.

### Implementation Notes:
- **`shaders/tower.fs`**:
  - Enhanced `uStructureKind == 11` (Landmark fractal core) with full 3D octahedral box-folding
    (`q.yz = q.y > q.z ? q.yz : q.zy`) adapted from `backdrop.fs`'s `marchCage`.
  - Added `uStructureKind == 12`: Penthouse Control Chamber, featuring an internal demoscene
    box-fold lattice core for rooftop penthouse rooms.
  - Added `uStructureKind == 15`: Antenna Needle / Crown Transmitter with beacon tip strobes and
    upward-traveling vertical carrier pulses.
  - Added `uStructureKind == 16`: Service Floor Technical Band with horizontal intake louvers and
    traveling bus data pulses dividing tall building shafts.
  - Added subtle silicon-die micro-circuitry to inactive faces of Memory Slabs (`uStructureKind == 2`).
  - Added subtle vertical heatsink fluting to non-streaming prism facets of Cache Towers (`uStructureKind == 1`).
- **`src/environment.c`**:
  - In `DrawArchitectureDetailBatch`: wired `detailMode == 7` (rooftop rooms) to kind 12,
    `detailMode == 3` (antenna needles) to kind 15, and `detailMode == 4` (service bands) to kind 16.
    (Detail mode 13 remains wired to kind 6 multi-sine plasma).
- **`shaders/terrain.fs`**:
  - Added seamless vertical world grid continuation lines down cliff drops (`cliff = 1.0 - upward`).
  - Added anti-aliased horizontal digital canyon strata lines and downward cascade data pulses to
    vertical terrain drops.
- **Validation**:
  - `VALIDATE_GENERATOR=1 ./build.sh`: Passed with 0 violations and identical peak counts
    (790 structures, 891 terrain, 9 far structures). Confirms shading-only pass.
  - `VALIDATE_GAMEPLAY=1 ./build.sh`: Passed (all combat checks yes).
  - `VALIDATE_AUDIO=1 ./build.sh`: Passed (deterministic PCM hash match).
- **Binary Size**:
  - Packed binary (`bin/recursion94`): **725,032 / 1,474,560 bytes** (+744 bytes from baseline,
    749,528 bytes headroom remaining).
- **Decisions Log**:
  - Updated `RECURSION_94_decisions_log.md` with status **CONFIRMED**.

---

## Phase 2 — Backdrop visual pass

Scope: `shaders/backdrop.fs` only, plus its uniform wiring in `main.c`/`environment.c` if a new
uniform is genuinely needed.

Candidate improvements, drawing on the demoscene techniques from `research.md` that are still
actually missing (the `marchCage` box-fold cage already covers recursive folding — don't rebuild
that):
- City silhouette improvements already flagged as open: central tower emphasis, parallax layering
  between building bands, music-intensity-driven density (uIntensity already exists as an input —
  use it), per-seed SDF variation on the skyline silhouette shape.
- A genuine multi-frequency sine plasma layer or polar-tunnel-style screen-space effect as an
  *additional* backdrop layer, gated so it doesn't fight the existing sky/city compositing — this
  is the "no real demoscene effect" gap the person is describing, and it's cheap (pure screen-space
  math, zero geometry, zero memory).

**Done condition:** visual change is confirmed screen-space only (no gameplay/collision impact
possible since backdrop is non-interactive). Binary size recorded. Decisions log updated.

---

## Phase 3 — Gameplay VFX pass

Scope: `src/gameplay.c` drawing functions only (`DrawGameplay3D`, `SpawnBurst`, projectile/particle
draw loops) and, if needed, new fields on existing structs in `src/gameplay.h`
(`CombatParticle`, `PlayerProjectile`, `EnemyProjectile`) — additive fields only, keep fixed-size
pools, do not change `MAX_*` capacities without checking the size/perf impact first.

Candidate improvements:
- Richer weapon-fire visuals: reuse the `renderMatrixRain`/glyph-cell math pattern already in
  `tower.fs` conceptually, or simpler — improve the existing charged-shot ring/trail in
  `DrawGameplay3D` with an audio-reactive (beat-pulse-driven) pulse, since `beatPulse` is already
  threaded through.
- Enemy kill explosions: the current `SpawnBurst` is a simple radial particle spray. Consider a
  brief procedural shockwave/glitch-shard effect at kill time, deterministic per `killSerial`/seed,
  still using the fixed `MAX_COMBAT_PARTICLES` pool — no new allocation, just richer per-particle
  draw treatment (e.g. varying shape/color by `deadType`, which `EnemyColor` already supports).
- Keep every new effect readable against the "arcade forgiveness" pillar — don't reduce hit/telegraph
  clarity for visual flourish.

**Done condition:** `VALIDATE_GAMEPLAY=1 ./build.sh` still passes unchanged (confirms no combat-logic
drift, this was draw-only). Binary size recorded. Decisions log updated.

---

## Final gate (after Phase 3)

1. Plain `./build.sh` (restores playable build per `AGENTS.md` ordering rules).
2. Confirm final packed size is comfortably under 1,474,560 bytes; report headroom remaining.
3. One consolidated decisions-log entry summarizing all three phases and final size, so the log
   reads as a coherent record rather than three disconnected notes.
4. Do not start any new architectural exploration (raymarched worlds, soft-body systems, or
   anything else from `research.md` beyond what's listed above) without a fresh, explicitly-scoped
   prompt — not a repeat of the two broad prompts that caused this cleanup.
