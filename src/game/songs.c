// Level songs: the composed layer over the seeded synth (see SynthSong in
// audio_synth.h). Each is a few dozen bytes of pattern data, tracker style; the
// synth's instruments, tempo, mode and timbre still come from the run seed.
#include "audio_synth.h"

#define R (-1)   // rest

// Level 1, server corridor: driving electro. Rolling octave bass under an
// eight-bar progression; the lead motif enters in the B sections; a kickless
// break with the half-time bass every 40 bars.
const SynthSong SYNTH_SONG_CORRIDOR = {
    .name = "CORRIDOR",
    .progression = { 0, 0, 5, 6, 3, 5, 4, 6 },
    .bass = {
        { 0, R, 7, 0,  R, 0, 7, R,  0, R, 7, 0,  4, R, 6, 7 },   // main: rolling octaves, climb at the turn
        { 0, R, R, R,  R, R, 0, R,  2, R, R, R,  4, R, R, R },   // break: half-time, gliding up the chord
    },
    .bassSlide = { (1u << 6) | (1u << 14) | (1u << 15), (1u << 8) | (1u << 12) },
    .bassAccent = { (1u << 0) | (1u << 8), (1u << 0) },
    .lead = {
        4, R, R, 2,  R, R, 4, R,  7, R, R, 6,  R, R, 4, R,
        2, R, R, 0,  R, R, 2, R,  R, R, 4, R,  R, R, R, R,
    },
    .sections = {
        { 16, SECTION_KICK | SECTION_HATS | SECTION_SNARE },                    // A: groove
        { 16, SECTION_ALL },                                                      // B: lead, open hats
        { 8,  SECTION_HATS | SECTION_BASS_ALT | SECTION_BREAK },                 // break
        { 16, SECTION_ALL },                                                      // B again
        { 8,  SECTION_KICK | SECTION_HATS | SECTION_BASS_ALT },                  // turnaround
    },
    .sectionCount = 5,
    .introEnd = 300.0f, .buildEnd = 750.0f,
    // Measured with song_render against the original balance, where the sub was
    // the loudest layer and hats/snare sat ~30 dB under the kick. Targets (RMS,
    // full groove): kick -19, bass -22, sub -24, pad -32, arp -31, lead -27,
    // hats -33, snare -29 dB.
    .kickGain = 1.1f, .bassGain = 1.2f, .subGain = 0.4f, .padGain = 3.0f,
    .arpGain = 2.5f, .leadGain = 1.4f, .hatGain = 4.5f, .snareGain = 10.0f,
};
