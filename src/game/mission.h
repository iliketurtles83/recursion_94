#ifndef DATARUN_MISSION_H
#define DATARUN_MISSION_H
#include <stdbool.h>
#include <stdint.h>

#define MISSION_EVENTS 192
#define MISSION_DURATION 90.0f
#define MISSION_FINISH 3000.0
#define MISSION_REQUIRED 8
#define MISSION_COUNTDOWN_SECONDS 5.0f
#define MISSION_WARP_SECONDS 2.4f
#define MISSION_ENEMY_KINDS 10
typedef enum MissionPhase { MISSION_TITLE, MISSION_COUNTDOWN, MISSION_RUNNING, MISSION_WARP, MISSION_LOST } MissionPhase;
typedef enum EventKind {
    EVENT_PACKET, EVENT_DRONE, EVENT_SWEEPER, EVENT_MINE, EVENT_BARRIER,
    EVENT_ORBITER, EVENT_SHUTTER, EVENT_INTERCEPTOR, EVENT_ROTOR, EVENT_PULSAR, EVENT_SPLITTER
} EventKind;
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
    int eventCount, packets, misses, nearMisses, shield, combo;
    int number, completed, enemyKinds, required, packetCount;
    int64_t score;
    uint64_t seed;
    float elapsed, countdown, immunity, warpRemaining, duration, cruiseSpeed, boostSpeed;
    MissionSample previous;
    int justCollected, justHit, justNear;
    const char *failure;
} Mission;

void mission_reset(Mission *m, uint64_t seed);
void mission_reset_at(Mission *m, uint64_t seed, int number);
int mission_enemy_count(int number);
uint64_t mission_route_seed(const Mission *m);
float mission_theme_hue(int number);
const char *mission_enemy_name(EventKind kind);
bool mission_event_opening(const MissionEvent *e);
float mission_event_angle(const MissionEvent *e, float time);
float mission_event_radius(const MissionEvent *e, float time);
void mission_event_split(const MissionEvent *e, float time, int side, float *x, float *y);
void mission_start(Mission *m);
bool mission_can_boost(const Mission *m);
void mission_tick(Mission *m, float dt, MissionSample sample, bool boosting);
int mission_next_packet(const Mission *m);
// The same time-dependent center is used by rendering, warnings and collision.
void mission_event_position(const MissionEvent *e, float time, float *x, float *y);
bool mission_event_danger(const MissionEvent *e, float time, float x, float y, float clearance);
#endif
