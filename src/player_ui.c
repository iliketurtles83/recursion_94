#include "player_ui.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

PlayerPreferences playerPreferences = {
    {KEY_A,KEY_D,KEY_W,KEY_S,KEY_J,KEY_K,KEY_L,KEY_SPACE,KEY_LEFT_SHIFT,KEY_P},
    1.0f,1.0f,0.65f,0,0,0,false
};
static const int defaultKeys[ACT_COUNT] = {
    KEY_A,KEY_D,KEY_W,KEY_S,KEY_J,KEY_K,KEY_L,KEY_SPACE,KEY_LEFT_SHIFT,KEY_P
};
static const int padButtons[ACT_COUNT] = {
    GAMEPAD_BUTTON_LEFT_FACE_LEFT,GAMEPAD_BUTTON_LEFT_FACE_RIGHT,
    GAMEPAD_BUTTON_LEFT_FACE_UP,GAMEPAD_BUTTON_LEFT_FACE_DOWN,
    GAMEPAD_BUTTON_RIGHT_FACE_DOWN,GAMEPAD_BUTTON_RIGHT_FACE_LEFT,
    GAMEPAD_BUTTON_RIGHT_FACE_UP,GAMEPAD_BUTTON_RIGHT_TRIGGER_1,
    GAMEPAD_BUTTON_LEFT_TRIGGER_1,GAMEPAD_BUTTON_MIDDLE_RIGHT
};
static const char *actionNames[ACT_COUNT] = {
    "Move left","Move right","Move up","Move down","Pulse / charge",
    "Beam","Bomb","Boost","Evade","Pause"
};
static int activePad=-1, stickX,stickY,navX,navY,settingsRow,captureAction=-1;
static bool fireBlocked, lastRecord;
static char profilePath[1024];
static int tutorialStep,tutorialShots,tutorialCharges,tutorialDodges;
static Vector3 tutorialOrigin;
static float tutorialStart,tutorialStepStart;
static bool BindableKey(int key);

static float ClampSetting(float value) { return isfinite(value)?fmaxf(0,fminf(1,value)):1; }

void InitPlayerPreferences(void) {
    const char *base=getenv("RECURSION_PROFILE_DIR");
    if(!base) base=getenv("LOCALAPPDATA");
    if(!base) base=getenv("XDG_CONFIG_HOME");
    if(!base) base=getenv("HOME");
    if(!base || snprintf(profilePath,sizeof profilePath,"%s/Recursion94",base)>=(int)sizeof profilePath) {
        profilePath[0]=0; return;
    }
    MakeDirectory(profilePath);
    size_t len=strlen(profilePath);
    if(len+22>=sizeof profilePath) { profilePath[0]=0; return; }
    strcat(profilePath,"/pilot-v1.txt");
    FILE *file=fopen(profilePath,"r");
    if(!file) return;
    PlayerPreferences loaded=playerPreferences;
    int version=0,tutorial=0;
    bool valid=fscanf(file,"R94 %d %f %f %f %f %d %d %d",&version,
        &loaded.musicVolume,&loaded.sfxVolume,&loaded.shake,&loaded.bestTime,
        &loaded.bestBosses,&loaded.bestCombo,&tutorial)==8 && version==1;
    for(int i=0;i<ACT_COUNT;i++) {
        int key=0;
        if(fscanf(file," %d",&key)!=1 || !BindableKey(key)) valid=false;
        for(int previous=0;previous<i;previous++) if(loaded.keys[previous]==key) valid=false;
        loaded.keys[i]=key;
    }
    fclose(file);
    if(!valid || !isfinite(loaded.bestTime) || loaded.bestTime<0 ||
       loaded.bestTime>31536000 || loaded.bestBosses<0 || loaded.bestCombo<0) return;
    loaded.musicVolume=ClampSetting(loaded.musicVolume);
    loaded.sfxVolume=ClampSetting(loaded.sfxVolume);
    loaded.shake=ClampSetting(loaded.shake);
    loaded.tutorialDone=tutorial==1;
    playerPreferences=loaded;
}

void SavePlayerPreferences(void) {
    if(!profilePath[0]) return;
    FILE *file=fopen(profilePath,"w");
    if(!file) { TraceLog(LOG_WARNING,"PILOT: preferences could not be saved"); return; }
    fprintf(file,"R94 1 %.3f %.3f %.3f %.3f %d %d %d\n",
        playerPreferences.musicVolume,playerPreferences.sfxVolume,playerPreferences.shake,
        playerPreferences.bestTime,playerPreferences.bestBosses,playerPreferences.bestCombo,
        playerPreferences.tutorialDone?1:0);
    for(int i=0;i<ACT_COUNT;i++) fprintf(file,"%d ",playerPreferences.keys[i]);
    fclose(file);
}

