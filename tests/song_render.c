// Offline song render: simulates a run's music controls (cruise, boost bursts,
// pre-boss hush, a boss fight, more cruise), writes a 16-bit stereo WAV and, when
// built with SYNTH_METER, prints per-layer levels for each 10 s window.
//   song_render corridor|none SEED OUT.wav
#include "audio_synth.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLOCK 735   // 1/60 s at 44.1 kHz, like a game frame

static void put32(FILE *f, unsigned v) { fputc(v & 255, f); fputc(v >> 8 & 255, f); fputc(v >> 16 & 255, f); fputc(v >> 24, f); }
static void put16(FILE *f, unsigned v) { fputc(v & 255, f); fputc(v >> 8 & 255, f); }

typedef struct { const char *label; float bossLayer; float hush; bool boss; } Phase;

int main(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: song_render corridor|none SEED OUT.wav\n"); return 2; }
    const SynthSong *song = strcmp(argv[1], "corridor") == 0 ? &SYNTH_SONG_CORRIDOR : NULL;
    uint32_t seed = (uint32_t)strtoul(argv[2], NULL, 10);
    static SynthSystem synth;
    InitAudioSynthOffline(&synth, seed, song);
    printf("song %s, seed %u: %s, %.0f BPM\n", song ? song->name : "(original)", (unsigned)seed,
           GetSynthSeedKeyName(seed), GetSynthSeedBpm(seed));

    FILE *f = fopen(argv[3], "wb");
    if (!f) { perror(argv[3]); return 1; }
    fwrite("RIFF", 1, 4, f); put32(f, 0); fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16); put16(f, 1); put16(f, 2); put32(f, SAMPLE_RATE); put32(f, SAMPLE_RATE * 4); put16(f, 4); put16(f, 16);
    fwrite("data", 1, 4, f); put32(f, 0);

    const double bossAt = 1500.0, bossSeconds[4] = { 2.6, 25.0, 20.0, 25.0 };
    double z = 0.0, t = 0.0, bossT = -1.0, after = 0.0;
    int encounter = 0;
    short buf[BLOCK * 2];
    long frames = 0, clipped = 0;
    double windowSq = 0.0; int peak = 0; int window = 0;
    (void)windowSq;
#ifdef SYNTH_METER
    double last[METER_COUNT] = { 0 };
    printf("%-6s %-9s", "time", "part");
    const char *names[METER_COUNT] = { "kick", "bass", "sub", "pad", "arp", "lead", "hat", "ohat", "snare", "atmos", "echo", "TOTAL" };
    for (int i = 0; i < METER_COUNT; i++) printf(" %6s", names[i]);
    printf("   peak\n");
#endif
    while (after < 150.0) {
        float dt = 1.0f / 60.0f;
        float difficulty = fminf(log1pf((float)(z / 350.0)) * 0.43429448f, 1.0f);
        float cruise = 18.0f + fminf((float)(z / 1800.0), 1.0f) * 12.0f + fminf(encounter * 2.0f, 6.0f);
        bool boosting = bossT < 0.0 && fmod(t, 30.0) > 22.0 && fmod(t, 30.0) < 26.0;
        float intensity = 0.24f + difficulty * 0.20f + 0.05f + (boosting ? 0.48f : 0.0f);
        float hush = 0.0f, bossLayer = 0.0f;
        const char *part = encounter ? "after" : "run";
        if (encounter == 0 && bossT < 0.0 && z >= bossAt) bossT = 0.0;
        if (bossT >= 0.0) {
            double b = bossT; int phase = 0;
            while (phase < 4 && b >= bossSeconds[phase]) b -= bossSeconds[phase++];
            if (phase == 4) { bossT = -1.0; encounter = 1; }
            else {
                static const float layer[4] = { 0.0f, 0.70f, 0.84f, 1.0f };
                static const float bonus[4] = { 0.14f, 0.14f, 0.14f, 0.24f };
                static const char *names4[4] = { "arrive", "shielded", "exposed", "enraged" };
                bossLayer = phase == 0 ? 0.42f + (float)fmin(b / 2.6, 1.0) * 0.25f : layer[phase];
                intensity += bonus[phase];
                part = names4[phase];
                bossT += dt;
            }
        } else if (encounter == 0) {
            double toBoss = bossAt - z;
            if (toBoss > 0.0 && toBoss < 350.0) { hush = (float)(1.0 - toBoss / 350.0); part = "hush"; }
        }
        intensity *= 1.0f - hush * 0.5f;
        if (intensity > 1.0f) intensity = 1.0f;
        float speed = bossT >= 0.0 ? 6.0f : (boosting ? cruise * 1.85f : cruise);
        z += speed * dt;
        UpdateAudioSynth(&synth, intensity, 0.0f, bossLayer, (float)z, hush);
        RenderAudioSynthOffline(&synth, buf, BLOCK);
        for (int i = 0; i < BLOCK * 2; i++) {
            int v = abs(buf[i]);
            if (v > peak) peak = v;
            if (v >= 29900) clipped++;
            windowSq += (double)buf[i] * buf[i];
        }
        fwrite(buf, sizeof(short), BLOCK * 2, f);
        frames += BLOCK;
        t += dt;
        if (encounter) after += dt;
        if ((int)(t / 10.0) != window) {
#ifdef SYNTH_METER
            double n = 10.0 * SAMPLE_RATE;
            printf("%5.0fs %-9s", t, part);
            for (int i = 0; i < METER_COUNT; i++) {
                double e = g_synthMeter[i] - last[i];
                last[i] = g_synthMeter[i];
                double db = e > 0.0 ? 10.0 * log10(e / n) : -99.0;
                printf(" %6.1f", db);
            }
            printf("  %5.1f\n", 20.0 * log10(peak / 32768.0 + 1e-9));
#else
            printf("%5.0fs %-9s rms %6.1f dB peak %6.1f dB\n", t, part,
                   10.0 * log10(windowSq / (10.0 * SAMPLE_RATE * 2) / (32768.0 * 32768.0) + 1e-12),
                   20.0 * log10(peak / 32768.0 + 1e-9));
#endif
            window = (int)(t / 10.0);
            windowSq = 0.0; peak = 0;
        }
    }
    long bytes = frames * 4;
    fseek(f, 4, SEEK_SET); put32(f, (unsigned)(36 + bytes));
    fseek(f, 40, SEEK_SET); put32(f, (unsigned)bytes);
    fclose(f);
    printf("%s: %.1f s, %ld samples near full scale\n", argv[3], frames / (double)SAMPLE_RATE, clipped);
    return 0;
}
