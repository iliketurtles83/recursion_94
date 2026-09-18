// Deterministic inputs through the production loop; no OS input injection.
#include "raylib.h"
#include "gameplay.h"
#include "player_ui.h"
#include <math.h>
#include <stdio.h>
static int frame,failures,pauseCount,resumeCount,firstUpgrade=-1;
static GameplaySystem *live;
static float frozen,deadTime;
static bool streamPaused;
static void Require(bool condition,const char *name) {
    if(!condition) { failures++; printf("FAIL frame %d: %s\n",frame,name); }
}
static bool TestKeyPressed(int key) {
    if(key==KEY_LEFT_SHIFT) return frame==20;
    if(key==KEY_P) return frame==32 || frame==60;
    if(key==KEY_F2) return frame==40 || frame==94;
    if(key==KEY_LEFT) return frame==42 || frame==46 || frame==50;
    if(key==KEY_DOWN) return frame==44 || frame==48 || frame==52;
    if(key==KEY_ENTER) return frame==54 || frame==74;
    if(key==KEY_ESCAPE) return frame==58 || frame==72 || frame==96 || frame==98 || frame==100;
    if(key==KEY_K) return frame==90 || frame==116;
    if(key==KEY_TWO) return frame==105;
    if(key==KEY_ONE) return frame==130;
    return false;
}
static bool TestKeyDown(int key) {
    if(key==KEY_D) return frame>=5 && frame<=20;
    if(key==KEY_J) return (frame>=9&&frame<=11) || (frame>=23&&frame<=29);
    return false;
}
static int TestGetKeyPressed(void) { return frame==56?KEY_Q:0; }
static bool TestMousePressed(int button) { (void)button; return false; }
static bool TestMouseDown(int button) { (void)button; return false; }
static bool TestPadAvailable(int pad) { return pad==0 && frame>=480; }
static bool TestPadPressed(int pad,int button) {
    (void)pad;
    if(button==GAMEPAD_BUTTON_RIGHT_FACE_DOWN) return frame==480 || frame==494 || frame==501;
    if(button==GAMEPAD_BUTTON_RIGHT_FACE_RIGHT) return frame==490 || frame==498;
    if(button==GAMEPAD_BUTTON_MIDDLE_RIGHT) return frame==486 || frame==488;
    if(button==GAMEPAD_BUTTON_LEFT_FACE_LEFT) return frame==500;
    return false;
}
static bool TestPadDown(int pad,int button) { return TestPadPressed(pad,button); }
static float TestPadAxis(int pad,int axis) { (void)pad; return frame==482&&axis==GAMEPAD_AXIS_LEFT_X?.8f:0; }
#define IsKeyPressed TestKeyPressed
#define IsKeyDown TestKeyDown
#define GetKeyPressed TestGetKeyPressed
#define IsMouseButtonPressed TestMousePressed
#define IsMouseButtonDown TestMouseDown
#define IsGamepadAvailable TestPadAvailable
#define IsGamepadButtonPressed TestPadPressed
#define IsGamepadButtonDown TestPadDown
#define GetGamepadAxisMovement TestPadAxis
#include "../src/player_ui.c"
#include "../src/gameplay.c"

