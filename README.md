# RECURSION_94

RECURSION_94 is a small procedural rail shooter written in C11 with Raylib 6. Its graphics, levels, music, and sound effects are generated at runtime.

## Linux

The commands below target Debian and Ubuntu. Equivalent packages can be installed with another distribution's package manager.

### Prerequisites

Install the compiler, Raylib build dependencies, and Git:

```bash
sudo apt update
sudo apt install build-essential git \
    libasound2-dev libgl1-mesa-dev libglu1-mesa-dev \
    libx11-dev libxrandr-dev libxi-dev libxcursor-dev libxinerama-dev
```

Build and install the Raylib 6 static library:

```bash
git clone --depth 1 --branch 6.0 https://github.com/raysan5/raylib.git
cd raylib/src
make PLATFORM=PLATFORM_DESKTOP
sudo make install
cd ../..
```

The project build expects Raylib at `/usr/local/lib/libraylib.a`. Confirm that it was installed:

```bash
ls /usr/local/lib/libraylib.a
```

[UPX](https://upx.github.io/) is required for release builds. The build fails unless the packed executable passes UPX's integrity check and fits within 1,474,560 bytes.

### Build and run

From the project root:

```bash
./build.sh
./bin/recursion94
```

The build embeds the GLSL sources into the executable, creates `bin/recursion94`, and copies it to `recursion_94`. The resulting executable has no runtime asset-directory dependency.

Useful launch options:

```bash
./bin/recursion94 --seed 94
./bin/recursion94 --boss 94
```

To run the deterministic environment-generator validation:

```bash
VALIDATE_GENERATOR=1 ./build.sh
```

Validation mode replaces `bin/recursion94` with a validation-only executable. Run `./build.sh` again afterward to restore the playable build.

To render the procedural synth without an audio device and verify its deterministic PCM hash:

```bash
VALIDATE_AUDIO=1 ./build.sh
```

To validate chain-link earning and special-attack behavior without opening a window:

```bash
VALIDATE_GAMEPLAY=1 ./build.sh
```

To measure audio callback duration in a playable build:

```bash
PROFILE_AUDIO=1 ./build.sh
./bin/recursion94 --seed 94
```

Press `F3` to compare the maximum callback duration with the callback's frame budget. Profiling is disabled in normal release builds.

## Windows

The included Windows script cross-compiles a 64-bit `.exe` on Debian or Ubuntu using MinGW-w64.

### Prerequisites

Install the cross-compiler, Raylib build tools, and Git:

```bash
sudo apt update
sudo apt install mingw-w64 make git
```

Build Raylib 6 for Windows in the location expected by the project:

```bash
git clone --depth 1 --branch 6.0 https://github.com/raysan5/raylib.git "$HOME/raylib-win"
cd "$HOME/raylib-win/src"
make PLATFORM=PLATFORM_DESKTOP PLATFORM_OS=WINDOWS \
    CC=x86_64-w64-mingw32-gcc AR=x86_64-w64-mingw32-ar
cd -
```

Confirm that the cross-compiled static library exists:

```bash
ls "$HOME/raylib-win/src/libraylib.a"
```

UPX is required for Windows release builds too. Set `UPX_BIN` to its executable path if it is not in `PATH`.

### Build

From the project root:

```bash
bash ./build_windows.sh
```

The build creates `bin/recursion94.exe` and copies it to `recursion_94.exe`. Shader sources are embedded, so the executable can be distributed on its own.

Custom toolchain locations can be supplied through environment variables:

```bash
WINDOWS_CC=/path/to/x86_64-w64-mingw32-gcc \
RAYLIB_WIN_SOURCE=/path/to/raylib/src \
UPX_BIN=/path/to/upx \
bash ./build_windows.sh
```

## Controls

- `WASD` or arrow keys: move
- `J` or left mouse button: tap for a straight shot; hold to lock homing shots
- `K`: spend 50 chain links on a piercing beam
- `L`: spend 100 chain links on a projectile-clearing bomb
- `Space`: engage overdrive for higher speed, pressure, and rewards
- `F`: toggle fullscreen
- `R`: restart after game over
