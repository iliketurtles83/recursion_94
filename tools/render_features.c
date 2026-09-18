#include "../src/gameplay.c"
#define main production_main
#include "../main.c"
#undef main
#include <stdio.h>

static void Capture(GameplaySystem *game,EnvironmentSystem *env,PostProcessSystem *post,
                    DemosceneSystem *demo,Camera3D camera,const char *path,int overlay) {
    int w=GetScreenWidth(),h=GetScreenHeight();
    char capturePath[180]; snprintf(capturePath,sizeof capturePath,"%s",path);
    for(int frame=0;frame<3;frame++) {
        BeginDrawing();
        BeginTextureMode(post->target);
        ClearBackground((Color){2,3,10,255});
        DrawRaymarchBackdrop(post,game->runTime,.5f,game->boss.encounterIndex,w,h);
        DrawDemosceneBackdrop(demo,game->runTime,.5f,w,h);
        BeginMode3D(camera);
        DrawEnvironment(env,camera,1700);
        BeginCraftPass(post,game->runTime,.5f,camera.position);
        DrawGameplay3D(game,post->craftShader,post->craftObjectClassLoc,post->craftEnemyTypeLoc,post->craftOriginLoc);
        EndShaderMode(); EndMode3D(); EndTextureMode();
        ClearBackground(BLACK);
        DrawPostProcess(post,game->runTime,.5f,0,0,w,h,camera.fovy);
        if(!overlay) {
            float scale=fminf(1,w/1280.0f);
            BeginMode2D((Camera2D){.zoom=scale});
            DrawGameplayHUD(game,camera,(int)(w/scale),(int)(h/scale));
            EndMode2D();
        }
        if(overlay==1) DrawUpgradeChoices(game,1,w,h);
        if(overlay==2) DrawPlayerSettings(w,h);
        EndDrawing();
    }
    TakeScreenshot(capturePath);
}
int main(void) {
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_HIDDEN|FLAG_MSAA_4X_HINT|FLAG_WINDOW_RESIZABLE);
    InitWindow(1280,720,"feature render validation");
    if(!IsWindowReady()) return 1;
    Camera3D camera={.position={0,5.5f,8},.target={0,.2f,-20},.up={0,1,0},.fovy=72,.projection=CAMERA_PERSPECTIVE};
    static EnvironmentSystem env; static PostProcessSystem post; static DemosceneSystem demo;
    GameplaySystem game; GameplayEvents events={0};
    InitEnvironment(&env,94); InitDemoscene(&demo,94); InitPostProcess(&post,1280,720,94,&demo);
    UpdateEnvironment(&env,1700,25,.5f,0);
    playerPreferences.tutorialDone=true;
    InitGameplay(&game,94); game.runTime=25; game.activeTime=100; game.boss.nextSpawnDistance=1700;
    SpawnEnemyAt(&game,ENEMY_DRIFTER,(Vector3){-4,3,-18},0,1);
    SpawnEnemyAt(&game,ENEMY_CARRIER,(Vector3){4,4,-24},0,2);
    SpawnEnemyAt(&game,ENEMY_SUPPORT,(Vector3){0,5,-30},0,3);
    SpawnAimedProjectile(&game,(Vector3){-3,3,-8},game.player.position,25,0);
    Capture(&game,&env,&post,&demo,camera,"bin/qa-arrival-before.png",0);
    for(int frame=0;frame<36;frame++) UpdateGameplay(&game,1.0f/60,1700,false);
    Capture(&game,&env,&post,&demo,camera,"bin/qa-arrival-withdrawing.png",0);
    for(int frame=0;frame<64;frame++) UpdateGameplay(&game,1.0f/60,1700,false);
    Capture(&game,&env,&post,&demo,camera,"bin/qa-arrival-distant.png",0);
    for(int frame=0;frame<40;frame++) UpdateGameplay(&game,1.0f/60,1700,false);
    Capture(&game,&env,&post,&demo,camera,"bin/qa-arrival-boss.png",0);
    for(int form=0;form<5;form++) {
        InitGameplay(&game,94); game.boss.encounterIndex=form; game.runTime=25; game.activeTime=100;
        SpawnBoss(&game,1700,&events);
        game.boss.phase=BOSS_SHIELDED; game.boss.position=(Vector3){0,4.1f,-46};
        game.boss.encounterTime=20; game.boss.phaseTime=3;
        game.boss.introFlash=0; game.boss.fireTimer=.5f;
        UpdateBoss(&game,.01f,1700,&events);
        game.boss.attackAim=game.player.position; game.boss.safeLane=0;
        Capture(&game,&env,&post,&demo,camera,TextFormat("bin/qa-boss-%d-warning.png",form+1),0);
        FireBossPattern(&game);
        for(int f=0;f<35;f++) UpdateProjectiles(&game,1.0f/60,&events);
        Capture(&game,&env,&post,&demo,camera,TextFormat("bin/qa-boss-%d-volley.png",form+1),0);
    }
    InitGameplay(&game,94); game.boss.encounterIndex=2; game.runTime=25; game.activeTime=100;
    SpawnBoss(&game,1700,&events); game.boss.phase=BOSS_EXPOSED; game.boss.position.z=-46;
    game.boss.encounterTime=20; game.boss.introFlash=0; game.boss.reinforcementTimer=1000;
    game.boss.fireTimer=100; game.boss.shieldNodes=0;
    for(int n=0;n<4;n++) game.enemies[game.boss.nodeSlots[n]].active=false;
    UpdateBoss(&game,.01f,1700,&events);
    Capture(&game,&env,&post,&demo,camera,"bin/qa-prism-before.png",0);
    ShufflePrism(&game);
    for(int f=0;f<38;f++) UpdateBoss(&game,1.0f/60,1700,&events);
    Capture(&game,&env,&post,&demo,camera,"bin/qa-prism-crossing.png",0);
    for(int f=0;f<39;f++) UpdateBoss(&game,1.0f/60,1700,&events);
    Capture(&game,&env,&post,&demo,camera,"bin/qa-prism-after.png",0);
    InitGameplay(&game,94); game.boss.encounterIndex=19; game.runTime=25; game.activeTime=1600;
    SpawnBoss(&game,1700,&events); game.boss.phase=BOSS_SINGULARITY; game.boss.position.z=-39.5f;
    game.boss.encounterTime=20; game.boss.introFlash=0; game.boss.fireTimer=.5f;
    UpdateBoss(&game,.01f,1700,&events); FireBossPattern(&game);
    for(int f=0;f<35;f++) UpdateProjectiles(&game,1.0f/60,&events);
    Capture(&game,&env,&post,&demo,camera,"bin/qa-depth4-firewall.png",0);
    InitGameplay(&game,94); game.runTime=25;
    Enemy *sniper=SpawnEnemyAt(&game,ENEMY_SNIPER,(Vector3){-7,3,-28},0,1);
    sniper->phase=ENEMY_TELEGRAPH; sniper->attackAim=game.player.position;
    SpawnEnemyAt(&game,ENEMY_CARRIER,(Vector3){0,4,-32},0,2);
    SpawnEnemyAt(&game,ENEMY_SUPPORT,(Vector3){7,4,-30},0,3);
    SpawnEnemyAt(&game,ENEMY_DRIFTER,(Vector3){5,2,-26},0,4);
    Capture(&game,&env,&post,&demo,camera,"bin/qa-enemy-roles.png",0);
    SetWindowSize(960,600);
    UnloadPostProcess(&post); InitPostProcess(&post,960,600,94,&demo);
    Capture(&game,&env,&post,&demo,camera,"bin/qa-small-flight.png",0);
    OfferGameplayUpgrade(&game); game.upgradeOffers[0]=UPG_FOCUS; game.upgradeOffers[1]=UPG_BOMB_GUARD; game.upgradeOffers[2]=UPG_WIDELOCK;
    Capture(&game,&env,&post,&demo,camera,"bin/qa-small-upgrades.png",1);
    game.upgradeOffers[0]=UPG_HARVEST; game.upgradeOffers[1]=UPG_PIERCE; game.upgradeOffers[2]=UPG_CAPACITOR;
    Capture(&game,&env,&post,&demo,camera,"bin/qa-small-resource-upgrades.png",1);
    OpenPlayerSettings(); Capture(&game,&env,&post,&demo,camera,"bin/qa-small-settings.png",2);
    UnloadPostProcess(&post); UnloadEnvironment(&env); CloseWindow();
    puts("RENDER: five bosses, committed volleys, enemy roles and 960x600 UI captured");
    return 0;
}
