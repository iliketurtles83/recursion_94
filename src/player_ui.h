#ifndef PLAYER_UI_H
#define PLAYER_UI_H
#include "gameplay.h"

typedef enum { ACT_LEFT, ACT_RIGHT, ACT_UP, ACT_DOWN, ACT_FIRE, ACT_BEAM,
    ACT_BOMB, ACT_BOOST, ACT_DODGE, ACT_PAUSE, ACT_COUNT } PlayerAction;
typedef struct {
    int keys[ACT_COUNT];
    float musicVolume, sfxVolume, shake;
    float bestTime;
    int bestBosses, bestCombo;
    bool tutorialDone;
} PlayerPreferences;
extern PlayerPreferences playerPreferences;
void InitPlayerPreferences(void);
void SavePlayerPreferences(void);
void PollPlayerControls(void);
bool PlayerActionDown(PlayerAction action);
bool PlayerActionPressed(PlayerAction action);
Vector2 PlayerMoveInput(void);
bool MenuAccept(void);
bool MenuBack(void);
int MenuHorizontal(void);
int MenuVertical(void);
const char *PlayerActionLabel(PlayerAction action);
void SuppressPlayerFire(GameplaySystem *game);
void OpenPlayerSettings(void);
bool UpdatePlayerSettings(void);
void DrawPlayerSettings(int width, int height);
void DrawUpgradeChoices(const GameplaySystem *game, int selected, int width, int height);
int ClickedUpgrade(int width, int height);
void RecordPersonalBest(const GameplaySystem *game);
void DrawRunReport(const GameplaySystem *game, int width, int height);
void DrawFlightTutorial(const GameplaySystem *game, int width, int height);
void ResetFlightTutorial(const GameplaySystem *game);
#endif
