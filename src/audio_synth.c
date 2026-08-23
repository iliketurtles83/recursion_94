#define _POSIX_C_SOURCE 200809L
#include "audio_synth.h"

#include <math.h>
#include <string.h>
#ifdef AUDIO_SYNTH_PROFILE
#include <time.h>
#endif

#define PI 3.14159265358979323846f
#define DELAY_MASK (SYNTH_DELAY_FRAMES - 1u)
// Distance (world units) over which the ambient intro gives way to a full beat.
#define SONG_INTRO_END_DISTANCE 650.0f
#define SONG_BUILD_END_DISTANCE 1300.0f
#define AUDIO_VALIDATION_HASH UINT64_C(0xf7534a0b56bce62e)

_Static_assert(ATOMIC_INT_LOCK_FREE == 2,
               "The audio callback requires lock-free atomic integers");

static SynthSystem *g_synth_ref = 0;

static void ConfigureSynthSeed(SynthSystem *synth, uint32_t runSeed);
static void StartSynthSFX(SynthSystem *synth, SFXType type);

static int SFXPriority(SFXType type) {
    if (type == SFX_BOSS_RISER || type == SFX_POWER_UP) return 3;
    if (type == SFX_GLITCH_HIT || type == SFX_EXPLOSION) return 2;
    if (type == SFX_LASER_CHARGE) return 1;
    return 0;
}

