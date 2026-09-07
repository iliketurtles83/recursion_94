#!/bin/bash
set -e

WINDOWS_CC="${WINDOWS_CC:-x86_64-w64-mingw32-gcc}"
RAYLIB_WIN_SOURCE="${RAYLIB_WIN_SOURCE:-$HOME/raylib-win/src}"
UPX_BIN="${UPX_BIN:-}"

if ! command -v "$WINDOWS_CC" >/dev/null 2>&1; then
    echo "Missing Windows compiler: $WINDOWS_CC"
    echo "Install it in Ubuntu with: sudo apt install mingw-w64"
    exit 1
fi

if [ ! -f "$RAYLIB_WIN_SOURCE/libraylib.a" ]; then
    echo "Missing Windows Raylib library: $RAYLIB_WIN_SOURCE/libraylib.a"
    echo "Build Raylib 6 for Windows before running this script."
    exit 1
fi

mkdir -p bin

bash tools/embed_shaders.sh bin/shader_sources.c

CFLAGS="-std=c11 -O2 -s -Wall -Wextra -Wpedantic -ffunction-sections -fdata-sections"

"$WINDOWS_CC" $CFLAGS -Wl,--gc-sections -static -static-libgcc \
    -Isrc -I"$RAYLIB_WIN_SOURCE" \
    -o bin/recursion94.exe \
    main.c src/environment.c src/audio_synth.c src/gameplay.c src/demoscene.c \
    bin/shader_sources.c \
    "$RAYLIB_WIN_SOURCE/libraylib.a" \
    -lopengl32 -lgdi32 -lwinmm

if [ -z "$UPX_BIN" ] && command -v upx >/dev/null 2>&1; then
    UPX_BIN="$(command -v upx)"
elif [ -z "$UPX_BIN" ] && [ -x "$HOME/.local/bin/upx" ]; then
    UPX_BIN="$HOME/.local/bin/upx"
fi

if [ -z "$UPX_BIN" ]; then
    echo "ERROR: upx is required for a contest release build"
    exit 1
fi

"$UPX_BIN" --best --lzma bin/recursion94.exe
"$UPX_BIN" -t bin/recursion94.exe

MAX_SIZE=1474560
ACTUAL_SIZE=$(stat -c%s bin/recursion94.exe)
if [ "$ACTUAL_SIZE" -gt "$MAX_SIZE" ]; then
    echo "ERROR: packed binary is $ACTUAL_SIZE bytes; limit is $MAX_SIZE bytes"
    exit 1
fi

echo "Native Windows build complete:"
file bin/recursion94.exe
echo "Final size (bin/recursion94.exe): $ACTUAL_SIZE / $MAX_SIZE bytes"