static bool PadDown(int button) { return activePad>=0 && IsGamepadButtonDown(activePad,button); }
static bool PadPressed(int button) { return activePad>=0 && IsGamepadButtonPressed(activePad,button); }

void PollPlayerControls(void) {
    activePad=-1;
    for(int i=0;i<4;i++) if(IsGamepadAvailable(i)) { activePad=i; break; }
    float x=activePad>=0?GetGamepadAxisMovement(activePad,GAMEPAD_AXIS_LEFT_X):0;
    float y=activePad>=0?GetGamepadAxisMovement(activePad,GAMEPAD_AXIS_LEFT_Y):0;
    int nx=x>0.55f?1:x<-.55f?-1:0, ny=y>.55f?1:y<-.55f?-1:0;
    navX=nx!=stickX?nx:0; navY=ny!=stickY?ny:0; stickX=nx; stickY=ny;
    if(!IsKeyDown(playerPreferences.keys[ACT_FIRE]) && !IsMouseButtonDown(MOUSE_BUTTON_LEFT) &&
       !PadDown(GAMEPAD_BUTTON_RIGHT_FACE_DOWN) && !PadDown(GAMEPAD_BUTTON_RIGHT_TRIGGER_2)) fireBlocked=false;
}

bool PlayerActionDown(PlayerAction action) {
    if(action==ACT_FIRE && fireBlocked) return false;
    return IsKeyDown(playerPreferences.keys[action]) || PadDown(padButtons[action]) ||
        (action==ACT_FIRE && (IsMouseButtonDown(MOUSE_BUTTON_LEFT)||PadDown(GAMEPAD_BUTTON_RIGHT_TRIGGER_2)));
}
bool PlayerActionPressed(PlayerAction action) {
    return IsKeyPressed(playerPreferences.keys[action]) || PadPressed(padButtons[action]);
}
Vector2 PlayerMoveInput(void) {
    Vector2 v={(float)PlayerActionDown(ACT_RIGHT)-(float)PlayerActionDown(ACT_LEFT),
               (float)PlayerActionDown(ACT_UP)-(float)PlayerActionDown(ACT_DOWN)};
    if(IsKeyDown(KEY_LEFT)) v.x=-1;
    if(IsKeyDown(KEY_RIGHT)) v.x=1;
    if(IsKeyDown(KEY_UP)) v.y=1;
    if(IsKeyDown(KEY_DOWN)) v.y=-1;
    if(activePad>=0) {
        float x=GetGamepadAxisMovement(activePad,GAMEPAD_AXIS_LEFT_X);
        float y=-GetGamepadAxisMovement(activePad,GAMEPAD_AXIS_LEFT_Y);
        if(fabsf(x)>.18f) v.x=copysignf((fabsf(x)-.18f)/.82f,x);
        if(fabsf(y)>.18f) v.y=copysignf((fabsf(y)-.18f)/.82f,y);
    }
    float length=sqrtf(v.x*v.x+v.y*v.y);
    if(length>1) { v.x/=length; v.y/=length; }
    return v;
}
bool MenuAccept(void) { return IsKeyPressed(KEY_ENTER)||IsKeyPressed(KEY_KP_ENTER)||PadPressed(GAMEPAD_BUTTON_RIGHT_FACE_DOWN); }
bool MenuBack(void) { return IsKeyPressed(KEY_ESCAPE)||PadPressed(GAMEPAD_BUTTON_RIGHT_FACE_RIGHT); }
int MenuHorizontal(void) {
    int direction=navX+(IsKeyPressed(KEY_RIGHT)||PadPressed(GAMEPAD_BUTTON_LEFT_FACE_RIGHT))-(IsKeyPressed(KEY_LEFT)||PadPressed(GAMEPAD_BUTTON_LEFT_FACE_LEFT));
    return direction>0?1:direction<0?-1:0;
}
int MenuVertical(void) {
    int direction=navY+(IsKeyPressed(KEY_DOWN)||PadPressed(GAMEPAD_BUTTON_LEFT_FACE_DOWN))-(IsKeyPressed(KEY_UP)||PadPressed(GAMEPAD_BUTTON_LEFT_FACE_UP));
    return direction>0?1:direction<0?-1:0;
}

