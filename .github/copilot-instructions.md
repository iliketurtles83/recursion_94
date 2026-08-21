# Copilot Cloud Agent Onboarding

Trust this file first. Only search the repository when information here is missing or proven incorrect.

## Repository Summary

- `RECURSION_94` is a small C11 + Raylib 6 procedural rail-shooter with hand-written GLSL and real-time synthesized audio.
- No external art/audio assets are used at runtime; shaders are loaded from `shaders/`.
- Repo footprint is small (about 21 files excluding `.git` internals and generated binaries).
- Tech: C11 (`main.c`, `src/*.c`, `src/*.h`), GLSL 330 (`shaders/*.vs`, `shaders/*.fs`), Bash (`build.sh`, `build_windows.sh`).

## Validated Toolchain And Preconditions

Validated on Linux host with:

- `gcc 11.4.0`
- `upx 3.96`
- Raylib static library at `/usr/local/lib/libraylib.a`

Required preconditions for Linux build:

1. `gcc` must be installed.
2. `/usr/local/lib/libraylib.a` must exist (script hardcodes this path).
3. Link-time system libs must be available (`-lGL -lrt -lX11 -lXrandr -lXi -lXcursor -lXinerama -lasound -lm -ldl -lpthread`).
4. `upx` is optional for build success, but strongly recommended for contest-size output.

Required preconditions for actually running gameplay on Linux:

1. An X11 display must be available (`DISPLAY` set, GLFW can open it).
2. In headless environments, runtime fails quickly with GLFW/X11 errors.

Windows cross-build preconditions:

1. MinGW cross-compiler (`x86_64-w64-mingw32-gcc`) must be installed.
2. Windows Raylib static lib must exist at `$HOME/raylib-win/src/libraylib.a` or `RAYLIB_WIN_SOURCE/libraylib.a`.
3. Optional `UPX_BIN` or `upx` in `PATH` for compression.

## Build, Validate, Run, Test, Lint

There is no separate test framework and no lint script. Validation is done via a deterministic generator check.

Always run commands from repository root.

### Bootstrap

1. Verify prerequisites:
   - `command -v gcc`
   - `ls /usr/local/lib/libraylib.a`
   - `command -v upx` (optional but recommended)

### Clean

1. Remove generated artifacts before a fresh cycle:
   - `rm -f bin/recursion94 recursion_94 bin/recursion94.exe recursion_94.exe`

### Build (Linux)

1. Run: `./build.sh`
2. Expected result:
   - Builds `bin/recursion94`
   - Compresses with UPX when present
   - Copies artifact to `recursion_94`
   - Prints final size
3. Observed timing: about 14.5s on this machine.

### Validation/Test (Deterministic Generator Audit)

1. Run: `VALIDATE_GENERATOR=1 ./build.sh`
2. Expected output includes:
   - `INFO: GENERATOR: zones=2048 repeats=0 landmark-spacing=0 peak=645 dropped=0`
3. Observed timing: about 3.9s.
4. Exit code should be 0.

### Important Order Constraint

`VALIDATE_GENERATOR=1 ./build.sh` recompiles `bin/recursion94` with `RECURSION_VALIDATE_GENERATOR`, so that binary only runs validation and exits.

Always rebuild normal mode after validation before trying gameplay:

1. `VALIDATE_GENERATOR=1 ./build.sh`
2. `./build.sh`
3. Then run the game.

Also note: validation mode exits before `cp`, so root `recursion_94` can remain stale from a previous build while `bin/recursion94` changed.

### Run (Linux)

1. Standard run: `./bin/recursion94`
2. Deterministic seed run: `./bin/recursion94 --seed 94`
3. Boss debug start: `./bin/recursion94 --boss 94`

Headless CI/agent environments without X11 will fail quickly (observed ~0.15s) with:

- `GLFW: X11: Failed to open display :0`

This is expected infrastructure behavior, not a gameplay regression.

### Windows Cross-Build

1. The script file is currently not executable; run it via:
   - `bash ./build_windows.sh`
2. If prerequisites are missing, expected failure is explicit, e.g.:
   - `Missing Windows compiler: x86_64-w64-mingw32-gcc`

## Architecture And File Map

- `main.c`: app loop, seed selector, subsystem init, CLI shortcuts (`--seed`, `--boss`), validation-mode early exit.
- `src/environment.c` + `src/environment.h`: deterministic environment generation/rendering and `ValidateEnvironmentGenerator`.
- `src/gameplay.c` + `src/gameplay.h`: combat/gameplay state; fixed-size pools (`MAX_ENEMIES`, projectile/particle caps).
- `src/audio_synth.c` + `src/audio_synth.h`: realtime procedural music/SFX via Raylib audio callback.
- `src/demoscene.c` + `src/demoscene.h`: seeded backdrop/overlay/seed selector visuals.
- `shaders/`: `craft`, `floor`, `tower`, `post` shader stages loaded via relative paths.

## CI/Check Pipeline Reality

- No `.github/workflows` were found.
- No `README.md` / `CONTRIBUTING.md` were found.
- No dedicated lint/test scripts or project files (`Makefile`, `CMakeLists.txt`, etc.) were found.

Practical pre-PR validation sequence for agents:

1. `./build.sh`
2. `VALIDATE_GENERATOR=1 ./build.sh`
3. `./build.sh` (restore gameplay binary)
4. If GUI available: quick launch `./bin/recursion94 --seed 94`

## Root And Next-Level Layout

Repository root files:

- `build.sh`
- `build_windows.sh`
- `main.c`
- `RECURSION_94.md`
- `recursion_94` (generated binary copy)

Top-level directories:

- `bin/` (`recursion94` generated binary)
- `src/` (all C source/header modules)
- `shaders/` (GLSL shader programs)
- `.git/` (git metadata)

`src/` files: `audio_synth.c/.h`, `demoscene.c/.h`, `environment.c/.h`, `gameplay.c/.h`.

`shaders/` files: `craft.fs/.vs`, `floor.fs/.vs`, `post.fs`, `tower.fs/.vs`.

## Agent Behavior Expectations

- Prefer the command sequences above over rediscovery.
- Treat X11/display failures in headless environments as expected infra limitations.
- Always rerun normal `./build.sh` after validation mode before run checks.
- Only perform extra repository search when these instructions are incomplete or contradicted by current repo state.