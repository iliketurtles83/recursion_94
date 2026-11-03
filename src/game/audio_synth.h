#ifndef AUDIO_SYNTH_H
#define AUDIO_SYNTH_H

#include "raylib.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>

#define SAMPLE_RATE 44100
#define BUFFER_FRAMES 4096
#define MAX_SFX_VOICES 6
#define SYNTH_DELAY_FRAMES 16384
#define PAD_CHORD_NOTES 5
#define PAD_UNISON_VOICES 5
#define REVERB_COMB_COUNT 4
#define REVERB_ALLPASS_COUNT 2
#define SFX_COMMAND_CAPACITY 32
#define SYNTH_CONTROL_CAPACITY 16
#define SYNTH_MIN_BPM 120.0f
#define SYNTH_MAX_BPM 135.0f

typedef enum {
    SYNTH_MODE_AEOLIAN = 0,
    SYNTH_MODE_DORIAN
} SynthScaleMode;

typedef enum {
    SYNTH_STATE_AMBIENT = 0,
    SYNTH_STATE_CRUISING,
    SYNTH_STATE_HAZARD
} SynthMusicState;

typedef struct {
    float bpm;
    int rootMidi;
    SynthScaleMode mode;
} SynthMusicConfig;

typedef enum {
    SFX_NONE = 0,
    SFX_LASER_TAP,
    SFX_LASER_CHARGE,
    SFX_GLITCH_HIT,
    SFX_EXPLOSION,
    SFX_POWER_UP,
    SFX_BOSS_RISER,
    SFX_CORE_COLLAPSE
} SFXType;

// A level's song: the composed layer on top of the seeded synth. The seed still
// picks tempo, mode and arp timbre; the song fixes progression, bass line, lead
// motif and the section plan that cycles once the build-up completes.
#define SONG_MAX_SECTIONS 8
enum {
    SECTION_KICK = 1u << 0,
    SECTION_HATS = 1u << 1,
    SECTION_SNARE = 1u << 2,
    SECTION_OPEN_HATS = 1u << 3,
    SECTION_BASS_ALT = 1u << 4,   // second bass line
    SECTION_LEAD = 1u << 5,
    SECTION_BREAK = 1u << 6,      // no kick: pad and arp forward, delay up
    SECTION_ALL = 0x2fu           // everything but BASS_ALT and BREAK
};

typedef struct {
    unsigned char bars;
    unsigned char flags;
} SynthSection;

typedef struct {
    const char *name;
    signed char progression[8];      // chord root scale degree, one per bar
    signed char bass[2][16];         // scale degrees above the chord root (7 = octave), -1 rest
    unsigned short bassSlide[2];     // step bitmasks (bit n = step n)
    unsigned short bassAccent[2];
    signed char lead[32];            // two-bar motif, chord-relative scale degrees, -1 rest
    SynthSection sections[SONG_MAX_SECTIONS];
    int sectionCount;
    float introEnd, buildEnd;        // distances: ambient intro ends, full beat reached
    // Dry mix trims per layer (echo and reverb sends are unchanged). Set them all:
    // a missing field is 0, which silences the layer.
    float kickGain, bassGain, subGain, padGain, arpGain, leadGain, hatGain, snareGain;
} SynthSong;

extern const SynthSong SYNTH_SONG_CORRIDOR;

#ifdef SYNTH_METER
// Debug-only per-layer energy (sum of squares of each layer's contribution to
// the left output), for offline mix measurements.
enum { METER_KICK, METER_BASS, METER_SUB, METER_PAD, METER_ARP, METER_LEAD, METER_HAT,
       METER_OPEN_HAT, METER_SNARE, METER_ATMOS, METER_ECHO, METER_TOTAL, METER_COUNT };
extern double g_synthMeter[METER_COUNT];
#endif

typedef struct {
    float intensity;
    float glitchAmount;
    float bossIntensity;
    float virtualPlayerZ;
    float preBossHush;
} SynthControl;

typedef struct {
    float beatPulse;
    float drumGate;
    float currentBpm;
    unsigned int maxCallbackMicros;
    unsigned int callbackFrames;
    unsigned int controlDrops;
    unsigned int sfxCommandDrops;
} SynthTelemetry;

// 4-pole Moog-style ladder filter (tanh-saturated stages, 2x oversampled).
typedef struct {
    float stage[4];
    float cutoff;
    float resonance;
} LadderFilter;

enum {
    BASS_STEP_SLIDE = 1u << 0,
    BASS_STEP_ACCENT = 1u << 1
};

typedef struct {
    signed char note;
    unsigned char flags;
} BassStep;

typedef struct {
    SFXType type;
    float time;
    float duration;
    float freqStart;
    float freqEnd;
    float volume;
    bool active;
    // Only used by SFX_EXPLOSION to turn raw noise into a swept "boom" instead
    // of flat hiss; harmless to carry on other voice types.
    LadderFilter noiseFilter;
} SFXVoice;

// Freeverb-style damped feedback comb, sized for the longest tuned delay used.
typedef struct {
    float buffer[1356];
    int size;
    int index;
    float feedback;
    float damp;
    float dampState;
} ReverbComb;

typedef struct {
    float buffer[556];
    int size;
    int index;
    float feedback;
} ReverbAllpass;