const char *PlayerActionLabel(PlayerAction action) {
    if(activePad>=0) {
        static const char *labels[]={"LEFT","RIGHT","UP","DOWN","A / RT","X","Y","RB","LB","START"};
        return labels[action];
    }
    int key=playerPreferences.keys[action];
    if(key>=32 && key<=96 && key!=KEY_SPACE) return TextFormat("%c",key);
    switch(key) {
        case KEY_SPACE:return "SPACE"; case KEY_LEFT_SHIFT:return "LSHIFT";
        case KEY_RIGHT_SHIFT:return "RSHIFT"; case KEY_LEFT_CONTROL:return "LCTRL";
        case KEY_RIGHT_CONTROL:return "RCTRL"; case KEY_TAB:return "TAB";
        default:return TextFormat("KEY %d",key);
    }
}
void SuppressPlayerFire(GameplaySystem *game) {
    fireBlocked=true; game->player.fireHeldLast=false; game->player.chargeTime=0;
    game->player.fireHoldTime=0;
}

static void CenterText(const char *text,int y,int size,Color color,int width) {
    DrawText(text,(width-MeasureText(text,size))/2,y,size,color);
}
static void WrappedText(const char *text,int x,int y,int width,int size,Color color) {
    char line[160]={0}; int used=0;
    const char *word=text;
    while(*word) {
        const char *end=strchr(word,' '); int n=end?(int)(end-word):(int)strlen(word);
        if(used+n+2>=(int)sizeof line) break;
        char trial[160]; memcpy(trial,line,(size_t)used); memcpy(trial+used,word,(size_t)n); trial[used+n]=0;
        if(used && MeasureText(trial,size)>width) { DrawText(line,x,y,size,color); y+=size+8; used=0; }
        memcpy(line+used,word,(size_t)n); used+=n; line[used++]=' '; line[used]=0;
        word=end?end+1:word+n;
    }
    if(used) DrawText(line,x,y,size,color);
}

void OpenPlayerSettings(void) { settingsRow=0; captureAction=-1; }
static Rectangle SettingsRowRect(int row,int w,int h) { return (Rectangle){w*.5f-310,h*.5f-176+row*27,620,25}; }
static bool BindableKey(int key) {
    return key>=32 && key<=348 && key!=KEY_ESCAPE && key!=KEY_ENTER && key!=KEY_KP_ENTER &&
        key!=KEY_F && key!=KEY_R && key!=KEY_F1 && key!=KEY_F2 && key!=KEY_F3 &&
        key!=KEY_ONE && key!=KEY_TWO && key!=KEY_THREE && key!=KEY_LEFT && key!=KEY_RIGHT &&
        key!=KEY_UP && key!=KEY_DOWN && key!=KEY_Y && key!=KEY_N;
}
bool UpdatePlayerSettings(void) {
    if(captureAction>=0) {
        int key=GetKeyPressed();
        if(MenuBack()) captureAction=-1;
        else if(BindableKey(key)) {
            for(int i=0;i<ACT_COUNT;i++) if(i!=captureAction && playerPreferences.keys[i]==key)
                playerPreferences.keys[i]=playerPreferences.keys[captureAction];
            playerPreferences.keys[captureAction]=key; captureAction=-1;
        }
        return false;
    }
    if(MenuBack() || IsKeyPressed(KEY_F2)) { SavePlayerPreferences(); return true; }
    settingsRow=(settingsRow+MenuVertical()+15)%15;
    bool clicked=IsMouseButtonPressed(MOUSE_BUTTON_LEFT),activate=MenuAccept();
    Vector2 mouse=GetMousePosition();
    if(clicked) for(int row=0;row<15;row++) if(CheckCollisionPointRec(mouse,SettingsRowRect(row,GetScreenWidth(),GetScreenHeight()))) {
        settingsRow=row; activate=true;
    }
    if(settingsRow<3) {
        float *value=settingsRow==0?&playerPreferences.musicVolume:settingsRow==1?&playerPreferences.sfxVolume:&playerPreferences.shake;
        *value=ClampSetting(*value+MenuHorizontal()*.1f);
        Rectangle r=SettingsRowRect(settingsRow,GetScreenWidth(),GetScreenHeight());
        if(clicked && CheckCollisionPointRec(mouse,r)) *value=ClampSetting((mouse.x-(r.x+380))/210);
    } else if(activate && settingsRow<13) {
        captureAction=settingsRow-3;
        while(GetKeyPressed()!=0) { }
    } else if(activate && settingsRow==13) {
        memcpy(playerPreferences.keys,defaultKeys,sizeof defaultKeys);
        playerPreferences.musicVolume=playerPreferences.sfxVolume=1; playerPreferences.shake=.65f;
    } else if(activate && settingsRow==14) { SavePlayerPreferences(); return true; }
    return false;
}
void DrawPlayerSettings(int w,int h) {
    DrawRectangle(0,0,w,h,(Color){2,5,11,220});
    DrawRectangle(w/2-340,h/2-255,680,530,(Color){12,22,30,255});
    CenterText("PILOT SETTINGS",h/2-230,27,RAYWHITE,w);
    CenterText("Arrows / stick to adjust. Enter / A to select. Esc / B to return.",h/2-195,14,LIGHTGRAY,w);
    for(int row=0;row<15;row++) {
        Rectangle r=SettingsRowRect(row,w,h);
        if(row==settingsRow) DrawRectangleRec(r,(Color){40,61,70,255});
        const char *label=row<3?(row==0?"Music volume":row==1?"Effects volume":"Camera shake"):
            row<13?actionNames[row-3]:row==13?"Restore default controls and audio":"Save and return";
        DrawText(label,(int)r.x+10,(int)r.y+4,16,RAYWHITE);
        if(row<3) {
            float value=row==0?playerPreferences.musicVolume:row==1?playerPreferences.sfxVolume:playerPreferences.shake;
            DrawRectangle((int)r.x+380,(int)r.y+9,210,7,(Color){35,43,51,255});
            DrawRectangle((int)r.x+380,(int)r.y+9,(int)(210*value),7,(Color){127,191,174,255});
        } else if(row<13) {
            int key=playerPreferences.keys[row-3];
            const char *keyText=captureAction==row-3?"PRESS KEY...":key==KEY_SPACE?"SPACE":key==KEY_LEFT_SHIFT?"LEFT SHIFT":
                key>=33&&key<=96?TextFormat("%c",key):TextFormat("KEY %d",key);
            DrawText(keyText,(int)r.x+380,(int)r.y+4,16,(Color){180,208,195,255});
        }
    }
    CenterText("Pad: stick move | A/RT charge | X beam | Y bomb | RB boost | LB evade",h/2+244,13,LIGHTGRAY,w);
}