static void TestInitGameplay(GameplaySystem *game,uint32_t seed) { InitGameplay(game,seed); live=game; }
static void PrepareBoss(int form) {
    GameplayEvents e={0};
    live->boss.encounterIndex=form; SpawnBoss(live,0,&e);
    for(int i=0;i<4;i++) live->enemies[live->boss.nodeSlots[i]].active=false;
    live->boss.shieldNodes=0; live->boss.phase=BOSS_EXPOSED;
    live->enemies[live->boss.coreSlot].health=1;
    live->player.position.x=0; live->player.position.y=4.1f; live->player.velocity=(Vector3){0};
    live->chainLinks=100;
}
static bool TestWindowShouldClose(void) {
    frame++;
    if(!live) return frame>600;
    if(frame==22) Require(live->dodges==1 && live->player.dodgeCooldown>0,"evade executes");
    if(frame==34) frozen=live->activeTime;
    if(frame==39 || frame==57) Require(live->activeTime==frozen,"pause/settings freeze survival");
    if(frame==59) {
        Require(playerPreferences.keys[ACT_LEFT]==KEY_Q,"keyboard remapping");
        Require(fabsf(playerPreferences.musicVolume-.9f)<.001f && fabsf(playerPreferences.sfxVolume-.9f)<.001f,"separate volumes");
        TakeScreenshot("bin/qa-pause.png");
    }
    if(frame==55) TakeScreenshot("bin/qa-settings.png");
    if(frame==65) Require(live->activeTime>frozen,"resume restores simulation");
    if(frame==75) Require(live->activeTime>frozen,"default quit selection resumes safely");
    if(frame==90) PrepareBoss(0);
    if(frame==92) { Require(live->upgradePending,"boss kill opens salvage"); frozen=live->activeTime; firstUpgrade=live->upgradeOffers[1]; }
    if(frame==92 || frame==97 || frame==101 || frame==120)
        Require(!streamPaused,"salvage keeps music playing, including return from settings/quit");
    if(frame==95 || frame==99) Require(streamPaused,"nested settings/quit still pause audio");
    if(frame==102) TakeScreenshot("bin/qa-upgrades.png");
    if(frame==104) Require(live->activeTime==frozen,"salvage pauses simulation");
    if(frame==107) Require(!live->upgradePending && live->upgrades[firstUpgrade]==1,"upgrade selected once");
    if(frame==115) PrepareBoss(4);
    if(frame==120) { Require(live->upgradePending && live->boss.encounterIndex==5,"Leviathan salvages before collapse"); frozen=live->activeTime; }
    if(frame==129) Require(live->activeTime==frozen,"Leviathan choice freezes time");
    if(frame==150) Require(live->activeTime==frozen,"collapse not counted as survival");
    if(frame==460) {
        Require(GetGameplayRecursionDepth(live)==2 && live->modifier==MUT_CROSSFIRE && live->upgrades[firstUpgrade]>=1,"endless transition preserves build");
        TakeScreenshot("bin/qa-recursion.png");
    }
    if(frame==470) { GameplayEvents e={0}; live->player.energy=18; live->player.invulnerability=0;
        live->overdriveActive=false; live->lastHitSource=2; HitPlayer(live,(Vector3){1,3,0},&e); deadTime=live->activeTime; }
    if(frame==476) { Require(live->gameOver && live->activeTime==deadTime,"death freezes survival"); TakeScreenshot("bin/qa-report.png"); }
    if(frame==481) Require(!live->gameOver && live->activeTime<.1f && live->upgrades[firstUpgrade]==0,"controller restart resets run");
    if(frame==483) Require(live->player.position.x>0,"analog movement");
    return frame>520;
}
static void TestPause(AudioStream stream) { pauseCount++; streamPaused=true; PauseAudioStream(stream); }
static void TestResume(AudioStream stream) { resumeCount++; streamPaused=false; ResumeAudioStream(stream); }
static void TestFlags(unsigned int flags) { SetConfigFlags(flags|FLAG_WINDOW_HIDDEN); }
static void TestFPS(int fps) { (void)fps; SetTargetFPS(0); }
static float TestDt(void) { return 1.0f/60.0f; }
#define InitGameplay TestInitGameplay
#define WindowShouldClose TestWindowShouldClose
#define PauseAudioStream TestPause
#define ResumeAudioStream TestResume
#define SetConfigFlags TestFlags
#define SetTargetFPS TestFPS
#define GetFrameTime TestDt
#define main production_main
#include "../main.c"
#undef main
int main(void) {
    SetTraceLogLevel(LOG_WARNING);
    GameplaySystem tutorial;
    InitGameplay(&tutorial,94); playerPreferences.tutorialDone=false; ResetFlightTutorial(&tutorial);
    Require(AdvanceFlightTutorial(&tutorial) && tutorialStep==0,"tutorial starts");
    tutorial.activeTime=5.1f; AdvanceFlightTutorial(&tutorial);
    Require(tutorialStep==1,"unperformed tutorial action times out");
    tutorial.boss.active=true;
    Require(!AdvanceFlightTutorial(&tutorial),"tutorial never covers a boss fight");
    tutorial.activeTime=20.1f;
    Require(!AdvanceFlightTutorial(&tutorial) && playerPreferences.tutorialDone,"entire tutorial expires after twenty active seconds");
    tutorial.boss.active=false; playerPreferences.tutorialDone=false; ResetFlightTutorial(&tutorial);
    Require(AdvanceFlightTutorial(&tutorial),"F1 can restart expired tips");
    for(int i=0;i<100;i++) AdvanceFlightTutorial(&tutorial);
    Require(tutorialStep==0,"paused clock cannot consume tutorial timeout");
    tutorial.activeTime+=2; tutorial.shotsFired++; tutorial.chargeBursts++; tutorial.dodges++;
    tutorial.player.position.x+=2;
    Require(!AdvanceFlightTutorial(&tutorial) && playerPreferences.tutorialDone,"actions may complete tutorial before timeout");
    char *args[]={"recursion94.exe","--seed","94"};
    int result=production_main(3,args);
    Require(frame==502,"controller can select and confirm quit");
    Require(pauseCount>=6 && resumeCount>=5,"audio follows menu state");
    printf("PRODUCTION SMOKE frames=%d failures=%d pauses=%d resumes=%d result=%d\n",frame,failures,pauseCount,resumeCount,result);
    return result||failures;
}
