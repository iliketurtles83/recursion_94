// Headless combat checks ported from the original game's VALIDATE_GAMEPLAY mode:
// chain-link costs and caps, beam/bomb, hit damage (normal and boost), boost
// missile guidance and evade window, fire-rate pressure, swept hits, rounds,
// Leviathan and recursion advance.
#include "gameplay.h"
#include "raylib.h"

int main(void)
{
    SetTraceLogLevel(LOG_INFO);
    return ValidateGameplaySpecials() ? 0 : 1;
}
