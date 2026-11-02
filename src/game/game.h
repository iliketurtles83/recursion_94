#ifndef DATARUN_GAME_H
#define DATARUN_GAME_H
#include "cybertunnel.h"
typedef struct GameOptions {
    bool hidden, autoplay, idleBot, audioCheck, startupCheck, menuCheck, shipCheck, shotSteer;
    float shotSteerX, shotSteerY;
    int shotShield;
    int missions, startMission;
    double shotSeconds, transitionShot, menuShot, introShot;
    const char *shotOut;
} GameOptions;
// Interactive runs are endless. Mission limits/start overrides are QA tools.
int game_run(CtTunnel *ct, const GameOptions *options);
#endif
