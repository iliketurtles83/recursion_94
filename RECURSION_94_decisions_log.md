# RECURSION_94 — Decisions Log

This log is the source of truth for architectural choices, visual uplift milestones, feature statuses (Confirmed, Provisional, Rejected), and binary size tracking against the 1.44MB (1,474,560 bytes) contest limit.

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

