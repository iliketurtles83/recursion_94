#include "audio_synth.h"

#include <inttypes.h>
#include <stdio.h>

int main(void) {
    uint64_t pcmHash = 0;
    bool valid = ValidateAudioSynth(&pcmHash);
    printf("AUDIO: deterministic=%s pcm-hash=%016" PRIx64 "\n",
           valid ? "yes" : "no", pcmHash);
    return valid ? 0 : 1;
}