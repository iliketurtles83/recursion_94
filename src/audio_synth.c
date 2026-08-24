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
#define SONG_BUILD_END_DISTANCE 1250.0f
#define MUSIC_BUS_GAIN 1.42f
#define SFX_BUS_GAIN 0.22f
#define AUDIO_VALIDATION_HASH UINT64_C(0xe5b8d4ab7e1fd136)

_Static_assert(ATOMIC_INT_LOCK_FREE == 2,
               "The audio callback requires lock-free atomic integers");

static SynthSystem *g_synth_ref = 0;

static void ConfigureSynthSeed(SynthSystem *synth, uint32_t runSeed);
static void StartSynthSFX(SynthSystem *synth, SFXType type);

static int SFXPriority(SFXType type) {
    if (type == SFX_CORE_COLLAPSE) return 4;
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
    synth->targetPreBossHush = control.preBossHush;

    float introProgress = fmaxf(0.0f, fminf(
        control.virtualPlayerZ / SONG_INTRO_END_DISTANCE, 1.0f));
    float introEase = introProgress * introProgress * (3.0f - 2.0f * introProgress);
    synth->targetBpm = synth->ambientBpm +
                       (synth->baseBpm - synth->ambientBpm) * introEase;

    float buildProgress = fmaxf(0.0f, fminf(
        (control.virtualPlayerZ - SONG_INTRO_END_DISTANCE) /
        (SONG_BUILD_END_DISTANCE - SONG_INTRO_END_DISTANCE), 1.0f));
    synth->songBuildProgress = buildProgress;
    float dropProgress = buildProgress;
    dropProgress = dropProgress * dropProgress * (3.0f - 2.0f * dropProgress);
    synth->targetDrumGate = dropProgress * (1.0f - control.preBossHush * 0.94f);
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

static int ModeInterval(SynthScaleMode mode, int degree) {
    static const signed char aeolian[7] = { 0, 2, 3, 5, 7, 8, 10 };
    static const signed char dorian[7] = { 0, 2, 3, 5, 7, 9, 10 };
    int octave = degree / 7;
    int index = degree % 7;
    if (index < 0) {
        index += 7;
        octave--;
    }
    return (mode == SYNTH_MODE_DORIAN ? dorian[index] : aeolian[index]) + octave * 12;
}

float GetSynthSeedBpm(uint32_t runSeed) {
    return SYNTH_MIN_BPM + (float)(SynthHash(runSeed ^ 0x4b1d94a7u) % 16u);
}

const char *GetSynthSeedKeyName(uint32_t runSeed) {
    return (SynthHash(runSeed ^ 0xa53c9e1du) & 1u) != 0u ? "D DORIAN" : "D MINOR";
}

static float MidiFrequency(int midiNote) {
    return 440.0f * powf(2.0f, ((float)midiNote - 69.0f) / 12.0f);
}

static float SemitoneRatio(int semitones) {
    return powf(2.0f, (float)semitones / 12.0f);
}

static float SmoothRange(float value, float start, float end) {
    float progress = fmaxf(0.0f, fminf((value - start) / (end - start), 1.0f));
    return progress * progress * (3.0f - 2.0f * progress);
}

static void SetPadChord(SynthSystem *synth, int phrase) {
    static const signed char progressions[6][4] = {
        { 0, 5, 6, 3 },
        { 0, 3, 5, 6 },
        { 0, 6, 5, 3 },
        { 0, 4, 3, 5 },
        { 0, 2, 5, 3 },
        { 0, 5, 3, 6 }
    };
    static const signed char chordDegrees[PAD_CHORD_NOTES] = { 0, 2, 4, 6, 8 };
    int rootDegree = progressions[synth->progressionVariant][phrase & 3];
    int chordRoot = ModeInterval(synth->musicConfig.mode, rootDegree);
    int padRootMidi = synth->rootMidi + 12 + chordRoot;

    synth->chordRoot = chordRoot;
    synth->chordThird = ModeInterval(synth->musicConfig.mode, rootDegree + 2) - chordRoot;
    for (int note = 0; note < PAD_CHORD_NOTES; note++) {
        synth->padTargetFrequency[note] = MidiFrequency(
            padRootMidi + ModeInterval(synth->musicConfig.mode,
                                       rootDegree + chordDegrees[note]) - chordRoot);
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

static float ProcessLadderBandpass(LadderFilter *filter, float input) {
    ProcessLadder(filter, input);
    return (filter->stage[2] - filter->stage[3]) * 2.4f;
}

static float FilterControlFromHz(float frequency) {
    return fminf(0.32f, frequency * (2.0f / (float)SAMPLE_RATE));
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
    return sum * 0.24f;
}

static void TriggerStep(SynthSystem *synth, float intensity, float bossIntensity) {
    synth->currentStep = (synth->currentStep + 1) & 15;
    int step = synth->currentStep;
    if (step == 0) {
        synth->barCount++;
        SetPadChord(synth, synth->barCount - 1);
    }

    // Keep the 909/303 grid tight, with only tiny seeded analog-clock drift.
    uint32_t humanizeSeed = SynthHash(synth->runSeed ^ ((uint32_t)synth->barCount * 0x2f6e2b1u) ^
                                      ((uint32_t)step * 0x9e3779b1u));
    synth->stepJitter = (Hash01(humanizeSeed) - 0.5f) * 0.0014f;
    synth->hitVelocity = 0.90f + Hash01(humanizeSeed ^ 0x55u) * 0.14f;

    SynthMusicState nextState = SYNTH_STATE_AMBIENT;
    float buildProgress = synth->songBuildProgress;
    bool bossDrop = bossIntensity >= 0.45f;
    bool introComplete = buildProgress > 0.18f || bossDrop;
    if (introComplete) nextState = SYNTH_STATE_CRUISING;
    if (introComplete && (intensity >= 0.72f || bossIntensity >= 0.45f)) {
        nextState = SYNTH_STATE_HAZARD;
    }
    if (nextState != synth->musicState) {
        synth->transitionFxTime = 0.0f;
        synth->transitionFxDirection = nextState > synth->musicState ? 1.0f : -1.0f;
        synth->musicState = nextState;
    }

    bool kickActive = buildProgress > 0.60f || bossDrop;
    bool hatsActive = buildProgress > 0.34f || bossDrop;
    bool snareActive = buildProgress > 0.70f || bossDrop;
    if ((step & 3) == 0 && kickActive) {
        synth->kickTime = 0.0f;
        synth->kickPhase = 0.0f;
        synth->kickTrigger = true;
        synth->beatPulse = 1.0f;
    }

    BassStep bassStep = synth->bassPattern[step];
    if (bassStep.note >= 0 && (buildProgress > 0.18f || bossDrop)) {
        synth->bassSlide = (bassStep.flags & BASS_STEP_SLIDE) != 0u;
        synth->bassAccent = (bassStep.flags & BASS_STEP_ACCENT) != 0u ? 1.0f : 0.0f;
        synth->bassEnv = 1.0f;
        synth->bassFilterEnv = 1.0f;
        synth->subEnv = 1.0f;
        synth->bassTargetFrequency = synth->rootFrequency *
                                     SemitoneRatio(synth->chordRoot + bassStep.note);
    }

    static const float hatVelocities[4] = { 1.0f, 0.60f, 0.85f, 0.55f };
    if (hatsActive) synth->hatTime = 0.0f;
    synth->hatVelocity = hatVelocities[step & 3];
    if ((step & 3) == 2 && synth->musicState == SYNTH_STATE_HAZARD &&
        buildProgress > 0.76f) {
        synth->openHatTime = 0.0f;
    }
    bool fillBar = (synth->barCount & 7) == 0;
    bool snareRoll = snareActive && fillBar && step >= 10;
    if (snareActive && (step == 4 || step == 12 || snareRoll)) {
        synth->clapTime = 0.0f;
        synth->clapVelocity = snareRoll ? 0.35f + (float)(step - 10) * 0.10f : 1.0f;
    }

    static const unsigned short introRhythms[6] = {
        0x5555u, 0x4949u, 0x2525u, 0x9292u, 0x45a5u, 0x5151u
    };
    bool introArpStep = (introRhythms[synth->introMotifVariant] & (1u << step)) != 0u;
    bool arpStep = (buildProgress > 0.66f || bossDrop) ||
                   (buildProgress > 0.30f && (step & 1) == 0) || introArpStep;
    if (arpStep) {
        static const signed char arpPatterns[6][8] = {
            { 0, 1, 2, 0, 1, 2, 1, 0 },
            { 0, 2, 1, 3, 2, 1, 0, 2 },
            { 2, 1, 0, 1, 3, 2, 1, 0 },
            { 0, 3, 1, 2, 4, 2, 1, 3 },
            { 1, 0, 2, 3, 1, 4, 2, 0 },
            { 0, 2, 4, 3, 2, 0, 1, 3 }
        };
        int patternIndex = (step >> (synth->musicState == SYNTH_STATE_HAZARD ? 0 : 1)) & 7;
        int phraseTurn = (synth->barCount >> 2) & 1;
        if (phraseTurn != 0) patternIndex = 7 - patternIndex;
        int cellIndex = arpPatterns[synth->introMotifVariant][patternIndex];
        bool upperRegister = ((patternIndex + synth->introRegister) & 7) >= 4;
        bool octaveJump = Hash01(humanizeSeed ^ 0x8d31u) < 0.15f;
        synth->arpFrequency = synth->padTargetFrequency[cellIndex] *
                              SemitoneRatio((int)synth->introRegister * 12) *
                              ((upperRegister || octaveJump) ? 2.0f : 1.0f);
        float melodyBuild = SmoothRange(buildProgress, 0.12f, 0.62f);
        synth->arpEnv = 0.34f + melodyBuild * 0.44f + intensity * 0.10f;
    }

    if (synth->musicState == SYNTH_STATE_HAZARD && (buildProgress > 0.72f || bossDrop) &&
        (step == 0 || step == 3 || step == 6 || step == 10)) {
        int degree = (int)(SynthHash(humanizeSeed ^ 0x4ead1u) % 7u);
        synth->leadFrequency = synth->rootFrequency *
            SemitoneRatio(24 + ModeInterval(synth->musicConfig.mode, degree));
        synth->leadEnv = 1.0f;
    }
    if (synth->musicState == SYNTH_STATE_HAZARD && step == 0 &&
        (synth->barCount & 3) == 0) {
        synth->glitchRepeat = 0.55f + Hash01(humanizeSeed ^ 0xc173u) * 0.45f;
    }
}

static void RenderAudioFrames(SynthSystem *synth, short *output, unsigned int frames) {
    if (atomic_exchange_explicit(&synth->reseedPending, false, memory_order_acquire)) {
        uint32_t runSeed = atomic_load_explicit(&synth->pendingSeed, memory_order_relaxed);
        ConfigureSynthSeed(synth, runSeed);
        synth->musicState = SYNTH_STATE_AMBIENT;
        synth->songBuildProgress = 0.0f;
        synth->targetDrumGate = 0.0f;
        synth->drumGate = 0.0f;
        synth->barCount = 0;
        synth->currentStep = 15;
        synth->bassEnv = 0.0f;
        synth->bassFilterEnv = 0.0f;
        synth->subEnv = 0.0f;
        synth->arpEnv = 0.0f;
        synth->leadEnv = 0.0f;
        synth->kickTrigger = false;
        synth->hatTime = 1.0f;
        synth->openHatTime = 1.0f;
        synth->clapTime = 1.0f;
        synth->sidechainGain = 1.0f;
        synth->transitionFxTime = 0.0f;
        synth->transitionFxDirection = 1.0f;
        SetPadChord(synth, 0);
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
        synth->preBossHush +=
            (synth->targetPreBossHush - synth->preBossHush) * 0.0008f;
        float preBossHush = synth->preBossHush;
        float buildProgress = synth->songBuildProgress;
        float harmonyBuild = SmoothRange(buildProgress, 0.0f, 0.44f);
        float melodyBuild = SmoothRange(buildProgress, 0.12f, 0.62f);
        float bassBuild = SmoothRange(buildProgress, 0.18f, 0.64f);
        float hatBuild = SmoothRange(buildProgress, 0.34f, 0.80f);
        float kickBuild = SmoothRange(buildProgress, 0.60f, 0.96f);
        float snareBuild = SmoothRange(buildProgress, 0.70f, 1.0f);
        if (bossIntensity >= 0.45f) {
            harmonyBuild = 1.0f;
            melodyBuild = 1.0f;
            bassBuild = 1.0f;
            hatBuild = 1.0f;
            kickBuild = 1.0f;
            snareBuild = 1.0f;
        }
        // Slow, musical glide (multi-second time constant) rather than a snap -
        // only ever moves during the intro's ambientBpm -> baseBpm ramp now.
        synth->currentBpm += (synth->targetBpm - synth->currentBpm) * 0.00003f;
        const float secondsPerStep = (60.0f / synth->currentBpm) * 0.25f;
        synth->beatPulse = fmaxf(0.0f, synth->beatPulse - dt * 3.8f);
        synth->drumGate += (synth->targetDrumGate - synth->drumGate) * 0.00004f;
        float drumDrive = synth->musicState == SYNTH_STATE_AMBIENT ? 0.0f :
            synth->drumGate * (synth->musicState == SYNTH_STATE_HAZARD ? 1.18f : 0.92f);
        drumDrive *= 1.0f - preBossHush * 0.94f;

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
                kickEnvelope = expf(-synth->kickTime * 16.0f) * attack * drumDrive * kickBuild;
                float pitch = 48.0f + 112.0f * expf(-synth->kickTime * 48.0f);
                float tone = AdvanceSine(&synth->kickPhase, pitch, dt) * kickEnvelope * synth->hitVelocity;
                float click = NextNoise(synth) * expf(-synth->kickTime * 1800.0f) *
                             synth->hitVelocity * drumDrive * kickBuild;
                kick = tanhf((tone * 1.65f + click * 0.28f) * 1.25f);
            } else {
                synth->kickTrigger = false;
            }
        }

        float sidechainTarget = 1.0f - kickEnvelope * 0.68f;
        float sidechainSpeed = sidechainTarget < synth->sidechainGain ? 0.018f : 0.00016f;
        synth->sidechainGain += (sidechainTarget - synth->sidechainGain) * sidechainSpeed;

        float bassGlide = synth->bassSlide ? 0.00055f : 0.0038f;
        synth->bassFrequency += (synth->bassTargetFrequency - synth->bassFrequency) * bassGlide;
        synth->bassPhase += synth->bassFrequency * dt;
        if (synth->bassPhase >= 1.0f) synth->bassPhase -= 1.0f;
        float saw = synth->bassPhase * 2.0f - 1.0f;
        float triangle = 1.0f - 4.0f * fabsf(synth->bassPhase - 0.5f);
        synth->bassEnv *= expf(-dt * (7.2f - intensity * 0.8f));
        synth->bassFilterEnv *= expf(-dt * (17.0f - intensity * 2.0f));
        synth->bassAccent = fmaxf(0.0f, synth->bassAccent - dt * 7.5f);
        float baseCutoff = 0.014f + buildProgress * 0.012f;
        float envCutoff = 0.070f + synth->bassAccent * 0.045f;
        float lfoSweep = sinf((synth->resonanceLfoPhase * 0.5f +
                              (float)(synth->barCount & 7) * 0.125f) * 2.0f * PI) *
                         0.5f + 0.5f;
        float cutoff = baseCutoff + envCutoff * synth->bassFilterEnv +
                   lfoSweep * 0.018f + intensity * 0.035f;
        cutoff += preBossHush * (0.09f + resonanceLfo * 0.05f);
        if (synth->glitchAmount > 0.02f) {
            float cutoffSteps = 24.0f - synth->glitchAmount * 16.0f;
            cutoff = floorf(cutoff * cutoffSteps) / cutoffSteps;
            cutoff = fminf(cutoff, 0.24f - synth->glitchAmount * 0.08f);
        }
        synth->bassFilter.cutoff = fmaxf(0.012f, cutoff);
        synth->bassFilter.resonance = 0.48f + resonanceLfo * 0.10f;
        float bassSource = saw * 0.72f + triangle * 0.28f;
        float bassSidechain = synth->sidechainGain;
        float bassGate = bassBuild * (1.0f - preBossHush);
        float bass = Drive(ProcessLadder(&synth->bassFilter, bassSource * 1.08f),
                   0.52f + synth->bassAccent * 0.28f) * synth->bassEnv *
                     (0.68f + synth->bassAccent * 0.32f) * bassSidechain * bassGate;

        synth->subPhase += synth->bassFrequency * dt;
        if (synth->subPhase >= 1.0f) synth->subPhase -= 1.0f;
        synth->subEnv = fmaxf(0.28f, synth->subEnv - dt * 0.34f);
        float subDuck = 0.55f + synth->sidechainGain * 0.45f;
        float subBass = sinf(synth->subPhase * 2.0f * PI) * synth->subEnv * subDuck *
            (0.08f + synth->drumGate * 0.08f) * (1.0f - preBossHush) *
            bassBuild;

        synth->padLfoPhase += dt * 0.02f;
        if (synth->padLfoPhase >= 1.0f) synth->padLfoPhase -= 1.0f;
        float padLfo = sinf(synth->padLfoPhase * 2.0f * PI) * 0.5f + 0.5f;
        float padRawLeft = 0.0f;
        float padRawRight = 0.0f;
        static const float voiceDetune[PAD_UNISON_VOICES] = { -1.0f, -0.52f, 0.0f, 0.52f, 1.0f };
        static const float voicePan[PAD_UNISON_VOICES] = { 0.0f, 0.12f, 0.5f, 0.88f, 1.0f };
        static const float voiceLevel[PAD_UNISON_VOICES] = { 0.72f, 0.88f, 1.0f, 0.88f, 0.72f };
        static const float voiceDriftRate[PAD_UNISON_VOICES] = { 0.031f, 0.047f, 0.019f, 0.053f, 0.037f };
        static const float notePan[PAD_CHORD_NOTES] = { 0.5f, 0.18f, 0.82f, 0.30f, 0.70f };
        float padSpread = 0.020f + buildProgress * 0.013f + intensity * 0.006f;
        float hazardBlend = synth->musicState == SYNTH_STATE_HAZARD ?
                    SmoothRange(buildProgress, 0.72f, 1.0f) : 0.0f;
        float padSineLevel = 0.32f - harmonyBuild * 0.14f - hazardBlend * 0.06f;
        float padSawLevel = 0.052f + harmonyBuild * 0.033f + hazardBlend * 0.020f;
        for (int note = 0; note < PAD_CHORD_NOTES; note++) {
            synth->padFrequency[note] +=
                (synth->padTargetFrequency[note] - synth->padFrequency[note]) * 0.00011f;
            synth->padSinePhase[note] += synth->padFrequency[note] * dt;
            if (synth->padSinePhase[note] >= 1.0f) synth->padSinePhase[note] -= 1.0f;
            float padSine = sinf(synth->padSinePhase[note] * 2.0f * PI);
            padRawLeft += padSine * padSineLevel * (1.0f - notePan[note]);
            padRawRight += padSine * padSineLevel * notePan[note];

            for (int voice = 0; voice < PAD_UNISON_VOICES; voice++) {
                int index = note * PAD_UNISON_VOICES + voice;
                synth->padDrift[index] += dt * voiceDriftRate[voice];
                if (synth->padDrift[index] >= 1.0f) synth->padDrift[index] -= 1.0f;
                float drift = sinf(synth->padDrift[index] * 2.0f * PI) * 0.006f;
                float ratio = 1.0f + voiceDetune[voice] * padSpread + drift;
                synth->padPhase[index] += synth->padFrequency[note] * ratio * dt;
                if (synth->padPhase[index] >= 1.0f) synth->padPhase[index] -= 1.0f;
                float sawValue = (synth->padPhase[index] * 2.0f - 1.0f) *
                                 voiceLevel[voice] * padSawLevel;
                float pan = voicePan[voice] * 0.72f + notePan[note] * 0.28f;
                padRawLeft += sawValue * (1.0f - pan);
                padRawRight += sawValue * pan;
            }
        }
        float padCutoff = 0.012f + padLfo * 0.026f +
              harmonyBuild * 0.040f + hazardBlend * 0.050f +
              intensity * 0.012f;
        synth->padFilterLeft.cutoff = padCutoff * (0.97f + padLfo * 0.03f);
        synth->padFilterRight.cutoff = padCutoff * (1.04f - padLfo * 0.03f);
        synth->padFilterLeft.resonance = 0.45f + padLfo * 0.10f;
        synth->padFilterRight.resonance = 0.55f - padLfo * 0.10f;
        float padLeft = ProcessLadder(&synth->padFilterLeft, padRawLeft * 0.38f);
        float padRight = ProcessLadder(&synth->padFilterRight, padRawRight * 0.38f);
        float sidechain = synth->sidechainGain;
        float padGain = (0.16f + padLfo * 0.045f + intensity * 0.055f) * sidechain;
        padLeft *= padGain;
        padRight *= padGain;

        synth->arpEnv *= 0.999811f;
        if (synth->arpEnv < 0.0001f) synth->arpEnv = 0.0f;
        synth->arpPhase += synth->arpFrequency * dt;
        if (synth->arpPhase >= 1.0f) synth->arpPhase -= 1.0f;
        float arpSaw = synth->arpPhase * 2.0f - 1.0f;
        float arpSquare = synth->arpPhase < 0.5f ? 1.0f : -1.0f;
        float arpTriangle = 1.0f - 4.0f * fabsf(synth->arpPhase - 0.5f);
        // A small phase-modulated edge helps the filtered pluck sparkle in the echoes.
        synth->arpModPhase += synth->arpFrequency * synth->arpModRatio * dt;
        if (synth->arpModPhase >= 1.0f) synth->arpModPhase -= 1.0f;
        float arpModulator = sinf(synth->arpModPhase * 2.0f * PI);
        float arpFmIndex = 0.30f + intensity * 0.44f;
        float arpFm = sinf((synth->arpPhase + arpModulator * arpFmIndex * 0.15f) * 2.0f * PI);
        float arpSine = sinf(synth->arpPhase * 2.0f * PI);
        float arpCutoffLimit = FilterControlFromHz(950.0f) + melodyBuild *
            (0.28f - FilterControlFromHz(950.0f));
        synth->arpFilter.cutoff = fminf(arpCutoffLimit, 0.018f + synth->arpEnv *
            (0.055f + buildProgress * 0.12f) + preBossHush * 0.04f);
        synth->arpFilter.resonance = 0.58f + resonanceLfo * 0.10f;
        float introArpSource;
        if (synth->introTimbreVariant == 0u) {
            introArpSource = arpTriangle * 0.62f + arpFm * 0.38f;
        } else if (synth->introTimbreVariant == 1u) {
            introArpSource = arpSine * 0.72f + arpTriangle * 0.28f;
        } else {
            introArpSource = arpSquare * 0.28f + arpTriangle * 0.44f + arpFm * 0.28f;
        }
        float cruisingArpSource = arpSquare * 0.58f + arpTriangle * 0.32f + arpFm * 0.10f;
        float hazardArpSource = arpSaw * 0.34f + arpSquare * 0.24f + arpFm * 0.42f;
        float arpSource = introArpSource +
            (cruisingArpSource - introArpSource) * melodyBuild;
        arpSource += (hazardArpSource - arpSource) * hazardBlend;
        float arpWave = ProcessLadder(&synth->arpFilter, arpSource);
        float melodyGain = 0.10f + intensity * 0.045f + bossIntensity * 0.035f;
        float arp = arpWave * synth->arpEnv *
                melodyGain * sidechain;

        synth->leadEnv = fmaxf(0.0f, synth->leadEnv - dt * 2.8f);
        synth->leadPhase += synth->leadFrequency * dt;
        if (synth->leadPhase >= 1.0f) synth->leadPhase -= 1.0f;
        synth->leadFilter.cutoff = 0.12f + synth->leadEnv * 0.16f;
        synth->leadFilter.resonance = 0.62f;
        float leadSource = synth->leadPhase * 2.0f - 1.0f;
        float lead = Drive(ProcessLadder(&synth->leadFilter, leadSource), 2.6f) *
                 synth->leadEnv * 0.16f;

        float metallicTone = MetallicTone(synth, dt);

        synth->hatTime += dt;
        float hat = 0.0f;
        if (synth->hatTime < (intensity > 0.72f ? 0.072f : 0.045f)) {
            float noise = NextNoise(synth);
            float highNoise = noise - synth->previousNoise * 0.68f;
            synth->previousNoise = noise;
            float ringed = highNoise * (0.82f + 0.18f * metallicTone);
            hat = Drive(ringed * expf(-synth->hatTime * (intensity > 0.72f ? 42.0f : 68.0f)) *
                        synth->hatVelocity, 0.62f) * drumDrive * hatBuild;
        }

        synth->openHatTime += dt;
        float openHat = 0.0f;
        if (synth->openHatTime < 0.34f) {
            float noise = NextNoise(synth);
            float highNoise = noise - synth->previousNoise * 0.74f;
            synth->previousNoise = noise;
            float ringed = highNoise * (0.74f + 0.26f * metallicTone);
            openHat = Drive(ringed * expf(-synth->openHatTime * (10.0f - intensity * 1.2f)) *
                            synth->hatVelocity, 0.60f) * drumDrive * hatBuild;
        }

        synth->clapTime += dt;
        float clap = 0.0f;
        if (synth->clapTime < 0.27f) {
            float burst = expf(-synth->clapTime * 13.0f);
            float flutter = 0.62f + 0.38f * sinf(synth->clapTime * 2.0f * PI * 27.0f);
                        float muffledNoise = NextNoise(synth) + synth->previousNoise * 0.58f;
                        float snareBody = sinf(2.0f * PI * (185.0f - synth->clapTime * 210.0f) *
                                                                     synth->clapTime) * expf(-synth->clapTime * 18.0f);
                        clap = Drive((muffledNoise * burst * flutter + snareBody * 0.72f) *
                                                 synth->clapVelocity, 0.34f) * drumDrive * snareBuild;
        }

        synth->atmospherePhase += dt * (0.013f + Hash01(synth->runSeed ^ 0x713u) * 0.007f);
        if (synth->atmospherePhase >= 1.0f) synth->atmospherePhase -= 1.0f;
        float atmosphereLfo = sinf(synth->atmospherePhase * 2.0f * PI) * 0.5f + 0.5f;
        float windFrequency = 300.0f + atmosphereLfo * 3500.0f;
        synth->atmosphereFilterLeft.cutoff = FilterControlFromHz(windFrequency);
        synth->atmosphereFilterRight.cutoff = FilterControlFromHz(
            300.0f + (1.0f - atmosphereLfo * 0.88f) * 3500.0f);
        synth->atmosphereFilterLeft.resonance = 0.72f;
        synth->atmosphereFilterRight.resonance = 0.69f;
        float windGain = (0.026f + (1.0f - buildProgress) * 0.018f + intensity * 0.006f) *
                 (synth->musicState == SYNTH_STATE_AMBIENT ? 0.0f : 1.0f);
        float atmosphereLeft = ProcessLadderBandpass(
            &synth->atmosphereFilterLeft, NextNoise(synth)) * windGain;
        float atmosphereRight = ProcessLadderBandpass(
            &synth->atmosphereFilterRight, NextNoise(synth)) * windGain;
        float hushLift = buildProgress * (1.0f - synth->drumGate);
        atmosphereLeft *= 1.0f + hushLift * 0.45f;
        atmosphereRight *= 1.0f + hushLift * 0.45f;

        synth->transitionFxTime += dt;
        float transitionFx = 0.0f;
        if (synth->transitionFxTime < 2.4f) {
            float progress = synth->transitionFxTime / 2.4f;
            float sweep = synth->transitionFxDirection > 0.0f ? progress : 1.0f - progress;
            synth->transitionFilter.cutoff = FilterControlFromHz(180.0f + sweep * 7200.0f);
            synth->transitionFilter.resonance = 0.68f;
            transitionFx = ProcessLadderBandpass(&synth->transitionFilter, NextNoise(synth)) *
                           sinf(progress * PI) * 0.11f;
        }
        synth->glitchRepeat = fmaxf(0.0f, synth->glitchRepeat - dt * 1.7f);
        float glitchDownlifter = 0.0f;
        if (synth->glitchRepeat > 0.0f) {
            float stepped = floorf(synth->glitchRepeat * 12.0f) / 12.0f;
            glitchDownlifter = ProcessLadderBandpass(&synth->transitionFilter, NextNoise(synth)) *
                               stepped * 0.08f;
        }

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
                float pitchDecay = expf(-sfxVoice->time * 10.0f);
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
                float eased = progress * progress * (3.0f - 2.0f * progress);
                float frequency = sfxVoice->freqStart +
                                  (sfxVoice->freqEnd - sfxVoice->freqStart) * eased;
                float body = sinf(2.0f * PI * frequency * sfxVoice->time) * 0.72f +
                             sinf(2.0f * PI * frequency * 1.007f * sfxVoice->time) * 0.28f;
                float breath = NextNoise(synth) * sinf(progress * PI) * 0.07f;
                float attack = fminf(sfxVoice->time / 0.045f, 1.0f);
                float envelope = attack * sinf(progress * PI);
                sfx += SoftClip(body + breath) * envelope * sfxVoice->volume;
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
            } else if (sfxVoice->type == SFX_CORE_COLLAPSE) {
                float implosion = 1.0f - progress;
                float frequency = sfxVoice->freqEnd +
                                  (sfxVoice->freqStart - sfxVoice->freqEnd) * implosion * implosion;
                float sub = sinf(2.0f * PI * frequency * sfxVoice->time) *
                            (0.68f + 0.22f * sinf(2.0f * PI * 3.0f * sfxVoice->time));
                sfxVoice->noiseFilter.cutoff = 0.24f * implosion + 0.012f;
                sfxVoice->noiseFilter.resonance = 0.42f + progress * 0.24f;
                float debris = ProcessLadder(&sfxVoice->noiseFilter, NextNoise(synth));
                float fracture = sinf(2.0f * PI * (94.0f + progress * 310.0f) *
                                      sfxVoice->time + sinf(sfxVoice->time * 37.0f) * 2.4f);
                float pulse = 0.55f + 0.45f * fmaxf(0.0f,
                    sinf(2.0f * PI * (4.0f + progress * 7.0f) * sfxVoice->time));
                float envelope = sinf(progress * PI) * expf(-progress * 0.72f);
                float impact = NextNoise(synth) * expf(-sfxVoice->time * 28.0f);
                sfx += SoftClip(sub * 0.92f + debris * 0.58f + fracture * 0.18f * pulse + impact) *
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
        float rhythmGain = 0.024f + intensity * 0.028f + bossIntensity * 0.014f;
        float sendLeft = padLeft + arp * 1.42f + lead * 0.38f + atmosphereLeft +
                 transitionFx + glitchDownlifter + clap * rhythmGain * 0.16f;
        float sendRight = padRight + arp * 1.42f + lead * 0.42f + atmosphereRight +
                  transitionFx - glitchDownlifter + clap * rhythmGain * 0.18f;
        float feedback = 0.50f + intensity * 0.08f + buildProgress * 0.10f +
                 preBossHush * 0.08f;
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

        float reverbWet = 0.38f + intensity * 0.08f + preBossHush * 0.22f;
        float musicBedLeft = bass * (0.56f + bossIntensity * 0.08f) + subBass +
              padLeft + arp * 0.66f + lead + hat * rhythmGain * 0.60f +
              openHat * rhythmGain * 0.24f + clap * rhythmGain * 0.30f +
              atmosphereLeft + delayedLeft * 0.32f + earlyRight * 0.04f +
              reverbLeft * reverbWet;
        float musicBedRight = bass * (0.54f + bossIntensity * 0.08f) + subBass +
               padRight + arp * 0.70f + lead + hat * rhythmGain * 0.66f +
               openHat * rhythmGain * 0.28f + clap * rhythmGain * 0.34f +
               atmosphereRight + delayedRight * 0.32f + earlyLeft * 0.04f +
               reverbRight * reverbWet;
        float musicLeft = kick * (0.26f + bossIntensity * 0.015f) + musicBedLeft * 1.12f;
        float musicRight = kick * (0.26f + bossIntensity * 0.015f) + musicBedRight * 1.12f;
        float left = musicLeft * MUSIC_BUS_GAIN + sfx * SFX_BUS_GAIN;
        float right = musicRight * MUSIC_BUS_GAIN + sfx * SFX_BUS_GAIN;

        if (synth->glitchAmount > 0.08f) {
            float levels = 32.0f - synth->glitchAmount * 20.0f;
            left = floorf(left * levels) / levels;
            right = floorf(right * levels) / levels;
        }

        output[frame * 2u] = (short)(SoftClip(left) * 30000.0f);
        output[frame * 2u + 1u] = (short)(SoftClip(right) * 30000.0f);
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
    static const signed char bassOctaves[4][16] = {
        { 0,0,0,12, 0,0,12,0, 0,0,0,12, 0,12,0,0 },
        { 0,0,12,0, 0,12,0,0, 0,0,12,0, 12,0,0,0 },
        { 0,12,0,0, 0,0,12,0, 0,12,0,0, 0,0,12,0 },
        { 0,0,0,0, 12,0,0,12, 0,0,0,0, 12,0,12,0 }
    };

    synth->runSeed = runSeed;
    synth->noiseState = SynthHash(runSeed ^ 0xd1b54a35u);
    if (synth->noiseState == 0u) synth->noiseState = 0x94a5f31du;
    synth->baseBpm = synth->musicConfig.bpm;
    synth->ambientBpm = synth->baseBpm - 6.0f;
    synth->targetBpm = synth->baseBpm;
    synth->rootMidi = synth->musicConfig.rootMidi;
    synth->rootFrequency = MidiFrequency(synth->rootMidi - 12);
    synth->bassTargetFrequency = synth->rootFrequency;
    synth->progressionVariant = (unsigned char)(
        SynthHash(runSeed ^ 0x93a5f17du) % 6u);
    synth->introMotifVariant = (unsigned char)(
        SynthHash(runSeed ^ 0x6c8e9cf5u) % 6u);
    synth->introTimbreVariant = (unsigned char)(
        SynthHash(runSeed ^ 0x1f123bb5u) % 3u);
    synth->introRegister = (unsigned char)(
        SynthHash(runSeed ^ 0x748f2a91u) % 2u);
    SetPadChord(synth, synth->barCount >> 1);
    synth->arpModRatio = fmRatios[SynthHash(runSeed ^ 0x2f1e7bu) & 3u];

    int bassVariant = (int)(SynthHash(runSeed ^ 0xb455u) & 3u);
    for (int step = 0; step < 16; step++) {
        synth->bassPattern[step].note = bassOctaves[bassVariant][step];
        synth->bassPattern[step].flags = (unsigned char)(
            (step & 3) == 0 ? BASS_STEP_ACCENT : 0u);
    }

    float beatSeconds = 60.0f / synth->baseBpm;
    synth->delayFrames = (unsigned int)(beatSeconds * 0.75f * (float)SAMPLE_RATE);
    if (synth->delayFrames >= SYNTH_DELAY_FRAMES) synth->delayFrames = SYNTH_DELAY_FRAMES - 1u;
    synth->delayFramesRight = (unsigned int)(beatSeconds * 0.755f * (float)SAMPLE_RATE);
    if (synth->delayFramesRight >= SYNTH_DELAY_FRAMES) {
        synth->delayFramesRight = SYNTH_DELAY_FRAMES - 1u;
    }
    if (synth->delayFramesRight == 0u) synth->delayFramesRight = 1u;
}

static SynthMusicConfig DefaultMusicConfig(uint32_t runSeed) {
    SynthMusicConfig config = {
        GetSynthSeedBpm(runSeed),
        38,
        (SynthHash(runSeed ^ 0xa53c9e1du) & 1u) != 0u ?
            SYNTH_MODE_DORIAN : SYNTH_MODE_AEOLIAN
    };
    return config;
}

static SynthMusicConfig NormalizeMusicConfig(SynthMusicConfig config) {
    config.bpm = fmaxf(SYNTH_MIN_BPM, fminf(config.bpm, SYNTH_MAX_BPM));
    if (config.rootMidi < 24 || config.rootMidi > 60) config.rootMidi = 38;
    if (config.mode != SYNTH_MODE_DORIAN) config.mode = SYNTH_MODE_AEOLIAN;
    return config;
}

static void InitializeSynthState(SynthSystem *synth, uint32_t runSeed,
                                 SynthMusicConfig config) {
    *synth = (SynthSystem){ 0 };
    synth->musicConfig = NormalizeMusicConfig(config);
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
    atomic_init(&synth->controlDrops, 0u);
    atomic_init(&synth->sfxCommandDrops, 0u);
    atomic_init(&synth->initialized, false);
    ConfigureSynthSeed(synth, runSeed);
    synth->currentBpm = synth->ambientBpm;
    synth->targetBpm = synth->ambientBpm;
    synth->targetIntensity = 0.24f;
    synth->intensity = 0.24f;
    synth->targetDrumGate = 0.0f;
    synth->drumGate = 0.0f;
    synth->targetPreBossHush = 0.0f;
    synth->preBossHush = 0.0f;
    synth->songBuildProgress = 0.0f;
    synth->currentStep = 15;
    synth->stepTimer = (60.0f / synth->ambientBpm) * 0.25f;
    synth->kickTime = 1.0f;
    synth->hatTime = 1.0f;
    synth->openHatTime = 1.0f;
    synth->clapTime = 1.0f;
    synth->hatVelocity = 0.7f;
    synth->clapVelocity = 1.0f;
    synth->arpFrequency = 220.0f;
    synth->sidechainGain = 1.0f;
    synth->musicState = SYNTH_STATE_AMBIENT;
    synth->transitionFxTime = 2.4f;

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
        synth->reverbCombs[i].feedback = 0.89f;
        synth->reverbCombs[i].damp = 0.20f;
    }
    for (int i = 0; i < REVERB_ALLPASS_COUNT; i++) {
        synth->reverbAllpasses[i].size = allpassSizes[i];
        synth->reverbAllpasses[i].feedback = 0.5f;
    }
}

void InitAudioSynth(SynthSystem *synth, uint32_t runSeed) {
    InitAudioSynthConfigured(synth, runSeed, DefaultMusicConfig(runSeed));
}

void InitAudioSynthConfigured(SynthSystem *synth, uint32_t runSeed,
                              SynthMusicConfig config) {
    InitializeSynthState(synth, runSeed, config);
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
    if (nextWrite == controlRead) {
        atomic_fetch_add_explicit(&synth->controlDrops, 1u, memory_order_relaxed);
        return;
    }

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
            voice->duration = 0.32f; voice->freqStart = 380.0f;
            voice->freqEnd = 105.0f; voice->volume = 0.74f;
        } else if (type == SFX_GLITCH_HIT) {
            voice->duration = 0.22f; voice->freqStart = 400.0f;
            voice->freqEnd = 50.0f; voice->volume = 0.70f;
        } else if (type == SFX_EXPLOSION) {
            voice->duration = 0.44f; voice->freqStart = 130.0f;
            voice->freqEnd = 38.0f; voice->volume = 0.66f;
            voice->noiseFilter.stage[0] = 0.0f; voice->noiseFilter.stage[1] = 0.0f;
            voice->noiseFilter.stage[2] = 0.0f; voice->noiseFilter.stage[3] = 0.0f;
        } else if (type == SFX_POWER_UP) {
            voice->duration = 0.62f; voice->freqStart = 150.0f;
            voice->freqEnd = 300.0f; voice->volume = 0.30f;
        } else if (type == SFX_BOSS_RISER) {
            voice->duration = 2.55f; voice->freqStart = 48.0f;
            voice->freqEnd = 720.0f; voice->volume = 0.46f;
        } else if (type == SFX_CORE_COLLAPSE) {
            voice->duration = 3.25f; voice->freqStart = 92.0f;
            voice->freqEnd = 23.0f; voice->volume = 1.0f;
            voice->noiseFilter = (LadderFilter){ 0 };
        } else {
            return;
        }
        voice->active = true;
    }
}

