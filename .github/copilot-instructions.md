# Copilot Cloud Agent Onboarding

Trust this file first. Only search the repository when information here is missing or contradicted by the current project state.

## Repository Summary

- `RECURSION_94` is a small procedural rail-shooter written in C11 with Raylib 6.
- The game generates its environment, shaders, visuals, and synth audio at runtime instead of relying on external art or sound packs.
- The project is intentionally compact: a small set of C modules under `src/`, a handful of shader programs in `shaders/`, and a couple of build scripts in the repo root.
- Core tech: C11 (`main.c`, `src/*.c`, `src/*.h`), GLSL (`shaders/*.vs`, `shaders/*.fs`), Bash (`build.sh`, `build_windows.sh`).

## Validated Toolchain And Preconditions

Validated on a Linux host with:

- `gcc 11.4.0`
- `upx 3.96`
- Raylib static library at `/usr/local/lib/libraylib.a`

Required preconditions for Linux builds:

1. `gcc` must be installed.
2. `/usr/local/lib/libraylib.a` must exist; the Linux build script hardcodes this path.
3. Required link-time systems libraries must be available: `-lGL -lrt -lX11 -lXrandr -lXi -lXcursor -lXinerama -lasound -lm -ldl -lpthread`.
4. `upx` is optional for build success, but is used automatically when present.

Required preconditions for actually running gameplay:

1. An X11 display must be available (`DISPLAY` set and GL/GLFW able to open it).
2. In headless CI or remote agents, a quick X11 failure is expected and does not indicate a gameplay regression.

Windows cross-build preconditions:

1. MinGW cross-compiler (`x86_64-w64-mingw32-gcc`) must be installed.
2. The Windows Raylib static lib must exist at `$HOME/raylib-win/src/libraylib.a` or `RAYLIB_WIN_SOURCE/libraylib.a`.
3. Optional `UPX_BIN` or `upx` in `PATH` for compression.

## Build, Validate, Run, And Check Flow

This repository does not include a dedicated `Makefile`, `CMakeLists.txt`, or lint/test framework. Validation is done through the build script and deterministic runtime checks.

Always run commands from the repository root.

### Bootstrap

1. Check the compiler and library:
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
   - Prints the final binary size
3. Observed timing on the validated host: about 14.5s.

### Deterministic Validation

Use these checks when validating gameplay or generator logic:

1. `VALIDATE_GENERATOR=1 ./build.sh`
   - Expected output includes a generator report such as `INFO: GENERATOR: zones=2048 repeats=0 landmark-spacing=0 peak=645 dropped=0`
   - Exit code should be 0.
2. `VALIDATE_AUDIO=1 ./build.sh`
   - Runs an audio-only validation harness that checks deterministic PCM output.
3. `PROFILE_AUDIO=1 ./build.sh && ./bin/recursion94 --seed 94`
   - Enables the audio callback profiler for a playable build and shows callback-time statistics.

### Important Order Constraint

`VALIDATE_GENERATOR=1 ./build.sh` recompiles the binary with `RECURSION_VALIDATE_GENERATOR`, so the executable exits after validation instead of launching the game.

Always restore the playable build before gameplay:

1. `VALIDATE_GENERATOR=1 ./build.sh`
2. `./build.sh`
3. Then run the game.

### Run (Linux)

1. Normal run: `./bin/recursion94`
2. Deterministic seed run: `./bin/recursion94 --seed 94`
3. Boss debug start: `./bin/recursion94 --boss 94`
4. Audio validation-only run: `VALIDATE_AUDIO=1 ./build.sh`

Headless CI and agent environments without X11 will fail quickly with a message like:

- `GLFW: X11: Failed to open display :0`

This is expected infrastructure behavior and not a gameplay regression.

### Windows Cross-Build

1. Run the script as: `bash ./build_windows.sh`
2. If prerequisites are missing, the expected failure is explicit, e.g.:
   - `Missing Windows compiler: x86_64-w64-mingw32-gcc`
3. This build expects a Raylib static library at `$HOME/raylib-win/src/libraylib.a` unless `RAYLIB_WIN_SOURCE` is overridden.

## Architecture And File Map

- `main.c`: game loop, seed selection, subsystem initialization, CLI shortcuts (`--seed`, `--boss`), and validation-mode early exit.
- `src/environment.c` + `src/environment.h`: deterministic environment generation and `ValidateEnvironmentGenerator`.
- `src/gameplay.c` + `src/gameplay.h`: combat and runtime game state; fixed-size pools for enemies, projectiles, and particles.
- `src/audio_synth.c` + `src/audio_synth.h`: procedural music and SFX generated in the Raylib audio callback.
- `src/demoscene.c` + `src/demoscene.h`: seed selector visuals and runtime background/theme overlays.
- `audio_validation.c`: standalone deterministic audio validation harness for the synth pipeline.

### Shader Files

The current shader set is:

- `shaders/backdrop.fs`
- `shaders/craft.vs`
- `shaders/craft.fs`
- `shaders/terrain.vs`
- `shaders/terrain.fs`
- `shaders/tower.vs`
- `shaders/tower.fs`
- `shaders/post.fs`

These are loaded via relative paths from the project root, so run the binary from the repository root or keep the relative layout intact when distributing builds.

## Repo Layout

Root files:

- `build.sh`
- `build_windows.sh`
- `README.md`
- `main.c`
- `audio_validation.c`
- `recursion_94.md` (project notes)

Top-level directories:

- `bin/` (generated binaries)
- `src/` (game modules)
- `shaders/` (GLSL programs)
- `.git/` (git metadata)

`src/` folders and files:

- `audio_synth.c/.h`
- `demoscene.c/.h`
- `environment.c/.h`
- `gameplay.c/.h`

## Agent Behavior Expectations

- Prefer the validated command sequence above over ad hoc discovery.
- Treat X11/display failures in headless environments as expected infrastructure limitations.
- Always rerun the regular `./build.sh` after validation mode before trying gameplay or launch checks.
- Do not assume a separate test suite exists; trust deterministic builds and runtime validation harnesses instead.
- Only perform extra repository search when the file is missing or contradicts the current project state.