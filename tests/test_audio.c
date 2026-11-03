#include "audio_synth.h"

#include <inttypes.h>
#include <stdio.h>

// Golden hash of the corridor song render below. Re-bless only for intentional
// changes to the song or the synth (the original arrangement has its own hash).
#define SONG_CORRIDOR_HASH UINT64_C(0x2b15006e766e83fb)

// Renders 24 s of the corridor song with the controls jumping through the
// build-up, the full groove, the pre-boss hush and a boss fight.
static uint64_t RenderSong(uint32_t seed, const SynthSong *song, bool *signal, int *peak)
{
    static SynthSystem synth;
    static short buf[4096 * 2];
    uint64_t hash = UINT64_C(1469598103934665603);
    *signal = false;
    *peak = 0;
    InitAudioSynthOffline(&synth, seed, song);
    for (int block = 0; block < 260; block++) {
        float z = 200.0f + (float)block * 6.0f;               // 200 .. 1760
        float hush = z > 1150.0f && z < 1500.0f ? (z - 1150.0f) / 350.0f : 0.0f;
        float boss = z >= 1500.0f ? 0.84f : 0.0f;
        UpdateAudioSynth(&synth, 0.5f, 0.0f, boss, z, hush);
        if (block == 40) TriggerSynthSFX(&synth, SFX_EXPLOSION);
        if (block == 200) TriggerSynthSFX(&synth, SFX_BOSS_RISER);
        RenderAudioSynthOffline(&synth, buf, 4096);
        for (int i = 0; i < 4096 * 2; i++) {
            int v = buf[i] < 0 ? -buf[i] : buf[i];
            if (v > *peak) *peak = v;
            if (buf[i] != 0) *signal = true;
            uint16_t u = (uint16_t)buf[i];
            hash ^= u & 0xffu; hash *= UINT64_C(1099511628211);
            hash ^= u >> 8;    hash *= UINT64_C(1099511628211);
        }
    }
    return hash;
}

int main(void) {
    uint64_t pcmHash = 0;
    bool valid = ValidateAudioSynth(&pcmHash);
    printf("AUDIO: deterministic=%s pcm-hash=%016" PRIx64 "\n",
           valid ? "yes" : "no", pcmHash);

    bool s1, s2, s3;
    int p1, p2, p3;
    uint64_t a = RenderSong(94u, &SYNTH_SONG_CORRIDOR, &s1, &p1);
    uint64_t b = RenderSong(94u, &SYNTH_SONG_CORRIDOR, &s2, &p2);
    uint64_t o = RenderSong(94u, NULL, &s3, &p3);
    bool songOk = s1 && s2 && a == b && a != o && p1 < 29000 && a == SONG_CORRIDOR_HASH;
    printf("SONG corridor: deterministic=%s hash=%016" PRIx64 " peak=%d (original arrangement peak=%d)\n",
           songOk ? "yes" : "no", a, p1, p3);
    return valid && songOk ? 0 : 1;
}