void TriggerSynthSFX(SynthSystem *synth, SFXType type) {
    if (!atomic_load_explicit(&synth->initialized, memory_order_acquire) ||
        type <= SFX_NONE || type > SFX_CORE_COLLAPSE) return;

    unsigned int commandWrite = atomic_load_explicit(&synth->sfxCommandWrite,
                                                     memory_order_relaxed);
    unsigned int nextWrite = (commandWrite + 1u) % SFX_COMMAND_CAPACITY;
    unsigned int commandRead = atomic_load_explicit(&synth->sfxCommandRead,
                                                    memory_order_acquire);
    if (nextWrite == commandRead) {
        atomic_fetch_add_explicit(&synth->sfxCommandDrops, 1u, memory_order_relaxed);
        return;
    }

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
        telemetry.controlDrops = atomic_load_explicit(
            &synth->controlDrops, memory_order_relaxed);
        telemetry.sfxCommandDrops = atomic_load_explicit(
            &synth->sfxCommandDrops, memory_order_relaxed);
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

    InitializeSynthState(&synth, runSeed, DefaultMusicConfig(runSeed));
    atomic_store_explicit(&synth.initialized, true, memory_order_release);
    UpdateAudioSynth(&synth, 0.72f, 0.0f, 0.0f, 980.0f, 0.0f);
    TriggerSynthSFX(&synth, SFX_LASER_TAP);
    TriggerSynthSFX(&synth, SFX_EXPLOSION);

    for (int block = 0; block < 12; block++) {
        if (block == 2) {
            TriggerSynthSFX(&synth, SFX_LASER_CHARGE);
        } else if (block == 4) {
            UpdateAudioSynth(&synth, 0.94f, 0.35f, 0.84f, 1550.0f, 0.0f);
            TriggerSynthSFX(&synth, SFX_BOSS_RISER);
        } else if (block == 6) {
            TriggerSynthSFX(&synth, SFX_CORE_COLLAPSE);
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
