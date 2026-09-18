#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
raylib="${RAYLIB_WIN_SOURCE:-../raylib-windows}"
bash tools/embed_shaders.sh bin/shader_sources.c
for mode in GAMEPLAY GENERATOR; do
    x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror \
        -ffunction-sections -fdata-sections -Wl,--gc-sections -static -static-libgcc \
        -DRECURSION_VALIDATE_$mode -Isrc -I"$raylib" \
        main.c src/environment.c src/gameplay.c src/player_ui.c src/audio_synth.c \
        src/demoscene.c bin/shader_sources.c "$raylib/libraylib.a" \
        -lopengl32 -lgdi32 -lwinmm -o "bin/validate-$mode.exe"
done
