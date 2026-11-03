// Music clock: the game's view of where the song is, in 16th steps.
//
// With audio, the clock follows the synth's sample-accurate position
// (SynthTelemetry.musicStep, minus the audio still in the device buffer) and
// runs at the synth's tempo between callbacks, so it moves smoothly every frame
// and never steps backwards except on a restart. Without audio (--game-shot),
// it free-runs at the seed's BPM from the start of the run, which keeps shots
// byte-reproducible.
//
// Gameplay and visuals read it two ways: "did this frame cross a grid line"
// (music_clock_crossed, for timers that wait for the beat) and "how far past
// the last grid line are we" (music_clock_since, for flashes and pulses).
// An invalid clock (headless tests) crosses every grid line every frame, so
// gated timers then behave exactly as plain timers.
#ifndef MUSIC_CLOCK_H
#define MUSIC_CLOCK_H

#include <math.h>
#include <stdbool.h>

#define MUSIC_STEP 1     // 16th
#define MUSIC_EIGHTH 2
#define MUSIC_BEAT 4
#define MUSIC_BAR 16
#define MUSIC_PHRASE 64  // four bars

typedef struct {
    double step;           // current position, 16th steps since the song started
    double prevStep;       // position at the previous frame
    float stepsPerSecond;  // BPM / 15
    bool valid;
} MusicClock;

static inline void music_clock_reset(MusicClock *c, float bpm)
{
    *c = (MusicClock){ .stepsPerSecond = bpm / 15.0f, .valid = true };
}

// Free-running (no audio).
static inline void music_clock_advance(MusicClock *c, float dt)
{
    c->prevStep = c->step;
    c->step += (double)(dt * c->stepsPerSecond);
}

// Follow the synth. heardStep is the synth's position minus its buffered audio.
static inline void music_clock_follow(MusicClock *c, float dt, double heardStep, float bpm)
{
    if (bpm > 1.0f) c->stepsPerSecond = bpm / 15.0f;
    c->prevStep = c->step;
    double next = c->step + (double)(dt * c->stepsPerSecond);
    double error = heardStep - next;
    if (error > 4.0 || error < -16.0) {
        next = heardStep;                      // stall or restart: jump
        if (next < c->step) c->prevStep = next;
    } else {
        double k = fmin(1.0, (double)dt * 6.0);
        next += error * k;                     // ease out callback-sized steps
    }
    c->step = next < c->prevStep ? c->prevStep : next;
}

// True if this frame crossed a multiple of `division` steps (or the clock is off).
static inline bool music_clock_crossed(const MusicClock *c, int division)
{
    if (!c->valid) return true;
    return floor(c->step / division) != floor(c->prevStep / division);
}

// Seconds since the last multiple of `division` steps (large if the clock is off).
static inline float music_clock_since(const MusicClock *c, int division)
{
    if (!c->valid || c->step < 0.0 || c->stepsPerSecond <= 0.0f) return 1000.0f;
    double into = c->step - floor(c->step / division) * division;
    return (float)(into / c->stepsPerSecond);
}

#endif // MUSIC_CLOCK_H