static Rectangle UpgradeCard(int i,int w,int h) {
    float card=fminf(320,(w-80)/3.0f-12);
    return (Rectangle){w*.5f-1.5f*card-12+i*(card+12),h*.5f-95,card,254};
}
int ClickedUpgrade(int w,int h) {
    if(!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return -1;
    for(int i=0;i<3;i++) if(CheckCollisionPointRec(GetMousePosition(),UpgradeCard(i,w,h))) return i;
    return -1;
}
void DrawUpgradeChoices(const GameplaySystem *game,int selected,int w,int h) {
    DrawRectangle(0,0,w,h,(Color){2,5,10,235});
    CenterText("SALVAGE RECOVERED",h/2-193,34,RAYWHITE,w);
    CenterText("Choose one upgrade for this run. Flight and survival time are paused.",h/2-147,16,LIGHTGRAY,w);
    for(int i=0;i<3;i++) {
        int up=game->upgradeOffers[i]; Rectangle r=UpgradeCard(i,w,h);
        bool focus=i==selected || CheckCollisionPointRec(GetMousePosition(),r);
        DrawRectangleRec(r,focus?(Color){29,47,54,255}:(Color){15,26,34,255});
        DrawRectangleLinesEx(r,focus?2:1,focus?(Color){160,211,185,255}:(Color){60,79,87,255});
        DrawText(up>=0?TextFormat("%d   LEVEL %d / %d",i+1,game->upgrades[up]+1,UPGRADE_MAX_LEVEL):
            TextFormat("%d   FIELD SALVAGE",i+1),(int)r.x+20,(int)r.y+21,16,(Color){167,197,185,255});
        WrappedText(GameplayUpgradeName(up),(int)r.x+20,(int)r.y+62,(int)r.width-40,23,RAYWHITE);
        WrappedText(GameplayUpgradeDescription(up),(int)r.x+20,(int)r.y+112,(int)r.width-40,15,LIGHTGRAY);
        WrappedText(GameplayUpgradePreview(game,up),(int)r.x+20,(int)r.y+206,(int)r.width-40,13,(Color){167,211,184,255});
    }
    CenterText("1 / 2 / 3, click a card, or arrows / stick + Enter / A",h/2+171,16,RAYWHITE,w);
    CenterText("Upgrades stack up to three times. New run = new build.",h/2+203,14,GRAY,w);
}

void RecordPersonalBest(const GameplaySystem *game) {
    lastRecord=false;
    if(game->practice) return;
    if(game->activeTime>playerPreferences.bestTime) { playerPreferences.bestTime=game->activeTime; lastRecord=true; }
    if(game->boss.encounterIndex>playerPreferences.bestBosses) playerPreferences.bestBosses=game->boss.encounterIndex;
    if(game->bestCombo>playerPreferences.bestCombo) playerPreferences.bestCombo=game->bestCombo;
    SavePlayerPreferences();
}
void DrawRunReport(const GameplaySystem *game,int w,int h) {
    static const char *causes[]={"Enemy projectile","Guided missile","Sniper shot","Boss attack","Enemy collision"};
    DrawRectangle(0,0,w,h,(Color){2,5,11,225});
    DrawRectangle(w/2-330,h/2-205,660,420,(Color){12,21,30,250});
    CenterText("CORE DESYNCHRONIZED",h/2-176,31,(Color){236,155,141,255},w);
    CenterText(game->practice?"PRACTICE RUN - personal bests disabled":lastRecord?"NEW SURVIVAL PERSONAL BEST":"RUN COMPLETE",h/2-130,17,(Color){167,218,192,255},w);
    CenterText(TextFormat("SURVIVED  %02d:%02d     BOSSES  %d",(int)game->activeTime/60,(int)game->activeTime%60,game->boss.encounterIndex),h/2-84,25,RAYWHITE,w);
    CenterText(TextFormat("Best chain  %d     Near misses  %d     Hits taken  %d",game->bestCombo,game->nearMisses,game->hitsTaken),h/2-37,17,LIGHTGRAY,w);
    CenterText(TextFormat("Final hit: %s",causes[game->lastHitSource>=0&&game->lastHitSource<5?game->lastHitSource:0]),h/2+3,17,(Color){214,170,152,255},w);
    CenterText(TextFormat("YOUR BEST  %02d:%02d  /  %d bosses",(int)playerPreferences.bestTime/60,(int)playerPreferences.bestTime%60,playerPreferences.bestBosses),h/2+43,17,LIGHTGRAY,w);
    CenterText("R / Enter / A  -  FLY AGAIN",h/2+99,24,RAYWHITE,w);
    CenterText("F2 settings    F1 flight tips    Esc / B quit",h/2+148,15,GRAY,w);
}
static bool AdvanceFlightTutorial(const GameplaySystem *game) {
    if(playerPreferences.tutorialDone || game->gameOver || game->upgradePending) return false;
    if(game->activeTime-tutorialStart>=20) { playerPreferences.tutorialDone=true; return false; }
    int previous=tutorialStep;
    float moved=fabsf(game->player.position.x-tutorialOrigin.x)+fabsf(game->player.position.y-tutorialOrigin.y);
    if(tutorialStep==0 && moved>1) tutorialStep=1;
    if(tutorialStep==1 && game->shotsFired>tutorialShots) tutorialStep=2;
    if(tutorialStep==2 && game->chargeBursts>tutorialCharges) tutorialStep=3;
    if(tutorialStep==3 && game->dodges>tutorialDodges) tutorialStep=4;
    if(previous==tutorialStep && game->activeTime-tutorialStepStart>=5) tutorialStep++;
    if(previous!=tutorialStep) tutorialStepStart=game->activeTime;
    if(tutorialStep>=4) { playerPreferences.tutorialDone=true; return false; }
    return !game->boss.active && !game->boss.arrivalPending && game->loopTransitionTimer<=0;
}
void DrawFlightTutorial(const GameplaySystem *game,int w,int h) {
    if(!AdvanceFlightTutorial(game)) return;
    const char *message;
    if(tutorialStep==0)
        message="Move: arrows / movement keys / left stick";
    else if(tutorialStep==1)
        message=TextFormat("Tap %s: pulse",PlayerActionLabel(ACT_FIRE));
    else if(tutorialStep==2)
        message=TextFormat("Hold %s, aim, release: lock-on shot",PlayerActionLabel(ACT_FIRE));
    else if(tutorialStep==3)
        message=TextFormat("Move + %s: evade",PlayerActionLabel(ACT_DODGE));
    else { playerPreferences.tutorialDone=true; return; }
    int width=MeasureText(message,16)+36;
    float remaining=fminf(5-(game->activeTime-tutorialStepStart),20-(game->activeTime-tutorialStart));
    unsigned char alpha=(unsigned char)(fmaxf(0,fminf(1,remaining))*225);
    DrawRectangle(w/2-width/2,h-144,width,36,(Color){10,24,31,alpha});
    CenterText(message,h-134,16,(Color){195,225,207,alpha},w);
}
void ResetFlightTutorial(const GameplaySystem *game) {
    tutorialStep=0; tutorialOrigin=game->player.position;
    tutorialStart=tutorialStepStart=game->activeTime;
    tutorialShots=game->shotsFired; tutorialCharges=game->chargeBursts; tutorialDodges=game->dodges;
}
