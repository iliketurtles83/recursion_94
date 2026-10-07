#ifndef DATA_RUN_TECHNO_H
#define DATA_RUN_TECHNO_H
#include <stdbool.h>
#include <stdint.h>
#define TECHNO_RATE 48000
#define TECHNO_CRUISE_BPM 140
#define TECHNO_BOOST_BPM 166
typedef struct Techno Techno;
// The render thread owns synthesis state. Controls/status use atomics; the
// callback never allocates, reads files, locks, or touches the game state.
Techno *techno_create(uint64_t seed);
void techno_destroy(Techno *m);
void techno_control(Techno *m, float speed, bool running, bool paused, bool muted);
void techno_render(Techno *m, float *stereo, unsigned int frames);
float techno_bpm(const Techno *m);
uint64_t techno_frames(const Techno *m);
bool techno_preview(const char *file, uint64_t seed);
#endif