static unsigned int FloatBits(float value) {
    unsigned int bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static float BitsFloat(unsigned int bits) {
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static void ApplySynthControl(SynthSystem *synth, SynthControl control) {
    synth->targetIntensity = control.intensity;
    synth->targetBossIntensity = control.bossIntensity;
    synth->glitchAmount = control.glitchAmount;

    float introProgress = fmaxf(0.0f, fminf(
        control.virtualPlayerZ / SONG_INTRO_END_DISTANCE, 1.0f));
    float introEase = introProgress * introProgress * (3.0f - 2.0f * introProgress);
    synth->targetBpm = synth->ambientBpm +
                       (synth->baseBpm - synth->ambientBpm) * introEase;

    float buildProgress = fmaxf(0.0f, fminf(
        (control.virtualPlayerZ - SONG_INTRO_END_DISTANCE) /
        (SONG_BUILD_END_DISTANCE - SONG_INTRO_END_DISTANCE), 1.0f));
    synth->songBuildProgress = buildProgress;
    synth->targetDrumGate = buildProgress * (1.0f - control.preBossHush * 0.94f);
}

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
    // Slow minor movement with open extensions leaves ambiguity and space.
    // Each chord holds for two bars and glides into the next.
    static const signed char progressions[4][4] = {
        { 0, 8, 5, 10 },
        { 0, 5, 3, 10 },
        { 0, 10, 8, 5 },
        { 0, 3, 8, 10 }
    };
    int variant = (int)(SynthHash(synth->runSeed ^ 0x93a5f17du) & 3u);
    int chordRoot = progressions[variant][phrase & 3];
    int third = (chordRoot == 0 || chordRoot == 5) ? 3 : 4;
    int padRootMidi = synth->rootMidi + 12 + chordRoot;
    int voicing = (int)(SynthHash(synth->runSeed ^ (uint32_t)phrase * 0x45d9f3bu) & 3u);
    static const signed char upperIntervals[4][3] = {
        { 3, 7, 14 },
        { 3, 7, 10 },
        { 2, 7, 12 },
        { 7, 12, 19 }
    };

    synth->chordRoot = chordRoot;
    synth->chordThird = third;
    synth->padTargetFrequency[0] = MidiFrequency(padRootMidi);
    for (int note = 1; note < PAD_CHORD_NOTES; note++) {
        int interval = upperIntervals[voicing][note - 1];
        if (note == 1 && interval == 3) interval = third;
        synth->padTargetFrequency[note] = MidiFrequency(padRootMidi + interval);
    }
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

// One 2x-oversampled half-step of the 4-pole ladder: tanh-saturated stages
// with resonance fed back from the last stage, as in the classic Moog topology.
static void LadderHalfStep(LadderFilter *filter, float g, float resonance, float input) {
    float feedback = tanhf(input - resonance * filter->stage[3]);
    filter->stage[0] += g * (feedback - tanhf(filter->stage[0]));
    filter->stage[1] += g * (tanhf(filter->stage[0]) - tanhf(filter->stage[1]));
    filter->stage[2] += g * (tanhf(filter->stage[1]) - tanhf(filter->stage[2]));
    filter->stage[3] += g * (tanhf(filter->stage[2]) - tanhf(filter->stage[3]));
}

static float ProcessLadder(LadderFilter *filter, float input) {
    // `cutoff`/`resonance` keep the same control ranges the old SVF used;
    // rescale here into the ladder's stable coefficient/feedback ranges.
    float g = fminf(filter->cutoff * 3.5f, 0.98f) * 0.5f;
    float resonance = fminf(filter->resonance * 3.6f, 3.8f);
    LadderHalfStep(filter, g, resonance, input);
    LadderHalfStep(filter, g, resonance, input);
    return filter->stage[3];
}

static float ProcessComb(ReverbComb *comb, float input) {
    float output = comb->buffer[comb->index];
    comb->dampState += (output - comb->dampState) * comb->damp;
    comb->buffer[comb->index] = input + comb->dampState * comb->feedback;
    comb->index++;
    if (comb->index >= comb->size) comb->index = 0;
    return output;
}

static float ProcessAllpass(ReverbAllpass *allpass, float input) {
    float buffered = allpass->buffer[allpass->index];
    float output = buffered - input;
    allpass->buffer[allpass->index] = input + buffered * allpass->feedback;
    allpass->index++;
    if (allpass->index >= allpass->size) allpass->index = 0;
    return output;
}

// Tiny Schroeder/Freeverb-style tank: parallel damped combs into series
// allpasses, mono in (send mix) / stereo out (width read tapped off the tail).
static void ProcessReverb(SynthSystem *synth, float input, float *outLeft, float *outRight) {
    float combSum = 0.0f;
    for (int i = 0; i < REVERB_COMB_COUNT; i++) {
        combSum += ProcessComb(&synth->reverbCombs[i], input);
    }
    combSum *= 0.25f;

    float tank = combSum;
    for (int i = 0; i < REVERB_ALLPASS_COUNT; i++) {
        tank = ProcessAllpass(&synth->reverbAllpasses[i], tank);
    }

    ReverbAllpass *widthTap = &synth->reverbAllpasses[REVERB_ALLPASS_COUNT - 1];
    int widthIndex = widthTap->index - 17;
    if (widthIndex < 0) widthIndex += widthTap->size;
    *outLeft = tank;
    *outRight = widthTap->buffer[widthIndex];
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

// TR-909-style inharmonic square oscillator bank, ring-modulated against
// filtered noise to give hats/claps a metallic clang instead of plain hiss.
static float MetallicTone(SynthSystem *synth, float dt) {
    static const float ratios[3] = { 1.0f, 1.342f, 1.867f };
    float sum = 0.0f;
    for (int i = 0; i < 3; i++) {
        synth->hatMetalPhase[i] += 63.0f * ratios[i] * dt;
        if (synth->hatMetalPhase[i] >= 1.0f) synth->hatMetalPhase[i] -= 1.0f;
        sum += synth->hatMetalPhase[i] < 0.5f ? 1.0f : -1.0f;
    }
    return sum * (1.0f / 3.0f);
}

static void TriggerStep(SynthSystem *synth, float intensity, float bossIntensity) {
    synth->currentStep = (synth->currentStep + 1) & 15;
    int step = synth->currentStep;
    if (step == 0) {
        synth->barCount++;
        if ((synth->barCount & 1) == 0) SetPadChord(synth, synth->barCount >> 1);
    }

    // Alternating swing keeps each eighth-note pair the same length while
    // seeded micro timing prevents the halftime groove feeling quantized.
    uint32_t humanizeSeed = SynthHash(synth->runSeed ^ ((uint32_t)synth->barCount * 0x2f6e2b1u) ^
                                      ((uint32_t)step * 0x9e3779b1u));
    float swing = 0.008f + Hash01(synth->runSeed ^ 0x17c3u) * 0.007f;
    synth->stepJitter = ((step & 1) ? swing : -swing) +
                        (Hash01(humanizeSeed) - 0.5f) * 0.003f;
    synth->hitVelocity = 0.76f + Hash01(humanizeSeed ^ 0x55u) * 0.28f;

    static const unsigned short kickMasks[4] = {
        0x0001u, 0x0401u, 0x4001u, 0x0441u
    };
    static const unsigned short hatMasks[4] = {
        0x5120u, 0x8924u, 0x9210u, 0x4492u
    };
    static const unsigned short openHatMasks[4] = {
        0x2000u, 0x0200u, 0x0020u, 0x0800u
    };
    int rhythmVariant = (int)(SynthHash(synth->runSeed ^ 0x61d5a9u) & 3u);
    unsigned short stepBit = (unsigned short)(1u << step);
    unsigned short kickMask = 0x0001u;
    if (synth->songBuildProgress > 0.48f) kickMask |= kickMasks[rhythmVariant];
    if (bossIntensity > 0.58f) kickMask |= 0x2040u;
    if ((kickMask & stepBit) != 0u) {
        synth->kickTime = 0.0f;
        synth->kickPhase = 0.0f;
        synth->kickTrigger = true;
        synth->beatPulse = 1.0f;
    }

    int bassNote = synth->bassPattern[step];
    if (bassNote >= 0) {
        synth->bassEnv = 1.0f;
        synth->subEnv = 1.0f;
        synth->bassTargetFrequency = synth->rootFrequency *
                                     SemitoneRatio(synth->chordRoot + bassNote);
    } else if (bossIntensity > 0.58f && (step & 1) == 1) {
        synth->bassEnv = 0.42f + bossIntensity * 0.22f;
        synth->bassTargetFrequency = synth->rootFrequency *
                                     SemitoneRatio(synth->chordRoot + 12);
    }
    unsigned short hatMask = hatMasks[rhythmVariant];
    if (intensity > 0.74f) hatMask |= 0x0888u;
    if (bossIntensity > 0.48f) hatMask |= 0x2222u;
    if ((hatMask & stepBit) != 0u) synth->hatTime = 0.0f;
    if ((openHatMasks[rhythmVariant] & stepBit) != 0u &&
        synth->songBuildProgress > 0.34f) synth->openHatTime = 0.0f;
    if (step == 8 || (step == 14 && bossIntensity > 0.68f)) synth->clapTime = 0.0f;

    static const unsigned short motifMasks[4] = {
        0x4884u, 0x2488u, 0x8422u, 0x4212u
    };
    int motif = (int)(SynthHash(synth->runSeed ^ 0xc4ceb9feu) & 3u);
    unsigned short motifMask = motifMasks[motif];
    if (synth->songBuildProgress < 0.42f) motifMask &= 0x0444u;
    if (bossIntensity > 0.62f) motifMask |= 0x4008u;
    if ((motifMask & stepBit) != 0u && synth->songBuildProgress > 0.12f) {
        signed char chordTones[7] = {
            0, (signed char)synth->chordThird, 7, 10, 14,
            (signed char)(12 + synth->chordThird), 19
        };
        uint32_t toneSeed = SynthHash(synth->runSeed ^ (uint32_t)motif * 0x9e37u ^
                                      (uint32_t)step * 0x45d9u);
        int tone = (int)(toneSeed % 7u);
        int note = synth->chordRoot + chordTones[tone];
        synth->arpFrequency = synth->rootFrequency * SemitoneRatio(note + 12);
        synth->arpEnv = 0.62f + intensity * 0.14f + bossIntensity * 0.08f;
    }
}

static void RenderAudioFrames(SynthSystem *synth, short *output, unsigned int frames) {
    if (atomic_exchange_explicit(&synth->reseedPending, false, memory_order_acquire)) {
        uint32_t runSeed = atomic_load_explicit(&synth->pendingSeed, memory_order_relaxed);
        ConfigureSynthSeed(synth, runSeed);
    }
    unsigned int commandRead = atomic_load_explicit(&synth->sfxCommandRead,
                                                    memory_order_relaxed);
    unsigned int commandWrite = atomic_load_explicit(&synth->sfxCommandWrite,
                                                     memory_order_acquire);
    while (commandRead != commandWrite) {
        StartSynthSFX(synth, synth->sfxCommands[commandRead]);
        commandRead = (commandRead + 1u) % SFX_COMMAND_CAPACITY;
    }
    atomic_store_explicit(&synth->sfxCommandRead, commandRead, memory_order_release);

    unsigned int controlRead = atomic_load_explicit(&synth->controlRead,
                                                    memory_order_relaxed);
    unsigned int controlWrite = atomic_load_explicit(&synth->controlWrite,
                                                     memory_order_acquire);
    while (controlRead != controlWrite) {
        ApplySynthControl(synth, synth->controls[controlRead]);
        controlRead = (controlRead + 1u) % SYNTH_CONTROL_CAPACITY;
    }
    atomic_store_explicit(&synth->controlRead, controlRead, memory_order_release);
    const float dt = 1.0f / (float)SAMPLE_RATE;

    for (unsigned int frame = 0; frame < frames; frame++) {
        synth->intensity += (synth->targetIntensity - synth->intensity) * 0.00065f;
        float intensity = synth->intensity;
        synth->bossIntensity +=
            (synth->targetBossIntensity - synth->bossIntensity) * 0.00072f;
        float bossIntensity = synth->bossIntensity;
        float buildProgress = synth->songBuildProgress;
        // Slow, musical glide (multi-second time constant) rather than a snap -
        // only ever moves during the intro's ambientBpm -> baseBpm ramp now.
        synth->currentBpm += (synth->targetBpm - synth->currentBpm) * 0.00003f;
        const float secondsPerStep = (60.0f / synth->currentBpm) * 0.25f;
        synth->beatPulse = fmaxf(0.0f, synth->beatPulse - dt * 3.8f);
        synth->drumGate += (synth->targetDrumGate - synth->drumGate) * 0.00004f;

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
            if (synth->kickTime < 0.18f) {
                float attack = fminf(synth->kickTime / 0.005f, 1.0f);
                kickEnvelope = expf(-synth->kickTime * 16.0f) * attack * synth->drumGate;
                float pitch = 36.0f + 88.0f * expf(-synth->kickTime * 38.0f);
                float tone = AdvanceSine(&synth->kickPhase, pitch, dt) * kickEnvelope * synth->hitVelocity;
                float click = NextNoise(synth) * expf(-synth->kickTime * 1800.0f) *
                             synth->hitVelocity * synth->drumGate;
                kick = tanhf((tone * 1.65f + click * 0.28f) * 1.25f);
            } else {
                synth->kickTrigger = false;
            }
        }

        synth->bassFrequency += (synth->bassTargetFrequency - synth->bassFrequency) * 0.0018f;
        synth->bassPhase += synth->bassFrequency * dt;
        if (synth->bassPhase >= 1.0f) synth->bassPhase -= 1.0f;
        float saw = synth->bassPhase * 2.0f - 1.0f;
        float pulse = synth->bassPhase < 0.46f ? 1.0f : -1.0f;
        synth->bassEnv = fmaxf(0.0f, synth->bassEnv - dt * (3.7f - intensity * 1.1f));
        synth->bassFilter.cutoff = 0.026f + synth->bassEnv *
                                  (0.070f + intensity * 0.075f + bossIntensity * 0.040f);
        synth->bassFilter.resonance = 0.40f + resonanceLfo * 0.30f - intensity * 0.08f;
        float bassSource = saw * 0.72f + pulse * 0.28f;
        float bassSidechain = 1.0f - kickEnvelope * (0.18f + intensity * 0.05f);
        float bass = ProcessLadder(&synth->bassFilter, bassSource) * synth->bassEnv *
                    bassSidechain * synth->drumGate;

        synth->subPhase += synth->bassFrequency * dt;
        if (synth->subPhase >= 1.0f) synth->subPhase -= 1.0f;
        synth->subEnv = fmaxf(0.28f, synth->subEnv - dt * 0.34f);
        float subDuck = 1.0f - kickEnvelope * 0.14f;
        float subBass = sinf(synth->subPhase * 2.0f * PI) * synth->subEnv * subDuck *
                (0.15f + synth->drumGate * 0.10f + bossIntensity * 0.035f);

        synth->padLfoPhase += dt * (0.045f + Hash01(synth->runSeed ^ 0x51u) * 0.025f);
        if (synth->padLfoPhase >= 1.0f) synth->padLfoPhase -= 1.0f;
        float padLfo = sinf(synth->padLfoPhase * 2.0f * PI) * 0.5f + 0.5f;
        float padRawLeft = 0.0f;
        float padRawRight = 0.0f;
        // Dark unison bed: sine body supports restrained detuned saw movement.
        static const float voiceDetune[PAD_UNISON_VOICES] = { -1.0f, -0.5f, 0.0f, 0.5f, 1.0f };
        static const float voicePan[PAD_UNISON_VOICES] = { 0.05f, 0.30f, 0.5f, 0.70f, 0.95f };
        static const float voiceLevel[PAD_UNISON_VOICES] = { 0.68f, 0.85f, 1.0f, 0.85f, 0.68f };
        static const float voiceDriftRate[PAD_UNISON_VOICES] = { 0.031f, 0.047f, 0.019f, 0.053f, 0.037f };
        static const float notePan[PAD_CHORD_NOTES] = { 0.38f, 0.62f, 0.22f, 0.78f };
        float padSpread = 0.022f + intensity * 0.014f;
        for (int note = 0; note < PAD_CHORD_NOTES; note++) {
            synth->padFrequency[note] +=
                (synth->padTargetFrequency[note] - synth->padFrequency[note]) * 0.000025f;
            synth->padSinePhase[note] += synth->padFrequency[note] * dt;
            if (synth->padSinePhase[note] >= 1.0f) synth->padSinePhase[note] -= 1.0f;
            float padSine = sinf(synth->padSinePhase[note] * 2.0f * PI);
            padRawLeft += padSine * 0.20f * (1.0f - notePan[note]);
            padRawRight += padSine * 0.20f * notePan[note];

            for (int voice = 0; voice < PAD_UNISON_VOICES; voice++) {
                int index = note * PAD_UNISON_VOICES + voice;
                synth->padDrift[index] += dt * voiceDriftRate[voice];
                if (synth->padDrift[index] >= 1.0f) synth->padDrift[index] -= 1.0f;
                float drift = sinf(synth->padDrift[index] * 2.0f * PI) * 0.006f;
                float ratio = 1.0f + voiceDetune[voice] * padSpread + drift;
                synth->padPhase[index] += synth->padFrequency[note] * ratio * dt;
                if (synth->padPhase[index] >= 1.0f) synth->padPhase[index] -= 1.0f;
                float sawValue = (synth->padPhase[index] * 2.0f - 1.0f) *
                                 voiceLevel[voice] * 0.12f;
                float pan = voicePan[voice] * 0.72f + notePan[note] * 0.28f;
                padRawLeft += sawValue * (1.0f - pan);
                padRawRight += sawValue * pan;
            }
        }
        float padCutoff = 0.011f + padLfo * 0.010f + intensity * 0.016f;
        synth->padFilterLeft.cutoff = padCutoff;
        synth->padFilterRight.cutoff = padCutoff * 1.035f;
        synth->padFilterLeft.resonance = 0.58f - intensity * 0.08f;
        synth->padFilterRight.resonance = 0.56f - intensity * 0.08f;
        float padLeft = ProcessLadder(&synth->padFilterLeft, padRawLeft * 0.38f);
        float padRight = ProcessLadder(&synth->padFilterRight, padRawRight * 0.38f);
        float sidechain = 1.0f - kickEnvelope * (0.22f + intensity * 0.06f);
        float padGain = (0.16f + padLfo * 0.045f + intensity * 0.055f) * sidechain;
        padLeft *= padGain;
        padRight *= padGain;

        synth->arpEnv = fmaxf(0.0f, synth->arpEnv - dt * (2.8f - intensity * 0.6f));
        synth->arpPhase += synth->arpFrequency * dt;
        if (synth->arpPhase >= 1.0f) synth->arpPhase -= 1.0f;
        float arpSaw = synth->arpPhase * 2.0f - 1.0f;
        float arpTriangle = 1.0f - 4.0f * fabsf(synth->arpPhase - 0.5f);
        // Restrained phase modulation gives the sparse pluck a corroded bell edge.
        synth->arpModPhase += synth->arpFrequency * synth->arpModRatio * dt;
        if (synth->arpModPhase >= 1.0f) synth->arpModPhase -= 1.0f;
        float arpModulator = sinf(synth->arpModPhase * 2.0f * PI);
        float arpFmIndex = 0.42f + intensity * 0.62f;
        float arpFm = sinf((synth->arpPhase + arpModulator * arpFmIndex * 0.15f) * 2.0f * PI);
        synth->arpFilter.cutoff = 0.018f + synth->arpEnv * (0.052f + intensity * 0.038f);
        synth->arpFilter.resonance = 0.36f + resonanceLfo * 0.22f;
        float arpWave = ProcessLadder(&synth->arpFilter,
                          arpSaw * 0.08f + arpTriangle * 0.58f + arpFm * 0.34f);
        float melodyGain = 0.10f + intensity * 0.045f + bossIntensity * 0.035f;
        float arp = arpWave * synth->arpEnv *
                melodyGain * sidechain;

        float metallicTone = MetallicTone(synth, dt);

        synth->hatTime += dt;
        float hat = 0.0f;
        if (synth->hatTime < (intensity > 0.72f ? 0.072f : 0.045f)) {
            float noise = NextNoise(synth);
            float highNoise = noise - synth->previousNoise * 0.68f;
            synth->previousNoise = noise;
            float ringed = highNoise * (0.72f + 0.28f * metallicTone);
            hat = Drive(ringed * expf(-synth->hatTime * (intensity > 0.72f ? 42.0f : 68.0f)) *
                        synth->hitVelocity, 0.62f) * synth->drumGate;
        }

        synth->openHatTime += dt;
        float openHat = 0.0f;
        if (synth->openHatTime < 0.34f) {
            float noise = NextNoise(synth);
            float highNoise = noise - synth->previousNoise * 0.74f;
            synth->previousNoise = noise;
            float ringed = highNoise * (0.55f + 0.45f * metallicTone);
            openHat = Drive(ringed * expf(-synth->openHatTime * (10.0f - intensity * 1.2f)) *
                            synth->hitVelocity, 0.60f) * synth->drumGate;
        }

        synth->clapTime += dt;
        float clap = 0.0f;
        if (synth->clapTime < 0.27f) {
            float burst = expf(-synth->clapTime * 13.0f);
            float flutter = 0.62f + 0.38f * sinf(synth->clapTime * 2.0f * PI * 27.0f);
            float darkNoise = NextNoise(synth) + synth->previousNoise * 0.45f;
            clap = Drive(darkNoise * burst * flutter * synth->hitVelocity, 0.42f) *
                   synth->drumGate;
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
        float atmosphereLeft = ProcessLadder(&synth->atmosphereFilterLeft, NextNoise(synth)) *
                               (0.018f + intensity * 0.007f + bossIntensity * 0.008f);
        float atmosphereRight = ProcessLadder(&synth->atmosphereFilterRight, NextNoise(synth)) *
                                (0.018f + intensity * 0.007f + bossIntensity * 0.008f);
        float droneFrequency = synth->rootFrequency * 2.0f;
        float droneLeft = AdvanceSine(&synth->dronePhaseLeft, droneFrequency, dt);
        float droneRight = AdvanceSine(&synth->dronePhaseRight,
                                       droneFrequency * SemitoneRatio(7), dt);
        float hushLift = buildProgress * (1.0f - synth->drumGate);
        float droneGain = 0.018f + (1.0f - buildProgress) * 0.018f + hushLift * 0.025f;
        atmosphereLeft += (droneLeft * 0.78f + droneRight * 0.22f) * droneGain;
        atmosphereRight += (droneRight * 0.72f + droneLeft * 0.28f) * droneGain;

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

            if (sfxVoice->type == SFX_LASER_TAP) {
                // A compact low-mid pulse, metallic edge and transient replace
                // the exposed high-frequency sweep of an arcade laser.
                float pitchDecay = expf(-sfxVoice->time * 42.0f);
                float frequency = sfxVoice->freqEnd +
                                  (sfxVoice->freqStart - sfxVoice->freqEnd) * pitchDecay;
                float body = sinf(2.0f * PI * frequency * sfxVoice->time) * 0.82f;
                float edge = sinf(2.0f * PI * frequency * 2.73f * sfxVoice->time +
                                  sinf(2.0f * PI * 71.0f * sfxVoice->time) * 0.7f) * 0.24f;
                float transient = NextNoise(synth) * expf(-sfxVoice->time * 240.0f) * 0.55f;
                float attack = fminf(sfxVoice->time / 0.0015f, 1.0f);
                float envelope = attack * expf(-progress * 5.2f);
                sfx += SoftClip((body + edge + transient) * 1.45f) *
                       envelope * sfxVoice->volume;
            } else if (sfxVoice->type == SFX_LASER_CHARGE) {
                float pitchDecay = expf(-sfxVoice->time * 18.0f);
                float frequency = sfxVoice->freqEnd +
                                  (sfxVoice->freqStart - sfxVoice->freqEnd) * pitchDecay;
                float body = sinf(2.0f * PI * frequency * sfxVoice->time) * 0.78f +
                             sinf(2.0f * PI * frequency * 0.5f * sfxVoice->time) * 0.38f;
                float edge = sinf(2.0f * PI * frequency * 2.37f * sfxVoice->time) * 0.19f;
                float transient = NextNoise(synth) * expf(-sfxVoice->time * 150.0f) * 0.48f;
                float attack = fminf(sfxVoice->time / 0.003f, 1.0f);
                float envelope = attack * expf(-progress * 3.4f);
                sfx += SoftClip((body + edge + transient) * 1.35f) *
                       envelope * sfxVoice->volume;
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
                // Separate snap, body and debris make a kill immediately legible
                // while the resonant low-pass keeps the destruction smooth.
                sfxVoice->noiseFilter.cutoff = 0.32f * expf(-progress * 3.8f) + 0.018f;
                sfxVoice->noiseFilter.resonance = 0.30f;
                float rumble = ProcessLadder(&sfxVoice->noiseFilter, NextNoise(synth));
                float pitchDecay = expf(-sfxVoice->time * 12.0f);
                float subFrequency = sfxVoice->freqEnd +
                                     (sfxVoice->freqStart - sfxVoice->freqEnd) * pitchDecay;
                float sub = sinf(2.0f * PI * subFrequency * sfxVoice->time);
                float snap = NextNoise(synth) * expf(-sfxVoice->time * 180.0f) * 0.75f;
                float bodyEnvelope = expf(-progress * 3.1f);
                float debrisEnvelope = expf(-progress * 6.5f) * (0.35f + progress);
                float debris = NextNoise(synth) * debrisEnvelope * 0.18f;
                sfx += SoftClip((rumble * 0.78f + sub * 0.62f) * bodyEnvelope +
                                snap + debris) * sfxVoice->volume;
            }
        }

        unsigned int readIndex = (synth->delayIndex + SYNTH_DELAY_FRAMES - synth->delayFrames) & DELAY_MASK;
        unsigned int readIndexRight = (synth->delayIndex + SYNTH_DELAY_FRAMES -
                                      synth->delayFramesRight) & DELAY_MASK;
        float delayedLeft = synth->delayLeft[readIndex];
        float delayedRight = synth->delayRight[readIndexRight];
        unsigned int earlyIndex = (synth->delayIndex + SYNTH_DELAY_FRAMES -
                                   synth->delayFrames / 2u) & DELAY_MASK;
        unsigned int earlyIndexRight = (synth->delayIndex + SYNTH_DELAY_FRAMES -
                                       synth->delayFramesRight / 2u) & DELAY_MASK;
        float earlyLeft = synth->delayLeft[earlyIndex];
        float earlyRight = synth->delayRight[earlyIndexRight];
        // Third tap between the early reflection and the main echo thickens the
        // tail into something closer to a cheap diffuse reverb than one repeat.
        unsigned int lateIndex = (synth->delayIndex + SYNTH_DELAY_FRAMES -
                                 (synth->delayFrames * 3u) / 4u) & DELAY_MASK;
        unsigned int lateIndexRight = (synth->delayIndex + SYNTH_DELAY_FRAMES -
                                      (synth->delayFramesRight * 3u) / 4u) & DELAY_MASK;
        float lateLeft = synth->delayLeft[lateIndex];
        float lateRight = synth->delayRight[lateIndexRight];
        float rhythmGain = 0.030f + intensity * 0.036f + bossIntensity * 0.018f;
        float sendLeft = padLeft + arp * 1.08f + atmosphereLeft + clap * rhythmGain * 0.16f;
        float sendRight = padRight + arp * 1.08f + atmosphereRight + clap * rhythmGain * 0.18f;
        float feedback = 0.38f + intensity * 0.08f;
        synth->delayDampLeft += (delayedRight - synth->delayDampLeft) * 0.35f;
        synth->delayDampRight += (delayedLeft - synth->delayDampRight) * 0.35f;
        synth->delayLeft[synth->delayIndex] =
            SoftClip(sendLeft + synth->delayDampRight * feedback + earlyRight * 0.06f +
                    lateRight * 0.05f);
        synth->delayRight[synth->delayIndex] =
            SoftClip(sendRight + synth->delayDampLeft * feedback + earlyLeft * 0.06f +
                    lateLeft * 0.05f);
        synth->delayIndex = (synth->delayIndex + 1u) & DELAY_MASK;

        float reverbLeft = 0.0f;
        float reverbRight = 0.0f;
        ProcessReverb(synth, (sendLeft + sendRight) * 0.5f, &reverbLeft, &reverbRight);

        float reverbWet = 0.48f + intensity * 0.12f;
        float musicBedLeft = bass * (0.56f + bossIntensity * 0.08f) + subBass +
              padLeft + arp * 0.66f + hat * rhythmGain * 0.60f +
              openHat * rhythmGain * 0.24f + clap * rhythmGain * 0.30f +
              atmosphereLeft + delayedLeft * 0.32f + earlyRight * 0.04f +
              reverbLeft * reverbWet;
        float musicBedRight = bass * (0.54f + bossIntensity * 0.08f) + subBass +
               padRight + arp * 0.70f + hat * rhythmGain * 0.66f +
               openHat * rhythmGain * 0.28f + clap * rhythmGain * 0.34f +
               atmosphereRight + delayedRight * 0.32f + earlyLeft * 0.04f +
               reverbRight * reverbWet;
        float musicLeft = kick * (0.32f + bossIntensity * 0.015f) + musicBedLeft * 1.12f;
        float musicRight = kick * (0.32f + bossIntensity * 0.015f) + musicBedRight * 1.12f;
        float left = musicLeft * 1.42f + sfx * 0.18f;
        float right = musicRight * 1.42f + sfx * 0.18f;

        if (synth->glitchAmount > 0.08f) {
            float levels = 32.0f - synth->glitchAmount * 20.0f;
            left = floorf(left * levels) / levels;
            right = floorf(right * levels) / levels;
        }

        output[frame * 2u] = (short)(SoftClip(left) * 27800.0f);
        output[frame * 2u + 1u] = (short)(SoftClip(right) * 27800.0f);
    }

}

static void PublishSynthTelemetry(SynthSystem *synth, unsigned int frames,
                                  unsigned int elapsedMicros) {
    atomic_fetch_add_explicit(&synth->telemetrySequence, 1u, memory_order_acq_rel);
    atomic_store_explicit(&synth->telemetryBeatPulse, FloatBits(synth->beatPulse),
                          memory_order_relaxed);
    atomic_store_explicit(&synth->telemetryDrumGate, FloatBits(synth->drumGate),
                          memory_order_relaxed);
    atomic_store_explicit(&synth->telemetryBpm, FloatBits(synth->currentBpm),
                          memory_order_relaxed);
    atomic_store_explicit(&synth->lastCallbackFrames, frames, memory_order_relaxed);
    unsigned int previousMax = atomic_load_explicit(&synth->maxCallbackMicros,
                                                    memory_order_relaxed);
    while (elapsedMicros > previousMax &&
           !atomic_compare_exchange_weak_explicit(&synth->maxCallbackMicros,
                                                  &previousMax, elapsedMicros,
                                                  memory_order_relaxed,
                                                  memory_order_relaxed)) {
    }
    atomic_fetch_add_explicit(&synth->telemetrySequence, 1u, memory_order_release);
}

static void NativeAudioCallback(void *buffer, unsigned int frames) {
    unsigned int elapsedMicros = 0u;
#ifdef AUDIO_SYNTH_PROFILE
    struct timespec callbackStart;
    clock_gettime(CLOCK_MONOTONIC, &callbackStart);
#endif
    SynthSystem *synth = g_synth_ref;
    if (!synth) {
        memset(buffer, 0, frames * 2u * sizeof(short));
        return;
    }

    bool rendered = false;
    if (atomic_load_explicit(&synth->initialized, memory_order_acquire)) {
        RenderAudioFrames(synth, (short *)buffer, frames);
        rendered = true;
    } else {
        memset(buffer, 0, frames * 2u * sizeof(short));
    }
#ifdef AUDIO_SYNTH_PROFILE
    struct timespec callbackEnd;
    clock_gettime(CLOCK_MONOTONIC, &callbackEnd);
    int64_t elapsedNanos = (int64_t)(callbackEnd.tv_sec - callbackStart.tv_sec) *
                           INT64_C(1000000000) +
                           (int64_t)(callbackEnd.tv_nsec - callbackStart.tv_nsec);
    elapsedMicros = elapsedNanos > 0 ? (unsigned int)(elapsedNanos / 1000) : 0u;
#endif
    if (rendered) PublishSynthTelemetry(synth, frames, elapsedMicros);
}

static void ConfigureSynthSeed(SynthSystem *synth, uint32_t runSeed) {
    static const float fmRatios[4] = { 1.0f, 1.4983f, 2.37f, 3.01f };
    static const signed char bassPatterns[4][16] = {
        { 0, -1, -1, -1, -1, -1, 7, -1, 0, -1, -1, -1, 12, -1, -1, -1 },
        { 0, -1, -1, -1, 7, -1, -1, -1, 0, -1, -1, 12, -1, -1, 7, -1 },
        { 0, -1, -1, 7, -1, -1, -1, -1, 0, -1, 12, -1, -1, -1, -1, -1 },
        { 0, -1, -1, -1, -1, 12, -1, -1, 0, -1, -1, -1, -1, -1, 7, -1 }
    };

    synth->runSeed = runSeed;
    synth->noiseState = SynthHash(runSeed ^ 0xd1b54a35u);
    if (synth->noiseState == 0u) synth->noiseState = 0x94a5f31du;
    synth->baseBpm = GetSynthSeedBpm(runSeed);
    synth->ambientBpm = fmaxf(92.0f, synth->baseBpm - 20.0f);
    synth->targetBpm = synth->baseBpm;
    synth->rootMidi = 36 + SeedRootIndex(runSeed);
    synth->rootFrequency = MidiFrequency(synth->rootMidi - 12);
    synth->bassTargetFrequency = synth->rootFrequency;
    SetPadChord(synth, synth->barCount >> 1);
    synth->arpModRatio = fmRatios[SynthHash(runSeed ^ 0x2f1e7bu) & 3u];

    int bassVariant = (int)(SynthHash(runSeed ^ 0xb455u) & 3u);
    memcpy(synth->bassPattern, bassPatterns[bassVariant], sizeof(synth->bassPattern));

    float delaySeconds = (60.0f / synth->baseBpm) * 0.75f;
    synth->delayFrames = (unsigned int)(delaySeconds * (float)SAMPLE_RATE);
    if (synth->delayFrames >= SYNTH_DELAY_FRAMES) synth->delayFrames = SYNTH_DELAY_FRAMES - 1u;
    synth->delayFramesRight = (synth->delayFrames * 2u) / 3u;
    if (synth->delayFramesRight == 0u) synth->delayFramesRight = 1u;
}

static void InitializeSynthState(SynthSystem *synth, uint32_t runSeed) {
    *synth = (SynthSystem){ 0 };
    atomic_init(&synth->sfxCommandRead, 0u);
    atomic_init(&synth->sfxCommandWrite, 0u);
    atomic_init(&synth->controlRead, 0u);
    atomic_init(&synth->controlWrite, 0u);
    atomic_init(&synth->pendingSeed, runSeed);
    atomic_init(&synth->reseedPending, false);
    atomic_init(&synth->telemetryBeatPulse, FloatBits(0.0f));
    atomic_init(&synth->telemetryDrumGate, FloatBits(0.0f));
    atomic_init(&synth->telemetryBpm, FloatBits(0.0f));
    atomic_init(&synth->maxCallbackMicros, 0u);
    atomic_init(&synth->lastCallbackFrames, 0u);
    atomic_init(&synth->telemetrySequence, 0u);
    atomic_init(&synth->initialized, false);
    ConfigureSynthSeed(synth, runSeed);
    synth->currentBpm = synth->ambientBpm;
    synth->targetBpm = synth->ambientBpm;
    synth->targetIntensity = 0.24f;
    synth->intensity = 0.24f;
    synth->targetDrumGate = 0.0f;
    synth->drumGate = 0.0f;
    synth->songBuildProgress = 0.0f;
    synth->currentStep = 15;
    synth->stepTimer = (60.0f / synth->ambientBpm) * 0.25f;
    synth->kickTime = 1.0f;
    synth->hatTime = 1.0f;
    synth->openHatTime = 1.0f;
    synth->clapTime = 1.0f;
    synth->arpFrequency = 220.0f;

    synth->bassFrequency = synth->rootFrequency;
    synth->subEnv = 0.35f;
    for (int note = 0; note < PAD_CHORD_NOTES; note++) {
        synth->padFrequency[note] = synth->padTargetFrequency[note];
        synth->padSinePhase[note] = Hash01(runSeed + (uint32_t)note * 41u);
        for (int voice = 0; voice < PAD_UNISON_VOICES; voice++) {
            int index = note * PAD_UNISON_VOICES + voice;
            synth->padPhase[index] = Hash01(runSeed + (uint32_t)index * 17u);
            synth->padDrift[index] = Hash01(runSeed + (uint32_t)index * 31u + 9u);
        }
    }
    synth->bassFilter.cutoff = 0.08f;
    synth->bassFilter.resonance = 0.48f;

    static const int combSizes[REVERB_COMB_COUNT] = { 1116, 1188, 1277, 1356 };
    static const int allpassSizes[REVERB_ALLPASS_COUNT] = { 556, 441 };
    for (int i = 0; i < REVERB_COMB_COUNT; i++) {
        synth->reverbCombs[i].size = combSizes[i];
        synth->reverbCombs[i].feedback = 0.88f;
        synth->reverbCombs[i].damp = 0.14f;
    }
    for (int i = 0; i < REVERB_ALLPASS_COUNT; i++) {
        synth->reverbAllpasses[i].size = allpassSizes[i];
        synth->reverbAllpasses[i].feedback = 0.5f;
    }
}

void InitAudioSynth(SynthSystem *synth, uint32_t runSeed) {
    InitializeSynthState(synth, runSeed);
    SetAudioStreamBufferSizeDefault(BUFFER_FRAMES);
    InitAudioDevice();
    if (!IsAudioDeviceReady()) return;
    synth->stream = LoadAudioStream(SAMPLE_RATE, 16, 2);
    if (!IsAudioStreamValid(synth->stream)) {
        CloseAudioDevice();
        return;
    }

    g_synth_ref = synth;
    atomic_store_explicit(&synth->initialized, true, memory_order_release);
    SetAudioStreamCallback(synth->stream, NativeAudioCallback);
    PlayAudioStream(synth->stream);
}

void ReseedAudioSynth(SynthSystem *synth, uint32_t runSeed) {
    if (!atomic_load_explicit(&synth->initialized, memory_order_acquire)) {
        ConfigureSynthSeed(synth, runSeed);
        return;
    }

    atomic_store_explicit(&synth->pendingSeed, runSeed, memory_order_relaxed);
    atomic_store_explicit(&synth->reseedPending, true, memory_order_release);
}

void UpdateAudioSynth(SynthSystem *synth, float intensity, float glitchAmount,
                      float bossIntensity, float virtualPlayerZ, float preBossHush) {
    if (intensity < 0.0f) intensity = 0.0f;
    if (intensity > 1.0f) intensity = 1.0f;
    if (bossIntensity < 0.0f) bossIntensity = 0.0f;
    if (bossIntensity > 1.0f) bossIntensity = 1.0f;
    if (preBossHush < 0.0f) preBossHush = 0.0f;
    if (preBossHush > 1.0f) preBossHush = 1.0f;

    unsigned int controlWrite = atomic_load_explicit(&synth->controlWrite,
                                                     memory_order_relaxed);
    unsigned int nextWrite = (controlWrite + 1u) % SYNTH_CONTROL_CAPACITY;
    unsigned int controlRead = atomic_load_explicit(&synth->controlRead,
                                                    memory_order_acquire);
    if (nextWrite == controlRead) return;

    synth->controls[controlWrite] = (SynthControl) {
        intensity, glitchAmount, bossIntensity, virtualPlayerZ, preBossHush
    };
    atomic_store_explicit(&synth->controlWrite, nextWrite, memory_order_release);
}

static void StartSynthSFX(SynthSystem *synth, SFXType type) {
    int selectedVoice = -1;
    int selectedPriority = SFXPriority(type);
    float selectedProgress = -1.0f;
    for (int i = 0; i < MAX_SFX_VOICES; i++) {
        SFXVoice *candidate = &synth->sfxPool[i];
        if (!candidate->active) {
            selectedVoice = i;
            break;
        }

        int candidatePriority = SFXPriority(candidate->type);
        float candidateProgress = candidate->time / candidate->duration;
        if (candidatePriority < selectedPriority ||
            (candidatePriority == selectedPriority && candidateProgress > selectedProgress)) {
            selectedVoice = i;
            selectedPriority = candidatePriority;
            selectedProgress = candidateProgress;
        }
    }

    if (selectedVoice >= 0 && selectedPriority <= SFXPriority(type)) {
        SFXVoice *voice = &synth->sfxPool[selectedVoice];
        voice->type = type;
        voice->time = 0.0f;

        if (type == SFX_LASER_TAP) {
            voice->duration = 0.11f; voice->freqStart = 310.0f;
            voice->freqEnd = 118.0f; voice->volume = 0.42f;
        } else if (type == SFX_LASER_CHARGE) {
            voice->duration = 0.26f; voice->freqStart = 240.0f;
            voice->freqEnd = 68.0f; voice->volume = 0.52f;
        } else if (type == SFX_GLITCH_HIT) {
            voice->duration = 0.22f; voice->freqStart = 400.0f;
            voice->freqEnd = 50.0f; voice->volume = 0.70f;
        } else if (type == SFX_EXPLOSION) {
            voice->duration = 0.44f; voice->freqStart = 130.0f;
            voice->freqEnd = 38.0f; voice->volume = 0.66f;
            voice->noiseFilter.stage[0] = 0.0f; voice->noiseFilter.stage[1] = 0.0f;
            voice->noiseFilter.stage[2] = 0.0f; voice->noiseFilter.stage[3] = 0.0f;
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
    }
}

void TriggerSynthSFX(SynthSystem *synth, SFXType type) {
    if (!atomic_load_explicit(&synth->initialized, memory_order_acquire) ||
        type <= SFX_NONE || type > SFX_BOSS_RISER) return;

    unsigned int commandWrite = atomic_load_explicit(&synth->sfxCommandWrite,
                                                     memory_order_relaxed);
    unsigned int nextWrite = (commandWrite + 1u) % SFX_COMMAND_CAPACITY;
    unsigned int commandRead = atomic_load_explicit(&synth->sfxCommandRead,
                                                    memory_order_acquire);
    if (nextWrite == commandRead) return;

    synth->sfxCommands[commandWrite] = type;
    atomic_store_explicit(&synth->sfxCommandWrite, nextWrite, memory_order_release);
}

float GetSynthBeatPulse(const SynthSystem *synth) {
    return GetSynthTelemetry(synth).beatPulse;
}

float GetSynthDrumGate(const SynthSystem *synth) {
    return GetSynthTelemetry(synth).drumGate;
}

float GetSynthCurrentBpm(const SynthSystem *synth) {
    return GetSynthTelemetry(synth).currentBpm;
}

unsigned int GetSynthMaxCallbackMicros(const SynthSystem *synth) {
    return atomic_load_explicit(&synth->maxCallbackMicros, memory_order_relaxed);
}

unsigned int GetSynthLastCallbackFrames(const SynthSystem *synth) {
    return atomic_load_explicit(&synth->lastCallbackFrames, memory_order_relaxed);
}

SynthTelemetry GetSynthTelemetry(const SynthSystem *synth) {
    SynthTelemetry telemetry;
    for (;;) {
        unsigned int sequenceBefore = atomic_load_explicit(
            &synth->telemetrySequence, memory_order_acquire);
        if ((sequenceBefore & 1u) != 0u) continue;
        telemetry.beatPulse = BitsFloat(atomic_load_explicit(
            &synth->telemetryBeatPulse, memory_order_relaxed));
        telemetry.drumGate = BitsFloat(atomic_load_explicit(
            &synth->telemetryDrumGate, memory_order_relaxed));
        telemetry.currentBpm = BitsFloat(atomic_load_explicit(
            &synth->telemetryBpm, memory_order_relaxed));
        telemetry.maxCallbackMicros = atomic_load_explicit(
            &synth->maxCallbackMicros, memory_order_relaxed);
        telemetry.callbackFrames = atomic_load_explicit(
            &synth->lastCallbackFrames, memory_order_relaxed);
        unsigned int sequenceAfter = atomic_load_explicit(
            &synth->telemetrySequence, memory_order_acquire);
        if (sequenceBefore == sequenceAfter) return telemetry;
    }
}

static uint64_t RenderValidationPass(uint32_t runSeed, bool *hasSignal) {
    SynthSystem synth;
    short output[BUFFER_FRAMES * 2];
    uint64_t hash = UINT64_C(1469598103934665603);
    *hasSignal = false;

    InitializeSynthState(&synth, runSeed);
    atomic_store_explicit(&synth.initialized, true, memory_order_release);
    UpdateAudioSynth(&synth, 0.72f, 0.0f, 0.0f, 980.0f, 0.0f);
    TriggerSynthSFX(&synth, SFX_LASER_TAP);
    TriggerSynthSFX(&synth, SFX_EXPLOSION);

    for (int block = 0; block < 12; block++) {
        if (block == 4) {
            UpdateAudioSynth(&synth, 0.94f, 0.35f, 0.84f, 1550.0f, 0.0f);
            TriggerSynthSFX(&synth, SFX_BOSS_RISER);
        } else if (block == 8) {
            ReseedAudioSynth(&synth, runSeed ^ UINT32_C(0x9e3779b9));
            TriggerSynthSFX(&synth, SFX_POWER_UP);
        }

        RenderAudioFrames(&synth, output, BUFFER_FRAMES);
        for (unsigned int sample = 0; sample < BUFFER_FRAMES * 2u; sample++) {
            uint16_t value = (uint16_t)output[sample];
            if (value != 0u) *hasSignal = true;
            hash ^= value & 0xffu;
            hash *= UINT64_C(1099511628211);
            hash ^= value >> 8;
            hash *= UINT64_C(1099511628211);
        }
    }

    atomic_store_explicit(&synth.initialized, false, memory_order_release);
    return hash;
}

bool ValidateAudioSynth(uint64_t *pcmHash) {
    bool firstHasSignal;
    bool secondHasSignal;
    bool alternateHasSignal;
    uint64_t first = RenderValidationPass(94u, &firstHasSignal);
    uint64_t second = RenderValidationPass(94u, &secondHasSignal);
    uint64_t alternate = RenderValidationPass(1337u, &alternateHasSignal);
    if (pcmHash) *pcmHash = first;
    return firstHasSignal && secondHasSignal && alternateHasSignal &&
            first == second && first != alternate && first == AUDIO_VALIDATION_HASH;
}

void UnloadAudioSynth(SynthSystem *synth) {
    if (!atomic_load_explicit(&synth->initialized, memory_order_acquire)) return;
    atomic_store_explicit(&synth->initialized, false, memory_order_release);
    // Raylib 6.0 runs stream callbacks while holding its audio mutex;
    // StopAudioStream takes the same mutex, so returning here is the barrier.
    StopAudioStream(synth->stream);
    UnloadAudioStream(synth->stream);
    CloseAudioDevice();
    if (g_synth_ref == synth) g_synth_ref = 0;
}
