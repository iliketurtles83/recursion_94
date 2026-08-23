#!/bin/bash
set -e

mkdir -p bin

CFLAGS="-std=c11 -O2 -s -Wall -Wextra -Wpedantic -ffunction-sections -fdata-sections"
if [ "${VALIDATE_GENERATOR:-0}" = "1" ]; then
    CFLAGS="$CFLAGS -DRECURSION_VALIDATE_GENERATOR"
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
    -Isrc -o bin/recursion94 main.c src/environment.c src/audio_synth.c src/gameplay.c src/demoscene.c $LIBS

if [ "${VALIDATE_GENERATOR:-0}" = "1" ]; then
    bin/recursion94
    exit 0
fi

if command -v upx >/dev/null 2>&1; then
    upx --best bin/recursion94
else
    echo "upx not found; skipping compression"
fi

cp bin/recursion94 recursion_94

echo "Final size (bin/recursion94):"
stat -c%s bin/recursion94
