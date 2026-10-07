#ifndef DATARUN_GAME_H
#define DATARUN_GAME_H
#include "cybertunnel.h"
// shotSeconds < 0: interactive; zero: title; positive: reproducible bot screenshot.
int game_run(CtTunnel *ct, bool hidden, double shotSeconds, bool autoplay, bool idleBot, bool audioCheck, bool startupCheck, const char *shotOut);
#endif