typedef struct {
    AudioStream stream;
    uint32_t runSeed;
    uint32_t noiseState;
    SynthMusicConfig musicConfig;
    SynthMusicState musicState;
    const SynthSong *song;   // NULL: the original seeded arrangement
    int chordDegree;
    int songBar;             // bars since the build-up completed
    unsigned char sectionFlags;

    // Seed defines the baseline tempo; it stays fixed once the intro ramp
    // (ambientBpm -> baseBpm) completes. No runtime intensity/boss nudging.
    float baseBpm;
    float ambientBpm;
    float currentBpm;
    float targetBpm;
    float targetIntensity;
    float intensity;
    float targetBossIntensity;
    float bossIntensity;
    float glitchAmount;
    float targetPreBossHush;
    float preBossHush;
    float beatPulse;
    // Song-arrangement gate: 0 during the ambient intro, ramps to 1 through the
    // buildup, dips again for the pre-boss hush, and snaps back on the drop.
    float targetDrumGate;
    float drumGate;
    // Unsmoothed distance-based buildup progress (0..1, ignores the pre-boss
    // hush) used for discrete structural decisions instead of the audio-rate
    // smoothed drumGate, which asymptotically never hits exactly 1.0.
    float songBuildProgress;

    float stepTimer;
    float stepJitter;
    float hitVelocity;
    int currentStep;
    int barCount;
    unsigned char progressionVariant;
    unsigned char introMotifVariant;
    unsigned char introTimbreVariant;
    unsigned char introRegister;

    float resonanceLfoPhase;

    float kickTime;
    float kickPhase;
    bool kickTrigger;
    float sidechainGain;

    float bassPhase;
    float bassEnv;
    float rootFrequency;
    float bassFrequency;
    float bassTargetFrequency;
    BassStep bassPattern[16];
    float bassAccent;
    float bassFilterEnv;
    bool bassSlide;
    LadderFilter bassFilter;
    float subPhase;
    float subEnv;

    int rootMidi;
    int chordRoot;
    int chordThird;
    // Supersaw unison: PAD_UNISON_VOICES detuned saws per chord note.
    float padPhase[PAD_CHORD_NOTES * PAD_UNISON_VOICES];
    float padDrift[PAD_CHORD_NOTES * PAD_UNISON_VOICES];
    float padSinePhase[PAD_CHORD_NOTES];
    float padFrequency[PAD_CHORD_NOTES];
    float padTargetFrequency[PAD_CHORD_NOTES];
    float padLfoPhase;
    LadderFilter padFilterLeft;
    LadderFilter padFilterRight;

    float arpPhase;
    float arpFrequency;
    float arpEnv;
    float arpModPhase;
    float arpModRatio;
    LadderFilter arpFilter;

    float leadPhase;
    float leadFrequency;
    float leadEnv;
    LadderFilter leadFilter;

    float transitionFxTime;
    float transitionFxDirection;
    float glitchRepeat;
    LadderFilter transitionFilter;

    float hatTime;
    float openHatTime;
    float clapTime;
    float hatVelocity;
    float clapVelocity;
    float previousNoise;
    float hatMetalPhase[3];

    float atmospherePhase;
    LadderFilter atmosphereFilterLeft;
    LadderFilter atmosphereFilterRight;

    float delayLeft[SYNTH_DELAY_FRAMES];
    float delayRight[SYNTH_DELAY_FRAMES];
    unsigned int delayIndex;
    unsigned int delayFrames;
    unsigned int delayFramesRight;
    float delayDampLeft;
    float delayDampRight;

    ReverbComb reverbCombs[REVERB_COMB_COUNT];
    ReverbAllpass reverbAllpasses[REVERB_ALLPASS_COUNT];

    SFXVoice sfxPool[MAX_SFX_VOICES];
    SFXType sfxCommands[SFX_COMMAND_CAPACITY];
    atomic_uint sfxCommandRead;
    atomic_uint sfxCommandWrite;
    SynthControl controls[SYNTH_CONTROL_CAPACITY];
    atomic_uint controlRead;
    atomic_uint controlWrite;
    atomic_uint pendingSeed;
    atomic_bool reseedPending;
    atomic_uint telemetryBeatPulse;
    atomic_uint telemetryDrumGate;
    atomic_uint telemetryBpm;
    atomic_uint maxCallbackMicros;
    atomic_uint lastCallbackFrames;
    atomic_uint telemetrySequence;
    atomic_uint controlDrops;
    atomic_uint sfxCommandDrops;
    atomic_bool initialized;
} SynthSystem;

void InitAudioSynth(SynthSystem *synth, uint32_t runSeed);
void InitAudioSynthConfigured(SynthSystem *synth, uint32_t runSeed,
                              SynthMusicConfig config);
// Same as InitAudioSynth, with a level song (NULL = original arrangement).
void InitAudioSynthSong(SynthSystem *synth, uint32_t runSeed, const SynthSong *song);
// Offline rendering (no audio device): for tests and the song render tool.
void InitAudioSynthOffline(SynthSystem *synth, uint32_t runSeed, const SynthSong *song);
void RenderAudioSynthOffline(SynthSystem *synth, short *stereoOut, unsigned int frames);
void ReseedAudioSynth(SynthSystem *synth, uint32_t runSeed);
void UpdateAudioSynth(SynthSystem *synth, float intensity, float glitchAmount,
                      float bossIntensity, float virtualPlayerZ, float preBossHush);
void TriggerSynthSFX(SynthSystem *synth, SFXType type);
void UnloadAudioSynth(SynthSystem *synth);

float GetSynthSeedBpm(uint32_t runSeed);
const char *GetSynthSeedKeyName(uint32_t runSeed);
float GetSynthBeatPulse(const SynthSystem *synth);
float GetSynthDrumGate(const SynthSystem *synth);
float GetSynthCurrentBpm(const SynthSystem *synth);
unsigned int GetSynthMaxCallbackMicros(const SynthSystem *synth);
unsigned int GetSynthLastCallbackFrames(const SynthSystem *synth);
SynthTelemetry GetSynthTelemetry(const SynthSystem *synth);
bool ValidateAudioSynth(uint64_t *pcmHash);

#endif // AUDIO_SYNTH_H
