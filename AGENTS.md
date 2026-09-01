# AGENTS.md

RECURSION_94 is a C11 + Raylib 6 procedural rail shooter. All graphics, levels, music, and SFX are generated at runtime from a 32-bit seed. The target is a 1.44MB contest executable: both build scripts fail if the UPX-packed binary exceeds 1,474,560 bytes.

## Build and run

- Linux: `./build.sh` → `bin/recursion94` (plus root copy `recursion_94`). Requires `gcc`, `upx` (mandatory, not optional), and Raylib 6 at the hardcoded path `/usr/local/lib/libraylib.a`.
- Windows cross-build: `bash ./build_windows.sh` (MinGW `x86_64-w64-mingw32-gcc` + `$HOME/raylib-win/src/libraylib.a`; override with `RAYLIB_WIN_SOURCE`, `UPX_BIN`).
- Run: `./bin/recursion94` — no runtime asset dependency. `--seed N` skips the seed selector; `--boss N` starts just past the boss spawn.
- No Makefile, no test framework, no linter. Flags include `-Wall -Wextra -Wpedantic` — don't introduce warnings.

## Validation (this is the test suite)

- `VALIDATE_GENERATOR=1 ./build.sh` — headless environment-generator check; exits after running.
- `VALIDATE_GAMEPLAY=1 ./build.sh` — headless chain-link/special-attack check; exits after running.
- `VALIDATE_AUDIO=1 ./build.sh` — builds and runs `bin/audio_validation` (deterministic PCM hash); does not build the game.
- `PROFILE_AUDIO=1 ./build.sh && ./bin/recursion94 --seed 94` — audio callback profiler; `F3` in game.
- Order matters: the first two modes replace `bin/recursion94` with a validation-only binary. Re-run plain `./build.sh` before any gameplay run; the root `recursion_94` copy stays stale until then.
- Headless environments: `GLFW: X11: Failed to open display` is expected, not a regression — use the validation modes above instead.

## Codegen

- Shaders are embedded at build time: `tools/embed_shaders.sh` converts `shaders/*.vs/.fs` into `bin/shader_sources.c` (byte arrays), loaded via `LoadShaderFromMemory`.
- Edit `shaders/`, never `bin/shader_sources.c` (generated, but tracked in git).

## Architecture

- `main.c` — game loop, CLI parsing, post-processing, validation-mode entrypoints.
- `src/environment.c` — deterministic procedural world (sectors/zones/districts) + `ValidateEnvironmentGenerator`.
- `src/gameplay.c` — combat and runtime game state.
- `src/audio_synth.c` — sample-by-sample synthesis in the Raylib audio callback.
- `src/demoscene.c` — seed selector and background visuals.
- `audio_validation.c` — standalone audio validation harness.
- `recursion_94.md` — detailed design reference (gameplay, environment, audio, boss).

## Constraints

- Determinism: all procedural content derives from seeded integer hash functions — never `rand()`. The same seed must reproduce the same run.
- No gameplay-time heap allocation; fixed-size pools for enemies, projectiles, particles, and environment structures (caps in `src/gameplay.h` / `src/environment.h`).
- `.github/copilot-instructions.md` contains stale claims (upx "optional", shaders loaded from relative paths) — trust `build.sh` and `README.md` instead.
