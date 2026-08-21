#ifndef AUDIO_SYNTH_H
#define AUDIO_SYNTH_H

#include "raylib.h"
#include <stdbool.h>
#include <stdint.h>

#define SAMPLE_RATE 44100
#define BUFFER_FRAMES 4096
#define MAX_SFX_VOICES 6
#define SYNTH_DELAY_FRAMES 16384

typedef enum {
    SFX_NONE = 0,
    SFX_LASER_TAP,
    SFX_LASER_CHARGE,
    SFX_GLITCH_HIT,
    SFX_EXPLOSION,
    SFX_POWER_UP,
    SFX_BOSS_RISER
} SFXType;

typedef struct {
    SFXType type;
    float time;
    float duration;
    float freqStart;
    float freqEnd;
    float volume;
    bool active;
} SFXVoice;

typedef struct {
    float low;
    float band;
    float cutoff;
    float resonance;
} SVFilter;

typedef struct {
    AudioStream stream;
    uint32_t runSeed;
    uint32_t noiseState;

    // Tempo is fixed for an entire seed. Boost controls targetIntensity only.
    volatile float baseBpm;
    volatile float currentBpm;
    volatile float targetIntensity;
    volatile float intensity;
    volatile float targetBossIntensity;
    volatile float bossIntensity;
    volatile float glitchAmount;
    volatile float beatPulse;

    float stepTimer;
    volatile int currentStep;
    int barCount;

    float kickTime;
    float kickPhase;
    bool kickTrigger;

    float bassPhase;
    float bassEnv;
    float rootFrequency;
    float bassFrequency;
    float bassTargetFrequency;
    signed char bassPattern[16];
    SVFilter bassFilter;

    int rootMidi;
    int chordRoot;
    int chordThird;
    float padPhase[6];
    float padFrequency[3];
    float padTargetFrequency[3];
    float padLfoPhase;
    SVFilter padFilterLeft;
    SVFilter padFilterRight;

    float arpPhase;
    float arpFrequency;
    float arpEnv;
    SVFilter arpFilter;

    float hatTime;
    float openHatTime;
    float clapTime;
    float previousNoise;

    float atmospherePhase;
    SVFilter atmosphereFilterLeft;
    SVFilter atmosphereFilterRight;

    float delayLeft[SYNTH_DELAY_FRAMES];
    float delayRight[SYNTH_DELAY_FRAMES];
    unsigned int delayIndex;
    unsigned int delayFrames;

    SFXVoice sfxPool[MAX_SFX_VOICES];
    volatile bool initialized;
} SynthSystem;

void InitAudioSynth(SynthSystem *synth, uint32_t runSeed);
void UpdateAudioSynth(SynthSystem *synth, float intensity, float glitchAmount,
                      float bossIntensity);
void TriggerSynthSFX(SynthSystem *synth, SFXType type);
void UnloadAudioSynth(SynthSystem *synth);

float GetSynthSeedBpm(uint32_t runSeed);
const char *GetSynthSeedKeyName(uint32_t runSeed);
float GetSynthBeatPulse(const SynthSystem *synth);

#endif // AUDIO_SYNTH_H
