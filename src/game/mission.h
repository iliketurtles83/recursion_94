#ifndef DATARUN_MISSION_H
#define DATARUN_MISSION_H
#include <stdbool.h>
#include <stdint.h>

#define MISSION_EVENTS 96
#define MISSION_DURATION 90.0f
#define MISSION_FINISH 3000.0
#define MISSION_REQUIRED 8
typedef enum MissionPhase { MISSION_TITLE, MISSION_COUNTDOWN, MISSION_RUNNING, MISSION_WON, MISSION_LOST } MissionPhase;
typedef enum EventKind { EVENT_PACKET, EVENT_DRONE, EVENT_SWEEPER, EVENT_MINE, EVENT_BARRIER } EventKind;
typedef struct MissionEvent {
    double s;
    float x, y, radius;
    EventKind kind;
    bool resolved, collected, hit;
    float amplitude, frequency, phase;
} MissionEvent;
typedef struct MissionSample { double s; float x, y; } MissionSample;
typedef struct Mission {
    MissionPhase phase;
    MissionEvent events[MISSION_EVENTS];
    int eventCount, packets, misses, nearMisses, shield, score, combo;
    float elapsed, countdown, immunity;
    MissionSample previous;
    int justCollected, justHit, justNear;
    const char *failure;
} Mission;

void mission_reset(Mission *m, uint64_t seed);
void mission_start(Mission *m);
bool mission_can_boost(const Mission *m);
void mission_tick(Mission *m, float dt, MissionSample sample, bool boosting);
int mission_next_packet(const Mission *m);
// The same time-dependent center is used by rendering, warnings and collision.
void mission_event_position(const MissionEvent *e, float time, float *x, float *y);
bool mission_event_danger(const MissionEvent *e, float time, float x, float y, float clearance);
#endif
