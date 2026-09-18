#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
raylib="${RAYLIB_WIN_SOURCE:-../raylib-windows}"
x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror \
    -ffunction-sections -fdata-sections -Wl,--gc-sections -static -static-libgcc \
    -I. -Isrc -I"$raylib" tools/playability_smoke.c src/environment.c \
    src/audio_synth.c src/demoscene.c bin/shader_sources.c "$raylib/libraylib.a" \
    -lopengl32 -lgdi32 -lwinmm -o bin/playability-smoke.exe
x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror \
    -ffunction-sections -fdata-sections -Wl,--gc-sections -static -static-libgcc \
    -I. -Isrc -I"$raylib" tools/render_features.c src/player_ui.c src/environment.c \
    src/audio_synth.c src/demoscene.c bin/shader_sources.c "$raylib/libraylib.a" \
    -lopengl32 -lgdi32 -lwinmm -o bin/render-features.exe
x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror \
    -ffunction-sections -fdata-sections -Wl,--gc-sections -static -static-libgcc \
    -I. -Isrc -I"$raylib" tools/audio_controls_test.c "$raylib/libraylib.a" \
    -lopengl32 -lgdi32 -lwinmm -o bin/audio-controls-test.exe
