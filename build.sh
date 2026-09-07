#!/bin/bash
set -e

mkdir -p bin

bash tools/embed_shaders.sh bin/shader_sources.c

CFLAGS="-std=c11 -O2 -s -Wall -Wextra -Wpedantic -ffunction-sections -fdata-sections"
if [ "${VALIDATE_GENERATOR:-0}" = "1" ]; then
    CFLAGS="$CFLAGS -DRECURSION_VALIDATE_GENERATOR"
fi
if [ "${VALIDATE_GAMEPLAY:-0}" = "1" ]; then
    CFLAGS="$CFLAGS -DRECURSION_VALIDATE_GAMEPLAY"
fi
if [ "${PROFILE_AUDIO:-0}" = "1" ]; then
    CFLAGS="$CFLAGS -DAUDIO_SYNTH_PROFILE"
fi

LIBS="/usr/local/lib/libraylib.a -lGL -lrt -lX11 -lXrandr -lXi -lXcursor -lXinerama -lasound -lm -ldl -lpthread"

if [ "${VALIDATE_AUDIO:-0}" = "1" ]; then
    gcc $CFLAGS -Wl,--gc-sections -Isrc -o bin/audio_validation \
        audio_validation.c src/audio_synth.c $LIBS
    bin/audio_validation
    exit 0
fi

gcc $CFLAGS -Wl,--gc-sections \
    -Isrc -o bin/recursion94 main.c src/environment.c src/audio_synth.c src/gameplay.c \
    src/demoscene.c bin/shader_sources.c $LIBS

if [ "${VALIDATE_GENERATOR:-0}" = "1" ]; then
    bin/recursion94
    exit 0
fi
if [ "${VALIDATE_GAMEPLAY:-0}" = "1" ]; then
    bin/recursion94
    exit 0
fi

if ! command -v upx >/dev/null 2>&1; then
    echo "ERROR: upx is required for a contest release build"
    exit 1
fi

upx --best bin/recursion94
upx -t bin/recursion94

MAX_SIZE=1474560
ACTUAL_SIZE=$(stat -c%s bin/recursion94)
if [ "$ACTUAL_SIZE" -gt "$MAX_SIZE" ]; then
    echo "ERROR: packed binary is $ACTUAL_SIZE bytes; limit is $MAX_SIZE bytes"
    exit 1
fi

echo "Final size (bin/recursion94): $ACTUAL_SIZE / $MAX_SIZE bytes"
