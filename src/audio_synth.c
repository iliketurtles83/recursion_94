#include "audio_synth.h"

#include <math.h>
#include <string.h>

#define PI 3.14159265358979323846f
#define DELAY_MASK (SYNTH_DELAY_FRAMES - 1u)
// Distance (world units) over which the ambient intro gives way to a full beat.
#define SONG_INTRO_END_DISTANCE 650.0f
#define SONG_BUILD_END_DISTANCE 1300.0f

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

static void TriggerStep(SynthSystem *synth, float intensity, float bossIntensity,
                        float buildProgress) {
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

    // Ambient warmup: a sparse downbeat-only melody carries the intro/buildup
    // instead of the denser reactive arp below, which only wakes up once the
    // beat has fully arrived.
    bool introMelodyStep = buildProgress < 1.0f && (step == 0 || step == 8);
    bool reactiveArpStep = buildProgress >= 1.0f && intensity > 0.28f &&
                           ((step & 1) == 0 || intensity > 0.62f || bossIntensity > 0.38f);
    if (introMelodyStep || reactiveArpStep) {
        signed char chordTones[8] = {
            0, (signed char)synth->chordThird, 7, 12,
            7, (signed char)(12 + synth->chordThird), 19, 24
        };
        uint32_t noteHash = SynthHash(synth->runSeed + (uint32_t)synth->barCount * 31u + (uint32_t)step * 7u);
        int note = synth->chordRoot + chordTones[noteHash & 7u];
        synth->arpFrequency = synth->rootFrequency * SemitoneRatio(note + 12);
        synth->arpEnv = introMelodyStep ? 0.55f : 0.35f + intensity * 0.65f;
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

    for (unsigned int frame = 0; frame < frames; frame++) {
        synth->intensity += (synth->targetIntensity - synth->intensity) * 0.00065f;
        float intensity = synth->intensity;
        synth->bossIntensity +=
            (synth->targetBossIntensity - synth->bossIntensity) * 0.00072f;
        float bossIntensity = synth->bossIntensity;
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
            TriggerStep(synth, intensity, bossIntensity, synth->songBuildProgress);
        }

        synth->resonanceLfoPhase += dt * (0.07f + Hash01(synth->runSeed ^ 0x27u) * 0.05f);
        if (synth->resonanceLfoPhase >= 1.0f) synth->resonanceLfoPhase -= 1.0f;
        float resonanceLfo = sinf(synth->resonanceLfoPhase * 2.0f * PI) * 0.5f + 0.5f;

        float kick = 0.0f;
        float kickEnvelope = 0.0f;
        if (synth->kickTrigger) {
            synth->kickTime += dt;
            if (synth->kickTime < 0.20f) {
                float attack = fminf(synth->kickTime / 0.004f, 1.0f);
                kickEnvelope = expf(-synth->kickTime * 14.0f) * attack * synth->drumGate;
                float pitch = 38.0f + 112.0f * expf(-synth->kickTime * 34.0f);
                float tone = AdvanceSine(&synth->kickPhase, pitch, dt) * kickEnvelope * synth->hitVelocity;
                // Fast decaying noise burst gives the attack a click a pure sweep lacks.
                float click = NextNoise(synth) * expf(-synth->kickTime * 1400.0f) *
                             synth->hitVelocity * synth->drumGate;
                kick = tanhf((tone * 1.7f + click * 0.5f) * 1.3f);
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
        float bass = ProcessLadder(&synth->bassFilter, bassSource) * synth->bassEnv *
                    bassSidechain * synth->drumGate;

        synth->padLfoPhase += dt * (0.045f + Hash01(synth->runSeed ^ 0x51u) * 0.025f);
        if (synth->padLfoPhase >= 1.0f) synth->padLfoPhase -= 1.0f;
        float padLfo = sinf(synth->padLfoPhase * 2.0f * PI) * 0.5f + 0.5f;
        float padRawLeft = 0.0f;
        float padRawRight = 0.0f;
        // Supersaw: PAD_UNISON_VOICES detuned saws per chord note, wide spread
        // plus slow independent per-voice drift, instead of a thin 2-osc blend.
        static const float voiceDetune[PAD_UNISON_VOICES] = { -1.0f, -0.5f, 0.0f, 0.5f, 1.0f };
        static const float voicePan[PAD_UNISON_VOICES] = { 0.05f, 0.30f, 0.5f, 0.70f, 0.95f };
        static const float voiceLevel[PAD_UNISON_VOICES] = { 0.68f, 0.85f, 1.0f, 0.85f, 0.68f };
        static const float voiceDriftRate[PAD_UNISON_VOICES] = { 0.031f, 0.047f, 0.019f, 0.053f, 0.037f };
        float padSpread = 0.045f + intensity * 0.025f;
        for (int note = 0; note < PAD_CHORD_NOTES; note++) {
            synth->padFrequency[note] +=
                (synth->padTargetFrequency[note] - synth->padFrequency[note]) * 0.000025f;
            synth->padSinePhase[note] += synth->padFrequency[note] * dt;
            if (synth->padSinePhase[note] >= 1.0f) synth->padSinePhase[note] -= 1.0f;
            float sub = sinf(synth->padSinePhase[note] * 2.0f * PI);
            float notePan = (float)note * 0.32f;
            padRawLeft += sub * 0.16f * (0.82f - notePan);
            padRawRight += sub * 0.16f * (0.18f + notePan);

            for (int voice = 0; voice < PAD_UNISON_VOICES; voice++) {
                int index = note * PAD_UNISON_VOICES + voice;
                synth->padDrift[index] += dt * voiceDriftRate[voice];
                if (synth->padDrift[index] >= 1.0f) synth->padDrift[index] -= 1.0f;
                float drift = sinf(synth->padDrift[index] * 2.0f * PI) * 0.006f;
                float ratio = 1.0f + voiceDetune[voice] * padSpread + drift;
                synth->padPhase[index] += synth->padFrequency[note] * ratio * dt;
                if (synth->padPhase[index] >= 1.0f) synth->padPhase[index] -= 1.0f;
                float sawValue = (synth->padPhase[index] * 2.0f - 1.0f) * voiceLevel[voice] * 0.20f;
                float pan = voicePan[voice] + notePan * 0.16f;
                padRawLeft += sawValue * (1.0f - pan);
                padRawRight += sawValue * pan;
            }
        }
        float padCutoff = 0.018f + padLfo * 0.016f + intensity * 0.020f;
        synth->padFilterLeft.cutoff = padCutoff;
        synth->padFilterRight.cutoff = padCutoff * 1.035f;
        synth->padFilterLeft.resonance = 0.58f - intensity * 0.08f;
        synth->padFilterRight.resonance = 0.56f - intensity * 0.08f;
        float padLeft = ProcessLadder(&synth->padFilterLeft, padRawLeft * 0.38f);
        float padRight = ProcessLadder(&synth->padFilterRight, padRawRight * 0.38f);
        float sidechain = 1.0f - kickEnvelope * (0.48f + intensity * 0.12f);
        float padGain = (0.13f + padLfo * 0.035f + intensity * 0.050f) * sidechain;
        padLeft *= padGain;
        padRight *= padGain;

        synth->arpEnv = fmaxf(0.0f, synth->arpEnv - dt * (5.6f - intensity * 1.4f));
        synth->arpPhase += synth->arpFrequency * dt;
        if (synth->arpPhase >= 1.0f) synth->arpPhase -= 1.0f;
        float arpSaw = synth->arpPhase * 2.0f - 1.0f;
        float arpTriangle = 1.0f - 4.0f * fabsf(synth->arpPhase - 0.5f);
        // 2-operator phase-modulation voice blended in for a modern FM lead edge.
        synth->arpModPhase += synth->arpFrequency * synth->arpModRatio * dt;
        if (synth->arpModPhase >= 1.0f) synth->arpModPhase -= 1.0f;
        float arpModulator = sinf(synth->arpModPhase * 2.0f * PI);
        float arpFmIndex = 0.6f + intensity * 1.4f;
        float arpFm = sinf((synth->arpPhase + arpModulator * arpFmIndex * 0.15f) * 2.0f * PI);
        synth->arpFilter.cutoff = 0.030f + synth->arpEnv * (0.085f + intensity * 0.065f);
        synth->arpFilter.resonance = 0.30f + resonanceLfo * 0.28f;
        float arpWave = ProcessLadder(&synth->arpFilter,
                                      arpSaw * 0.20f + arpTriangle * 0.48f + arpFm * 0.32f);
        float arp = arpWave * synth->arpEnv *
                    (intensity * 0.14f + bossIntensity * 0.050f) * sidechain;

        float metallicTone = MetallicTone(synth, dt);

        synth->hatTime += dt;
        float hat = 0.0f;
        if (synth->hatTime < (intensity > 0.72f ? 0.095f : 0.055f)) {
            float noise = NextNoise(synth);
            float highNoise = noise - synth->previousNoise * 0.82f;
            synth->previousNoise = noise;
            float ringed = highNoise * (0.55f + 0.45f * metallicTone);
            hat = Drive(ringed * expf(-synth->hatTime * (intensity > 0.72f ? 34.0f : 60.0f)) *
                        synth->hitVelocity, 0.85f) * synth->drumGate;
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
        if (synth->clapTime < 0.19f) {
            float burst = expf(-synth->clapTime * 18.0f);
            float flutter = 0.55f + 0.45f * sinf(synth->clapTime * 2.0f * PI * 34.0f);
            clap = Drive(NextNoise(synth) * burst * flutter * synth->hitVelocity, 0.55f) * synth->drumGate;
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
                               (0.014f + intensity * 0.006f + bossIntensity * 0.007f);
        float atmosphereRight = ProcessLadder(&synth->atmosphereFilterRight, NextNoise(synth)) *
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

            if (sfxVoice->type == SFX_LASER_TAP) {
                // Exponential glide + a light 2nd-harmonic layer and a fast
                // noise chiff on the attack read as a modern blaster instead
                // of a linear-sweep arcade "pew".
                float pitchDecay = expf(-sfxVoice->time * 34.0f);
                float frequency = sfxVoice->freqEnd +
                                  (sfxVoice->freqStart - sfxVoice->freqEnd) * pitchDecay;
                float tone = sinf(2.0f * PI * frequency * sfxVoice->time) * 0.72f +
                             sinf(2.0f * PI * frequency * 2.01f * sfxVoice->time) * 0.20f;
                float chiff = NextNoise(synth) * expf(-sfxVoice->time * 500.0f) * 0.35f;
                float attack = fminf(sfxVoice->time / 0.003f, 1.0f);
                float envelope = attack * expf(-progress * 7.0f);
                sfx += SoftClip((tone + chiff) * 1.3f) * envelope * sfxVoice->volume;
            } else if (sfxVoice->type == SFX_LASER_CHARGE) {
                // Deeper sub layer and a longer settle give the charged shot
                // more weight than the quick tap without the same "pew" whistle.
                float pitchDecay = expf(-sfxVoice->time * 14.0f);
                float frequency = sfxVoice->freqEnd +
                                  (sfxVoice->freqStart - sfxVoice->freqEnd) * pitchDecay;
                float tone = sinf(2.0f * PI * frequency * sfxVoice->time) * 0.75f +
                             sinf(2.0f * PI * frequency * 0.5f * sfxVoice->time) * 0.30f;
                float shimmer = NextNoise(synth) * (0.10f + progress * 0.05f);
                float attack = fminf(sfxVoice->time / 0.006f, 1.0f);
                float envelope = attack * expf(-progress * 3.0f) * (1.0f - progress * 0.15f);
                sfx += SoftClip((tone + shimmer) * 1.25f) * envelope * sfxVoice->volume;
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
                // Sweep a ladder filter across the noise (bright -> closed) for a
                // filtered "boom/crunch" instead of raw broadband static, plus a
                // pitch-dropping sub thump for body and a short crackle transient.
                sfxVoice->noiseFilter.cutoff = 0.42f * expf(-progress * 3.4f) + 0.02f;
                sfxVoice->noiseFilter.resonance = 0.22f;
                float rumble = ProcessLadder(&sfxVoice->noiseFilter, NextNoise(synth));
                float pitchDecay = expf(-sfxVoice->time * 9.0f);
                float subFrequency = sfxVoice->freqEnd +
                                     (sfxVoice->freqStart - sfxVoice->freqEnd) * pitchDecay;
                float sub = sinf(2.0f * PI * subFrequency * sfxVoice->time);
                float crackle = NextNoise(synth) * expf(-sfxVoice->time * 260.0f) * 0.6f;
                float envelope = expf(-progress * 11.0f) * 0.6f + expf(-progress * 2.6f) * 0.75f;
                sfx += SoftClip((rumble * 0.85f + sub * 0.5f + crackle) * 1.15f) *
                       envelope * sfxVoice->volume;
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

        float reverbLeft = 0.0f;
        float reverbRight = 0.0f;
        ProcessReverb(synth, (sendLeft + sendRight) * 0.5f, &reverbLeft, &reverbRight);

        float rhythmGain = 0.030f + intensity * 0.036f + bossIntensity * 0.018f;
        float reverbWet = 0.38f + intensity * 0.12f;
        float left = kick * (0.44f + bossIntensity * 0.02f) +
                     bass * (0.56f + bossIntensity * 0.08f) + padLeft + arp * 0.66f +
                     hat * rhythmGain * 0.60f + openHat * rhythmGain * 0.24f +
                     clap * rhythmGain * 0.30f + atmosphereLeft +
                     delayedLeft * 0.32f + earlyRight * 0.04f + sfx * 0.46f +
                     reverbLeft * reverbWet;
        float right = kick * (0.44f + bossIntensity * 0.02f) +
                      bass * (0.54f + bossIntensity * 0.08f) + padRight + arp * 0.70f +
                      hat * rhythmGain * 0.66f + openHat * rhythmGain * 0.28f +
                      clap * rhythmGain * 0.34f + atmosphereRight +
                      delayedRight * 0.32f + earlyLeft * 0.04f + sfx * 0.46f +
                      reverbRight * reverbWet;

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
    synth->ambientBpm = fmaxf(92.0f, synth->baseBpm - 20.0f);
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

    synth->rootMidi = 36 + SeedRootIndex(runSeed);
    synth->rootFrequency = MidiFrequency(synth->rootMidi - 12);
    synth->bassFrequency = synth->rootFrequency;
    synth->bassTargetFrequency = synth->rootFrequency;
    SetPadChord(synth, 0);
    for (int note = 0; note < PAD_CHORD_NOTES; note++) {
        synth->padFrequency[note] = synth->padTargetFrequency[note];
        synth->padSinePhase[note] = Hash01(runSeed + (uint32_t)note * 41u);
        for (int voice = 0; voice < PAD_UNISON_VOICES; voice++) {
            int index = note * PAD_UNISON_VOICES + voice;
            synth->padPhase[index] = Hash01(runSeed + (uint32_t)index * 17u);
            synth->padDrift[index] = Hash01(runSeed + (uint32_t)index * 31u + 9u);
        }
    }
    static const float fmRatios[4] = { 1.0f, 1.5f, 2.0f, 3.0f };
    synth->arpModRatio = fmRatios[SynthHash(runSeed ^ 0x2f1e7bu) & 3u];

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
    // Shorter offset tap for the right channel so echoes bounce L/R instead of
    // repeating in lockstep.
    synth->delayFramesRight = (synth->delayFrames * 2u) / 3u;
    if (synth->delayFramesRight == 0u) synth->delayFramesRight = 1u;

    static const int combSizes[REVERB_COMB_COUNT] = { 1116, 1188, 1277, 1356 };
    static const int allpassSizes[REVERB_ALLPASS_COUNT] = { 556, 441 };
    for (int i = 0; i < REVERB_COMB_COUNT; i++) {
        synth->reverbCombs[i].size = combSizes[i];
        synth->reverbCombs[i].feedback = 0.82f;
        synth->reverbCombs[i].damp = 0.20f;
    }
    for (int i = 0; i < REVERB_ALLPASS_COUNT; i++) {
        synth->reverbAllpasses[i].size = allpassSizes[i];
        synth->reverbAllpasses[i].feedback = 0.5f;
    }

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
                      float bossIntensity, float virtualPlayerZ, float preBossHush) {
    if (intensity < 0.0f) intensity = 0.0f;
    if (intensity > 1.0f) intensity = 1.0f;
    synth->targetIntensity = intensity;
    if (bossIntensity < 0.0f) bossIntensity = 0.0f;
    if (bossIntensity > 1.0f) bossIntensity = 1.0f;
    synth->targetBossIntensity = bossIntensity;
    synth->glitchAmount = glitchAmount;
    if (preBossHush < 0.0f) preBossHush = 0.0f;
    if (preBossHush > 1.0f) preBossHush = 1.0f;

    // BPM is fixed per seed once the intro ramp completes - no reactive nudging.
    float introProgress = fmaxf(0.0f, fminf(virtualPlayerZ / SONG_INTRO_END_DISTANCE, 1.0f));
    float introEase = introProgress * introProgress * (3.0f - 2.0f * introProgress);
    synth->targetBpm = synth->ambientBpm + (synth->baseBpm - synth->ambientBpm) * introEase;

    float buildProgress = fmaxf(0.0f, fminf(
        (virtualPlayerZ - SONG_INTRO_END_DISTANCE) /
        (SONG_BUILD_END_DISTANCE - SONG_INTRO_END_DISTANCE), 1.0f));
    synth->songBuildProgress = buildProgress;
    synth->targetDrumGate = buildProgress * (1.0f - preBossHush * 0.94f);
}

void TriggerSynthSFX(SynthSystem *synth, SFXType type) {
    if (!synth->initialized) return;
    for (int i = 0; i < MAX_SFX_VOICES; i++) {
        SFXVoice *voice = &synth->sfxPool[i];
        if (voice->active) continue;
        voice->type = type;
        voice->time = 0.0f;

        if (type == SFX_LASER_TAP) {
            voice->duration = 0.085f; voice->freqStart = 1500.0f;
            voice->freqEnd = 260.0f; voice->volume = 0.40f;
        } else if (type == SFX_LASER_CHARGE) {
            voice->duration = 0.20f; voice->freqStart = 1800.0f;
            voice->freqEnd = 140.0f; voice->volume = 0.58f;
        } else if (type == SFX_GLITCH_HIT) {
            voice->duration = 0.22f; voice->freqStart = 400.0f;
            voice->freqEnd = 50.0f; voice->volume = 0.70f;
        } else if (type == SFX_EXPLOSION) {
            voice->duration = 0.50f; voice->freqStart = 150.0f;
            voice->freqEnd = 32.0f; voice->volume = 0.80f;
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
        return;
    }
}

float GetSynthBeatPulse(const SynthSystem *synth) {
    return synth->beatPulse;
}

float GetSynthDrumGate(const SynthSystem *synth) {
    return synth->drumGate;
}

void UnloadAudioSynth(SynthSystem *synth) {
    if (!synth->initialized) return;
    synth->initialized = false;
    StopAudioStream(synth->stream);
    UnloadAudioStream(synth->stream);
    CloseAudioDevice();
    if (g_synth_ref == synth) g_synth_ref = 0;
}
