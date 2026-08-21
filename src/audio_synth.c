#include "audio_synth.h"

#include <math.h>
#include <string.h>

#define PI 3.14159265358979323846f
#define DELAY_MASK (SYNTH_DELAY_FRAMES - 1u)

static SynthSystem *g_synth_ref = 0;

static uint32_t SynthHash(uint32_t value) {
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

static float Hash01(uint32_t value) {
    return (float)(SynthHash(value) & 0x00ffffffu) / 16777215.0f;
}

static int SeedRootIndex(uint32_t seed) {
    return (int)(SynthHash(seed ^ 0xa53c9e1du) % 12u);
}

float GetSynthSeedBpm(uint32_t runSeed) {
    return 128.0f + (float)(SynthHash(runSeed ^ 0x4b1d94a7u) % 5u) * 2.0f;
}

const char *GetSynthSeedKeyName(uint32_t runSeed) {
    static const char *names[12] = {
        "C MINOR", "C# MINOR", "D MINOR", "D# MINOR", "E MINOR", "F MINOR",
        "F# MINOR", "G MINOR", "G# MINOR", "A MINOR", "A# MINOR", "B MINOR"
    };
    return names[SeedRootIndex(runSeed)];
}

static float MidiFrequency(int midiNote) {
    return 440.0f * powf(2.0f, ((float)midiNote - 69.0f) / 12.0f);
}

static float SemitoneRatio(int semitones) {
    return powf(2.0f, (float)semitones / 12.0f);
}

static void SetPadChord(SynthSystem *synth, int phrase) {
    // Natural-minor progressions chosen for long-form trance phrasing. Each
    // holds for two bars and glides into the next rather than retriggering.
    static const signed char progressions[4][4] = {
        { 0, 8, 3, 10 },   // i - VI - III - VII
        { 0, 5, 8, 10 },   // i - iv - VI - VII
        { 0, 10, 8, 10 },  // i - VII - VI - VII
        { 0, 3, 5, 8 }     // i - III - iv - VI
    };
    int variant = (int)(SynthHash(synth->runSeed ^ 0x93a5f17du) & 3u);
    int chordRoot = progressions[variant][phrase & 3];
    int third = (chordRoot == 0 || chordRoot == 5) ? 3 : 4;
    int padRootMidi = synth->rootMidi + 12 + chordRoot;

    synth->chordRoot = chordRoot;
    synth->chordThird = third;
    synth->padTargetFrequency[0] = MidiFrequency(padRootMidi);
    synth->padTargetFrequency[1] = MidiFrequency(padRootMidi + third);
    synth->padTargetFrequency[2] = MidiFrequency(padRootMidi + 7);
}

static float SoftClip(float value) {
    float shaped = value / (1.0f + 0.42f * fabsf(value));
    if (shaped > 0.94f) return 0.94f;
    if (shaped < -0.94f) return -0.94f;
    return shaped;
}

// Stronger, separately-tunable drive for drum hits so they keep analog bite
// instead of relying on the subtle master soft clip alone.
static float Drive(float value, float amount) {
    float driven = value * (1.0f + amount);
    return driven / (1.0f + amount * fabsf(driven));
}

static float ProcessSVF(SVFilter *filter, float input) {
    filter->low += filter->cutoff * filter->band;
    float high = input - filter->low - filter->resonance * filter->band;
    filter->band += filter->cutoff * high;
    return filter->low;
}

static float NextNoise(SynthSystem *synth) {
    uint32_t x = synth->noiseState;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    synth->noiseState = x ? x : 0x94a5f31du;
    return (float)(synth->noiseState & 0x00ffffffu) / 8388607.5f - 1.0f;
}

static float AdvanceSine(float *phase, float frequency, float dt) {
    *phase += frequency * dt;
    if (*phase >= 1.0f) *phase -= floorf(*phase);
    return sinf(*phase * 2.0f * PI);
}

static void TriggerStep(SynthSystem *synth, float intensity, float bossIntensity) {
    synth->currentStep = (synth->currentStep + 1) & 15;
    int step = synth->currentStep;
    if (step == 0) {
        synth->barCount++;
        if ((synth->barCount & 1) == 0) SetPadChord(synth, synth->barCount >> 1);
    }

    // Seeded (not real-time random) micro timing and velocity drift so the
    // grid stays deterministic per seed but no longer feels quantize-perfect.
    uint32_t humanizeSeed = SynthHash(synth->runSeed ^ ((uint32_t)synth->barCount * 0x2f6e2b1u) ^
                                      ((uint32_t)step * 0x9e3779b1u));
    synth->stepJitter = (Hash01(humanizeSeed) - 0.5f) * 0.010f;
    synth->hitVelocity = 0.82f + Hash01(humanizeSeed ^ 0x55u) * 0.32f;

    if ((step & 3) == 0) {
        synth->kickTime = 0.0f;
        synth->kickPhase = 0.0f;
        synth->kickTrigger = true;
        synth->beatPulse = 1.0f;
    }

    int bassNote = synth->bassPattern[step];
    if (bassNote >= 0) {
        synth->bassEnv = 1.0f;
        synth->bassTargetFrequency = synth->rootFrequency *
                                     SemitoneRatio(synth->chordRoot + bassNote);
    } else if (bossIntensity > 0.58f && (step & 1) == 1) {
        synth->bassEnv = 0.42f + bossIntensity * 0.22f;
        synth->bassTargetFrequency = synth->rootFrequency *
                                     SemitoneRatio(synth->chordRoot + 12);
    }
    if ((step & 1) == 1 || intensity > 0.74f || bossIntensity > 0.48f) synth->hatTime = 0.0f;
    if ((step & 3) == 2) synth->openHatTime = 0.0f;
    if (step == 4 || step == 12) synth->clapTime = 0.0f;

    if (intensity > 0.28f && ((step & 1) == 0 || intensity > 0.62f ||
                              bossIntensity > 0.38f)) {
        signed char chordTones[8] = {
            0, (signed char)synth->chordThird, 7, 12,
            7, (signed char)(12 + synth->chordThird), 19, 24
        };
        uint32_t noteHash = SynthHash(synth->runSeed + (uint32_t)synth->barCount * 31u + (uint32_t)step * 7u);
        int note = synth->chordRoot + chordTones[noteHash & 7u];
        synth->arpFrequency = synth->rootFrequency * SemitoneRatio(note + 12);
        synth->arpEnv = 0.35f + intensity * 0.65f;
    }
}

static void NativeAudioCallback(void *buffer, unsigned int frames) {
    short *output = (short *)buffer;
    if (!g_synth_ref || !g_synth_ref->initialized) {
        memset(buffer, 0, frames * 2u * sizeof(short));
        return;
    }

    SynthSystem *synth = g_synth_ref;
    const float dt = 1.0f / (float)SAMPLE_RATE;
    const float secondsPerStep = (60.0f / synth->baseBpm) * 0.25f;

    for (unsigned int frame = 0; frame < frames; frame++) {
        synth->intensity += (synth->targetIntensity - synth->intensity) * 0.00065f;
        float intensity = synth->intensity;
        synth->bossIntensity +=
            (synth->targetBossIntensity - synth->bossIntensity) * 0.00072f;
        float bossIntensity = synth->bossIntensity;
        synth->beatPulse = fmaxf(0.0f, synth->beatPulse - dt * 3.8f);

        float stepInterval = secondsPerStep + synth->stepJitter;
        synth->stepTimer += dt;
        if (synth->stepTimer >= stepInterval) {
            synth->stepTimer -= stepInterval;
            TriggerStep(synth, intensity, bossIntensity);
        }

        synth->resonanceLfoPhase += dt * (0.07f + Hash01(synth->runSeed ^ 0x27u) * 0.05f);
        if (synth->resonanceLfoPhase >= 1.0f) synth->resonanceLfoPhase -= 1.0f;
        float resonanceLfo = sinf(synth->resonanceLfoPhase * 2.0f * PI) * 0.5f + 0.5f;

        float kick = 0.0f;
        float kickEnvelope = 0.0f;
        if (synth->kickTrigger) {
            synth->kickTime += dt;
            if (synth->kickTime < 0.34f) {
                float attack = fminf(synth->kickTime / 0.004f, 1.0f);
                kickEnvelope = expf(-synth->kickTime * 9.8f) * attack;
                float pitch = 44.0f + 92.0f * expf(-synth->kickTime * 29.0f);
                kick = AdvanceSine(&synth->kickPhase, pitch, dt) * kickEnvelope * synth->hitVelocity;
                kick = Drive(kick * 1.55f, 0.35f);
            } else {
                synth->kickTrigger = false;
            }
        }

        synth->bassFrequency += (synth->bassTargetFrequency - synth->bassFrequency) * 0.0018f;
        synth->bassPhase += synth->bassFrequency * dt;
        if (synth->bassPhase >= 1.0f) synth->bassPhase -= 1.0f;
        float saw = synth->bassPhase * 2.0f - 1.0f;
        float sub = sinf(synth->bassPhase * 2.0f * PI);
        float pulse = synth->bassPhase < 0.46f ? 1.0f : -1.0f;
        synth->bassEnv = fmaxf(0.0f, synth->bassEnv - dt * (3.7f - intensity * 1.1f));
        synth->bassFilter.cutoff = 0.026f + synth->bassEnv *
                                  (0.070f + intensity * 0.075f + bossIntensity * 0.040f);
        synth->bassFilter.resonance = 0.40f + resonanceLfo * 0.30f - intensity * 0.08f;
        float bassSource = saw * 0.26f + pulse * 0.05f + sub * 0.69f;
        float bassSidechain = 1.0f - kickEnvelope * (0.32f + intensity * 0.08f);
        float bass = ProcessSVF(&synth->bassFilter, bassSource) * synth->bassEnv * bassSidechain;

        synth->padLfoPhase += dt * (0.045f + Hash01(synth->runSeed ^ 0x51u) * 0.025f);
        if (synth->padLfoPhase >= 1.0f) synth->padLfoPhase -= 1.0f;
        float padLfo = sinf(synth->padLfoPhase * 2.0f * PI) * 0.5f + 0.5f;
        float padRawLeft = 0.0f;
        float padRawRight = 0.0f;
        for (int voice = 0; voice < 3; voice++) {
            synth->padFrequency[voice] +=
                (synth->padTargetFrequency[voice] - synth->padFrequency[voice]) * 0.000025f;
            float detune = 0.030f + (float)voice * 0.011f + intensity * 0.014f;
            int phaseIndex = voice * 2;
            synth->padPhase[phaseIndex] += synth->padFrequency[voice] * (1.0f - detune) * dt;
            synth->padPhase[phaseIndex + 1] += synth->padFrequency[voice] * (1.0f + detune) * dt;
            if (synth->padPhase[phaseIndex] >= 1.0f) synth->padPhase[phaseIndex] -= 1.0f;
            if (synth->padPhase[phaseIndex + 1] >= 1.0f) synth->padPhase[phaseIndex + 1] -= 1.0f;
            float sawA = synth->padPhase[phaseIndex] * 2.0f - 1.0f;
            float sawB = synth->padPhase[phaseIndex + 1] * 2.0f - 1.0f;
            float sineA = sinf(synth->padPhase[phaseIndex] * 2.0f * PI);
            float sineB = sinf(synth->padPhase[phaseIndex + 1] * 2.0f * PI);
            float tone = sawA * 0.32f + sawB * 0.28f + sineA * 0.22f + sineB * 0.18f;
            float pan = (float)voice * 0.32f;
            padRawLeft += tone * (0.82f - pan);
            padRawRight += tone * (0.18f + pan);
        }
        float padCutoff = 0.018f + padLfo * 0.016f + intensity * 0.020f;
        synth->padFilterLeft.cutoff = padCutoff;
        synth->padFilterRight.cutoff = padCutoff * 1.035f;
        synth->padFilterLeft.resonance = 0.58f - intensity * 0.08f;
        synth->padFilterRight.resonance = 0.56f - intensity * 0.08f;
        float padLeft = ProcessSVF(&synth->padFilterLeft, padRawLeft * 0.38f);
        float padRight = ProcessSVF(&synth->padFilterRight, padRawRight * 0.38f);
        float sidechain = 1.0f - kickEnvelope * (0.48f + intensity * 0.12f);
        float padGain = (0.080f + padLfo * 0.030f + intensity * 0.030f) * sidechain;
        padLeft *= padGain;
        padRight *= padGain;

        synth->arpEnv = fmaxf(0.0f, synth->arpEnv - dt * (5.6f - intensity * 1.4f));
        synth->arpPhase += synth->arpFrequency * dt;
        if (synth->arpPhase >= 1.0f) synth->arpPhase -= 1.0f;
        float arpSaw = synth->arpPhase * 2.0f - 1.0f;
        float arpTriangle = 1.0f - 4.0f * fabsf(synth->arpPhase - 0.5f);
        synth->arpFilter.cutoff = 0.030f + synth->arpEnv * (0.085f + intensity * 0.065f);
        synth->arpFilter.resonance = 0.30f + resonanceLfo * 0.28f;
        float arpWave = ProcessSVF(&synth->arpFilter, arpSaw * 0.28f + arpTriangle * 0.72f);
        float arp = arpWave * synth->arpEnv *
                    (intensity * 0.14f + bossIntensity * 0.050f) * sidechain;

        synth->hatTime += dt;
        float hat = 0.0f;
        if (synth->hatTime < (intensity > 0.72f ? 0.095f : 0.055f)) {
            float noise = NextNoise(synth);
            float highNoise = noise - synth->previousNoise * 0.82f;
            synth->previousNoise = noise;
            hat = Drive(highNoise * expf(-synth->hatTime * (intensity > 0.72f ? 34.0f : 60.0f)) *
                        synth->hitVelocity, 0.85f);
        }

        synth->openHatTime += dt;
        float openHat = 0.0f;
        if (synth->openHatTime < 0.34f) {
            float noise = NextNoise(synth);
            float metallic = noise - synth->previousNoise * 0.74f;
            synth->previousNoise = noise;
            openHat = Drive(metallic * expf(-synth->openHatTime * (10.0f - intensity * 1.2f)) *
                            synth->hitVelocity, 0.60f);
        }

        synth->clapTime += dt;
        float clap = 0.0f;
        if (synth->clapTime < 0.19f) {
            float burst = expf(-synth->clapTime * 18.0f);
            float flutter = 0.55f + 0.45f * sinf(synth->clapTime * 2.0f * PI * 34.0f);
            clap = Drive(NextNoise(synth) * burst * flutter * synth->hitVelocity, 0.55f);
        }

        // Slowly breathing, decorrelated band-pass noise removes the clean
        // workstation/MIDI-bank edge and supplies an always-present atmosphere.
        synth->atmospherePhase += dt * (0.010f + Hash01(synth->runSeed ^ 0x713u) * 0.008f);
        if (synth->atmospherePhase >= 1.0f) synth->atmospherePhase -= 1.0f;
        float atmosphereLfo = sinf(synth->atmospherePhase * 2.0f * PI) * 0.5f + 0.5f;
        float atmosphereCutoff = 0.008f + atmosphereLfo * 0.012f + intensity * 0.006f;
        synth->atmosphereFilterLeft.cutoff = atmosphereCutoff;
        synth->atmosphereFilterRight.cutoff = atmosphereCutoff * 1.17f;
        synth->atmosphereFilterLeft.resonance = 0.34f;
        synth->atmosphereFilterRight.resonance = 0.31f;
        ProcessSVF(&synth->atmosphereFilterLeft, NextNoise(synth));
        ProcessSVF(&synth->atmosphereFilterRight, NextNoise(synth));
        float atmosphereLeft = synth->atmosphereFilterLeft.band *
                               (0.014f + intensity * 0.006f + bossIntensity * 0.007f);
        float atmosphereRight = synth->atmosphereFilterRight.band *
                                (0.014f + intensity * 0.006f + bossIntensity * 0.007f);

        float sfx = 0.0f;
        for (int voice = 0; voice < MAX_SFX_VOICES; voice++) {
            SFXVoice *sfxVoice = &synth->sfxPool[voice];
            if (!sfxVoice->active) continue;
            sfxVoice->time += dt;
            float progress = sfxVoice->time / sfxVoice->duration;
            if (progress >= 1.0f) {
                sfxVoice->active = false;
                continue;
            }

            if (sfxVoice->type == SFX_LASER_TAP || sfxVoice->type == SFX_LASER_CHARGE) {
                float frequency = sfxVoice->freqStart + (sfxVoice->freqEnd - sfxVoice->freqStart) * progress;
                sfx += sinf(2.0f * PI * frequency * sfxVoice->time) *
                       (1.0f - progress) * sfxVoice->volume;
            } else if (sfxVoice->type == SFX_BOSS_RISER) {
                float eased = progress * progress;
                float frequency = sfxVoice->freqStart +
                                  (sfxVoice->freqEnd - sfxVoice->freqStart) * eased;
                float tone = sinf(2.0f * PI * frequency * sfxVoice->time) * 0.68f +
                             sinf(2.0f * PI * frequency * 1.501f * sfxVoice->time) * 0.22f;
                float noiseLift = NextNoise(synth) * (0.05f + progress * 0.16f);
                float rise = fminf(progress / 0.16f, 1.0f);
                rise = rise * rise * (3.0f - 2.0f * rise);
                float envelope = sinf(progress * PI) * rise;
                sfx += (tone + noiseLift) * envelope * sfxVoice->volume;
            } else if (sfxVoice->type == SFX_POWER_UP) {
                float eased = progress * progress;
                float frequency = sfxVoice->freqStart +
                                  (sfxVoice->freqEnd - sfxVoice->freqStart) * eased;
                float chord = sinf(2.0f * PI * frequency * sfxVoice->time) * 0.62f +
                              sinf(2.0f * PI * frequency * 1.4983f * sfxVoice->time) * 0.38f;
                sfx += chord * sinf(progress * PI) * sfxVoice->volume;
            } else if (sfxVoice->type == SFX_GLITCH_HIT) {
                sfx += NextNoise(synth) * expf(-progress * 6.0f) * sfxVoice->volume;
            } else if (sfxVoice->type == SFX_EXPLOSION) {
                float frequency = sfxVoice->freqStart + (sfxVoice->freqEnd - sfxVoice->freqStart) * progress;
                float tone = sinf(2.0f * PI * frequency * sfxVoice->time);
                sfx += (NextNoise(synth) * 0.72f + tone * 0.28f) *
                       expf(-progress * 4.0f) * sfxVoice->volume;
            }
        }

        unsigned int readIndex = (synth->delayIndex + SYNTH_DELAY_FRAMES - synth->delayFrames) & DELAY_MASK;
        float delayedLeft = synth->delayLeft[readIndex];
        float delayedRight = synth->delayRight[readIndex];
        unsigned int earlyIndex = (synth->delayIndex + SYNTH_DELAY_FRAMES -
                                   synth->delayFrames / 2u) & DELAY_MASK;
        float earlyLeft = synth->delayLeft[earlyIndex];
        float earlyRight = synth->delayRight[earlyIndex];
        // Third tap between the early reflection and the main echo thickens the
        // tail into something closer to a cheap diffuse reverb than one repeat.
        unsigned int lateIndex = (synth->delayIndex + SYNTH_DELAY_FRAMES -
                                 (synth->delayFrames * 3u) / 4u) & DELAY_MASK;
        float lateLeft = synth->delayLeft[lateIndex];
        float lateRight = synth->delayRight[lateIndex];
        float sendLeft = padLeft + arp * 0.92f + atmosphereLeft;
        float sendRight = padRight + arp * 0.92f + atmosphereRight;
        float feedback = 0.34f + intensity * 0.10f;
        synth->delayDampLeft += (delayedRight - synth->delayDampLeft) * 0.35f;
        synth->delayDampRight += (delayedLeft - synth->delayDampRight) * 0.35f;
        synth->delayLeft[synth->delayIndex] =
            SoftClip(sendLeft + synth->delayDampRight * feedback + earlyRight * 0.06f +
                    lateRight * 0.05f);
        synth->delayRight[synth->delayIndex] =
            SoftClip(sendRight + synth->delayDampLeft * feedback + earlyLeft * 0.06f +
                    lateLeft * 0.05f);
        synth->delayIndex = (synth->delayIndex + 1u) & DELAY_MASK;

        float rhythmGain = 0.038f + intensity * 0.045f + bossIntensity * 0.022f;
        float left = kick * (0.70f + bossIntensity * 0.06f) +
                     bass * (0.48f + bossIntensity * 0.08f) + padLeft + arp * 0.42f +
                     hat * rhythmGain * 0.70f + openHat * rhythmGain * 0.32f +
                     clap * rhythmGain * 0.42f + atmosphereLeft +
                     delayedLeft * 0.32f + earlyRight * 0.04f + sfx * 0.46f;
        float right = kick * (0.70f + bossIntensity * 0.06f) +
                      bass * (0.46f + bossIntensity * 0.08f) + padRight + arp * 0.46f +
                      hat * rhythmGain * 0.78f + openHat * rhythmGain * 0.36f +
                      clap * rhythmGain * 0.46f + atmosphereRight +
                      delayedRight * 0.32f + earlyLeft * 0.04f + sfx * 0.46f;

        if (synth->glitchAmount > 0.08f) {
            float levels = 32.0f - synth->glitchAmount * 20.0f;
            left = floorf(left * levels) / levels;
            right = floorf(right * levels) / levels;
        }

        output[frame * 2u] = (short)(SoftClip(left) * 27800.0f);
        output[frame * 2u + 1u] = (short)(SoftClip(right) * 27800.0f);
    }
}

void InitAudioSynth(SynthSystem *synth, uint32_t runSeed) {
    *synth = (SynthSystem){ 0 };
    synth->runSeed = runSeed;
    synth->noiseState = SynthHash(runSeed ^ 0xd1b54a35u);
    if (synth->noiseState == 0u) synth->noiseState = 0x94a5f31du;
    synth->baseBpm = GetSynthSeedBpm(runSeed);
    synth->currentBpm = synth->baseBpm;
    synth->targetIntensity = 0.24f;
    synth->intensity = 0.24f;
    synth->currentStep = 15;
    synth->stepTimer = (60.0f / synth->baseBpm) * 0.25f;
    synth->kickTime = 1.0f;
    synth->hatTime = 1.0f;
    synth->openHatTime = 1.0f;
    synth->clapTime = 1.0f;
    synth->arpFrequency = 220.0f;

    synth->rootMidi = 36 + SeedRootIndex(runSeed);
    synth->rootFrequency = MidiFrequency(synth->rootMidi - 12);
    synth->bassFrequency = synth->rootFrequency;
    synth->bassTargetFrequency = synth->rootFrequency;
    SetPadChord(synth, 0);
    for (int voice = 0; voice < 3; voice++) {
        synth->padFrequency[voice] = synth->padTargetFrequency[voice];
        synth->padPhase[voice * 2] = Hash01(runSeed + (uint32_t)voice * 17u);
        synth->padPhase[voice * 2 + 1] = Hash01(runSeed + (uint32_t)voice * 31u + 9u);
    }

    static const signed char intervals[8] = { 0, 0, 0, 7, 0, 12, 7, 0 };
    for (int step = 0; step < 16; step++) {
        synth->bassPattern[step] = -1;
        uint32_t h = SynthHash(runSeed + (uint32_t)step * 0x9e3779b9u);
        if ((step & 3) == 2 || ((step & 1) == 1 && (h & 3u) == 0u)) {
            synth->bassPattern[step] = intervals[(h >> 3) & 7u];
        }
    }

    synth->bassFilter.cutoff = 0.08f;
    synth->bassFilter.resonance = 0.48f;
    float delaySeconds = (60.0f / synth->baseBpm) * 0.75f;
    synth->delayFrames = (unsigned int)(delaySeconds * (float)SAMPLE_RATE);
    if (synth->delayFrames >= SYNTH_DELAY_FRAMES) synth->delayFrames = SYNTH_DELAY_FRAMES - 1u;

    SetAudioStreamBufferSizeDefault(BUFFER_FRAMES);
    InitAudioDevice();
    if (!IsAudioDeviceReady()) return;
    synth->stream = LoadAudioStream(SAMPLE_RATE, 16, 2);
    if (!IsAudioStreamValid(synth->stream)) {
        CloseAudioDevice();
        return;
    }

    g_synth_ref = synth;
    synth->initialized = true;
    SetAudioStreamCallback(synth->stream, NativeAudioCallback);
    PlayAudioStream(synth->stream);
}

void UpdateAudioSynth(SynthSystem *synth, float intensity, float glitchAmount,
                      float bossIntensity) {
    if (intensity < 0.0f) intensity = 0.0f;
    if (intensity > 1.0f) intensity = 1.0f;
    synth->targetIntensity = intensity;
    if (bossIntensity < 0.0f) bossIntensity = 0.0f;
    if (bossIntensity > 1.0f) bossIntensity = 1.0f;
    synth->targetBossIntensity = bossIntensity;
    synth->glitchAmount = glitchAmount;
    synth->currentBpm = synth->baseBpm;
}

void TriggerSynthSFX(SynthSystem *synth, SFXType type) {
    if (!synth->initialized) return;
    for (int i = 0; i < MAX_SFX_VOICES; i++) {
        SFXVoice *voice = &synth->sfxPool[i];
        if (voice->active) continue;
        voice->type = type;
        voice->time = 0.0f;

        if (type == SFX_LASER_TAP) {
            voice->duration = 0.09f; voice->freqStart = 1100.0f;
            voice->freqEnd = 160.0f; voice->volume = 0.45f;
        } else if (type == SFX_LASER_CHARGE) {
            voice->duration = 0.18f; voice->freqStart = 1600.0f;
            voice->freqEnd = 90.0f; voice->volume = 0.65f;
        } else if (type == SFX_GLITCH_HIT) {
            voice->duration = 0.22f; voice->freqStart = 400.0f;
            voice->freqEnd = 50.0f; voice->volume = 0.70f;
        } else if (type == SFX_EXPLOSION) {
            voice->duration = 0.45f; voice->freqStart = 200.0f;
            voice->freqEnd = 30.0f; voice->volume = 0.85f;
        } else if (type == SFX_POWER_UP) {
            voice->duration = 0.55f; voice->freqStart = 165.0f;
            voice->freqEnd = 880.0f; voice->volume = 0.48f;
        } else if (type == SFX_BOSS_RISER) {
            voice->duration = 2.55f; voice->freqStart = 48.0f;
            voice->freqEnd = 720.0f; voice->volume = 0.46f;
        } else {
            return;
        }
        voice->active = true;
        return;
    }
}

float GetSynthBeatPulse(const SynthSystem *synth) {
    return synth->beatPulse;
}

void UnloadAudioSynth(SynthSystem *synth) {
    if (!synth->initialized) return;
    synth->initialized = false;
    StopAudioStream(synth->stream);
    UnloadAudioStream(synth->stream);
    CloseAudioDevice();
    if (g_synth_ref == synth) g_synth_ref = 0;
}
