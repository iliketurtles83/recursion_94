// Headless behavioral regressions, linked out of release builds by section GC.
bool ValidatePlayability(void) {
    GameplaySystem a,b;
    GameplayEvents events={0};
    int failures=0,checks=0;
#define CHECK(condition,label) do { checks++; if(!(condition)) { failures++; TraceLog(LOG_ERROR,"PLAYABILITY: %s",label); } } while(0)
    InitGameplay(&a,94); InitGameplay(&b,94);
    a.boss.encounterIndex=b.boss.encounterIndex=1;
    OfferGameplayUpgrade(&a); OfferGameplayUpgrade(&b);
    CHECK(a.upgradeOffers[0]==b.upgradeOffers[0] && a.upgradeOffers[1]==b.upgradeOffers[1] &&
          a.upgradeOffers[2]==b.upgradeOffers[2],"repeatable upgrade offers");
    CHECK(a.upgradeOffers[0]!=a.upgradeOffers[1] && a.upgradeOffers[1]!=a.upgradeOffers[2] &&
          a.upgradeOffers[0]!=a.upgradeOffers[2],"three distinct offers");
    UpdateGameplay(&a,.05f,0,false);
    CHECK(a.activeTime==0 && a.runTime==0,"upgrade selection freezes clocks");
    CHECK(!ChooseGameplayUpgrade(&a,3) && a.upgradePending,"invalid choice cannot resume");
    int selected=a.upgradeOffers[1];
    CHECK(ChooseGameplayUpgrade(&a,1) && a.upgrades[selected]==1 && !a.upgradePending,"choice applies exactly once");
    CHECK(!ChooseGameplayUpgrade(&a,1) && a.upgrades[selected]==1,"choice cannot be replayed");
    for(int up=0;up<UPGRADE_COUNT;up++) a.upgrades[up]=UPGRADE_MAX_LEVEL;
    OfferGameplayUpgrade(&a);
    CHECK(a.upgradeOffers[0]<0 && a.upgradeOffers[1]<0 && a.upgradeOffers[2]<0,"fully upgraded endless fallback");
    a.chainLinks=90; ChooseGameplayUpgrade(&a,0);
    CHECK(a.chainLinks==100,"fallback respects link cap");
    CHECK(GameplayBeamCost(&a)==35 && GameplayBombCost(&a)==70,"capacitor costs bounded");
    a.score=INT_MAX-20; AddGameplayScore(&a,100);
    CHECK(a.score==INT_MAX,"endless score saturates without integer overflow");
    a.boss.encounterIndex=5; AdvanceGameplayLoop(&a,123,4000);
    CHECK(a.upgrades[selected]==3 && a.modifier==MUT_CROSSFIRE,"black hole preserves upgrades and selects modifier");
    a.boss.encounterIndex=10; AdvanceGameplayLoop(&a,456,8000);
    CHECK(a.modifier==MUT_GRAVITY,"modifier replaces previous hazard");
    a.boss.encounterIndex=15; AdvanceGameplayLoop(&a,789,12000);
    CHECK(a.modifier==MUT_ESCORTS,"third modifier rotation");
    a.gameOver=true; a.activeTime=13; a.runTime=15;
    UpdateGameplay(&a,.05f,0,false);
    CHECK(a.activeTime==13 && a.runTime==15,"death screen freezes both clocks");
    a.gameOver=false; a.loopTransitionTimer=4;
    UpdateGameplay(&a,.05f,0,false);
    CHECK(a.activeTime==13,"collapse excluded from survival time");

    InitGameplay(&a,94); a.upgrades[UPG_GRAZE]=2;
    EnemyProjectile *p=&a.projectiles[0];
    *p=(EnemyProjectile){.position={1.2f,2.4f,-.5f},.velocity={0,0,5},.life=3,.active=true};
    UpdateProjectiles(&a,.05f,&events);
    UpdateProjectiles(&a,.05f,&events);
    CHECK(a.nearMisses==1 && a.chainLinks==5 && a.player.energy==100,"graze rewards once without damage");
    p->grazed=false; a.player.invulnerability=.2f;
    UpdateProjectiles(&a,.01f,&events);
    CHECK(a.nearMisses==1,"invulnerable rolling cannot farm graze");

    InitGameplay(&a,94); a.upgrades[UPG_PIERCE]=1;
    for(int i=0;i<2;i++) {
        Enemy *e=SpawnEnemyAt(&a,ENEMY_DRIFTER,(Vector3){0,2.4f,-5-i*3},0,(uint32_t)i);
        e->health=5;
    }
    SpawnPlayerProjectile(&a,(Vector3){0,2.4f,0},(Vector3){0,2.4f,-100},-1,100,1,false);
    for(int i=0;i<6;i++) UpdatePlayerProjectiles(&a,.05f,&events);
    CHECK(a.enemies[0].health==4 && a.enemies[1].health==4 && !a.playerProjectiles[0].active,
          "piercing hits two different targets only once");
    InitGameplay(&a,94); a.upgrades[UPG_FOCUS]=2;
    for(int i=0;i<4;i++) SpawnEnemyAt(&a,ENEMY_DRIFTER,(Vector3){(float)i,2.4f,-30},0,(uint32_t)i);
    a.player.chargeTime=1; UpdateLocks(&a); FireCharge(&a,&events);
    int charged=0; for(int i=0;i<MAX_PLAYER_PROJECTILES;i++) charged+=a.playerProjectiles[i].active;
    CHECK(charged==2 && a.playerProjectiles[0].damage==4,"focused charge trades locks for damage");
    InitGameplay(&a,94); a.upgrades[UPG_TWIN]=1; FireTap(&a,&events);
    CHECK(a.playerProjectiles[0].active && a.playerProjectiles[1].active &&
          fabsf(a.playerProjectiles[0].damage-.7f)<.0001f,"twin shot damage tradeoff");
    InitGameplay(&a,94); a.upgrades[UPG_BRANCH]=1; a.chainLinks=50;
    SpawnEnemyAt(&a,ENEMY_DRIFTER,(Vector3){0,2.4f,-30},0,1);
    SpawnEnemyAt(&a,ENEMY_DRIFTER,(Vector3){6,2.4f,-30},0,2);
    FireLinkBeam(&a,&events);
    CHECK(!a.enemies[0].active && !a.enemies[1].active && a.beamBranchCount==1 && a.chainLinks==0,
          "beam branches without refunding its own links");
    InitGameplay(&a,94); a.upgrades[UPG_BOMB_GUARD]=2; a.chainLinks=100;
    FireLinkBomb(&a,&events);
    CHECK(fabsf(a.player.invulnerability-.8f)<.001f,"bomb shelter protects after discharge");

    InitGameplay(&a,94);
    Enemy *support=SpawnEnemyAt(&a,ENEMY_SUPPORT,(Vector3){3,2.4f,-25},0,1);
    Enemy *protected=SpawnEnemyAt(&a,ENEMY_DRIFTER,(Vector3){0,2.4f,-25},0,2);
    protected->health=3;
    DamageEnemy(&a,protected,1,false,&events);
    CHECK(protected->health==2.5f,"support protects nearby regular ships");
    DamageEnemy(&a,support,10,false,&events); DamageEnemy(&a,protected,1,false,&events);
    CHECK(protected->health==1.5f,"destroying support removes protection");
    InitGameplay(&a,94);
    Enemy *sniper=SpawnEnemyAt(&a,ENEMY_SNIPER,(Vector3){0,2.4f,-40},0,1);
    sniper->phase=ENEMY_HOVER; sniper->fireTimer=0;
    UpdateSpecialEnemy(&a,sniper,.01f,false,&events);
    CHECK(events.attackWarning && sniper->phase==ENEMY_TELEGRAPH && !a.projectiles[0].active,"sniper warns before firing");
    a.player.position.x=10;
    for(int i=0;i<22;i++) UpdateSpecialEnemy(&a,sniper,.05f,false,&events);
    EnemyProjectile *sniperShot=&a.projectiles[0];
    float interceptX=sniperShot->position.x-sniperShot->position.z/sniperShot->velocity.z*sniperShot->velocity.x;
    CHECK(sniperShot->active && sniperShot->kind==1 && fabsf(interceptX)<.001f,
          "sniper retains committed target rather than tracking dodge");
    InitGameplay(&a,94);
    Enemy *carrier=SpawnEnemyAt(&a,ENEMY_CARRIER,(Vector3){0,2.4f,-35},0,1);
    carrier->phase=ENEMY_TELEGRAPH; carrier->phaseTime=1;
    UpdateSpecialEnemy(&a,carrier,.06f,false,&events);
    int adds=0; for(int i=0;i<MAX_ENEMIES;i++) adds+=a.enemies[i].active&&a.enemies[i].type==ENEMY_CHASER;
    CHECK(adds==2,"carrier deploys two attack craft");

    int counts[5]={0};
    for(int form=0;form<5;form++) {
        InitGameplay(&a,94); a.boss.encounterIndex=form; SpawnBoss(&a,0,&events);
        a.boss.safeLane=0; a.boss.attackAim=a.player.position;
        FireBossPattern(&a);
        bool opening=true;
        for(int i=0;i<MAX_ENEMY_PROJECTILES;i++) if(a.projectiles[i].active) {
            counts[form]++;
            EnemyProjectile *shot=&a.projectiles[i];
            if(form==0) {
                float t=-shot->position.z/shot->velocity.z;
                float x=shot->position.x+shot->velocity.x*t;
                if(fabsf(x)<3.39f) opening=false;
            }
            CHECK(shot->kind>=2 && !shot->homing,"boss patterns remain committed");
        }
        CHECK(counts[form]>0 && counts[form]<MAX_ENEMY_PROJECTILES,"each boss emits bounded pattern");
        if(form==0) CHECK(opening,"core pattern leaves advertised safe corridor");
        if(form==1) {
            for(int i=0;i<MAX_ENEMY_PROJECTILES;i++) a.projectiles[i].active=false;
            a.enemies[a.boss.nodeSlots[0]].active=false;
            FireBossPattern(&a);
            int after=0; for(int i=0;i<MAX_ENEMY_PROJECTILES;i++) after+=a.projectiles[i].active;
            CHECK(after<counts[form],"destroying Bastion cannon removes its attack");
        }
    }
    CHECK(counts[0]!=counts[1] && counts[1]!=counts[2] && counts[2]!=counts[3],"four ordinary bosses have different patterns");
    InitGameplay(&a,94); a.boss.encounterIndex=4; SpawnBoss(&a,0,&events);
    for(int n=0;n<4;n++) DamageEnemy(&a,&a.enemies[a.boss.nodeSlots[n]],100,true,&events);
    a.upgrades[UPG_PIERCE]=2; DamageEnemy(&a,&a.enemies[a.boss.coreSlot],10000,true,&events);
    CHECK(a.upgradePending && events.recursionCompleted && a.boss.encounterIndex==5 && a.player.energy==100,
          "Leviathan clear offers upgrade and advances endless cycle");
    ChooseGameplayUpgrade(&a,0); AdvanceGameplayLoop(&a,333,5000);
    CHECK(a.upgrades[UPG_PIERCE]>=2 && !a.upgradePending,"salvage and existing build survive collapse");
    // Same seed repeats; across seeds the real Wraith can start in all slots.
    int startingSlots=0;
    for(unsigned int seed=0;seed<48;seed++) {
        InitGameplay(&a,seed); InitGameplay(&b,seed);
        a.boss.encounterIndex=b.boss.encounterIndex=2;
        SpawnBoss(&a,0,&events); SpawnBoss(&b,0,&events);
        startingSlots|=1<<((int)a.boss.prismTo[0]+1);
        CHECK(a.boss.prismTo[0]==b.boss.prismTo[0],"Prism starting identity is deterministic");
        CHECK(fabsf(a.enemies[a.boss.coreSlot].position.x-a.boss.prismTo[0]*9)<.001f,
              "Prism collision target starts in its visible slot");
    }
    CHECK(startingSlots==7,"Prism can start left, middle or right");
    InitGameplay(&a,94); a.boss.encounterIndex=2; SpawnBoss(&a,0,&events);
    a.boss.phase=BOSS_EXPOSED; a.boss.position.z=-46; a.boss.reinforcementTimer=1000;
    a.player.invulnerability=1000;
    float oldX=a.enemies[a.boss.coreSlot].position.x;
    float originalHealth=a.enemies[a.boss.coreSlot].health;
    int visited=0; bool continuous=true,targetAligned=true;
    for(int frame=0;frame<1800;frame++) {
        a.runTime+=1.0f/60;
        UpdateBoss(&a,1.0f/60,0,&events); UpdateProjectiles(&a,1.0f/60,&events);
        Vector3 core=a.enemies[a.boss.coreSlot].position;
        if(fabsf(core.x-oldX)>.5f) continuous=false;
        if(Vector3Distance(core,PrismBodyPosition(&a.boss,0))>.001f) targetAligned=false;
        oldX=core.x;
        visited|=1<<((int)a.boss.prismTo[0]+1);
    }
    CHECK(continuous && targetAligned,"Prism shuffles continuously with collision and render aligned");
    CHECK(a.boss.prismShuffles>=4 && visited==7,"Prism actively changes sides during combat");
    CHECK(a.enemies[a.boss.coreSlot].health==originalHealth,"Prism does not reset health while shuffling");

    float lastHealth=0,lastCadence=100,lastPressure=-1;
    const int depths[]={1,2,4,8,16,64};
    for(int d=0;d<6;d++) {
        InitGameplay(&a,94); a.boss.encounterIndex=depths[d]*5-1;
        SpawnBoss(&a,0,&events); a.difficulty=1; a.boss.phase=BOSS_EXPOSED;
        a.boss.attackPattern=1; a.boss.attackAim=a.player.position;
        FireBossPattern(&a);
        CHECK(a.boss.maxHealth>lastHealth && LoopPressure(&a)>lastPressure,"depth scales beyond four loops");
        CHECK(a.boss.fireTimer<=lastCadence && a.boss.fireTimer>=.8f,"depth cadence scales with warning floor");
        if(d<3) CHECK(a.boss.fireTimer<lastCadence,"early depths have clearly increasing cadence");
        CHECK(1-LoopPressure(&a)*.45f>0,"formation cadence stays positive at late depths");
        TraceLog(LOG_INFO,"BALANCE depth=%d LeviathanHP=%.1f exposedCooldown=%.2f pressure=%.3f",
                 depths[d],a.boss.maxHealth,a.boss.fireTimer,LoopPressure(&a));
        lastHealth=a.boss.maxHealth; lastCadence=a.boss.fireTimer; lastPressure=LoopPressure(&a);
    }
    // Heights are committed, not the old alternating top/bottom safe-center wave.
    InitGameplay(&a,94); a.boss.encounterIndex=19; SpawnBoss(&a,0,&events);
    a.boss.attackAim=(Vector3){0,3.5f,0}; a.boss.attackPattern=2;
    FireBossPattern(&a); bool hitsMiddle=false,leavesTop=true;
    for(int i=0;i<MAX_ENEMY_PROJECTILES;i++) if(a.projectiles[i].active) {
        EnemyProjectile *p=&a.projectiles[i];
        float y=p->position.y-p->position.z/p->velocity.z*p->velocity.y;
        if(fabsf(y-3.5f)<.01f) hitsMiddle=true;
        if(y>6.0f) leavesTop=false;
    }
    CHECK(hitsMiddle && leavesTop,"Leviathan sweep punishes camping but retains vertical escape");

    // Three minutes per boss, at depths 1/4/8, with all adds left alive. Check
    // complete volleys against an empty-pool reference, not just a size bound.
    for(int depth=1;depth<=8;depth=depth==1?4:depth+4) for(int form=0;form<5;form++) {
        InitGameplay(&a,94); a.boss.encounterIndex=(depth-1)*5+form;
        SpawnBoss(&a,0,&events); a.boss.position.z=-46; a.boss.phase=form==4?BOSS_SINGULARITY:BOSS_ENRAGED;
        a.difficulty=1; a.player.invulnerability=10000;
        bool complete=true,warned=true; int peak=0,volleys=0; float warningAge=0;
        for(int frame=0;frame<10800;frame++) {
            int pattern=a.boss.attackPattern;
            bool hadWarning=a.boss.warningArmed;
            if(hadWarning) warningAge+=1.0f/60;
            a.runTime+=1.0f/60;
            UpdateBoss(&a,1.0f/60,0,&events);
            if(a.boss.attackPattern!=pattern) {
                if(!hadWarning || warningAge<.65f) warned=false;
                warningAge=0; volleys++;
                int actual=0,expected=0;
                for(int i=0;i<MAX_ENEMY_PROJECTILES;i++) actual+=a.projectiles[i].active&&a.projectiles[i].kind>=2&&a.projectiles[i].life==7;
                b=a; b.boss.attackPattern=pattern;
                for(int i=0;i<MAX_ENEMY_PROJECTILES;i++) b.projectiles[i].active=false;
                FireBossPattern(&b);
                for(int i=0;i<MAX_ENEMY_PROJECTILES;i++) expected+=b.projectiles[i].active;
                if(actual!=expected) complete=false;
            }
            for(int i=0;i<MAX_ENEMIES;i++) if(a.enemies[i].active) UpdateEnemy(&a,&a.enemies[i],1.0f/60,false,&events);
            UpdateProjectiles(&a,1.0f/60,&events);
            int live=0; for(int i=0;i<MAX_ENEMY_PROJECTILES;i++) live+=a.projectiles[i].active;
            if(live>peak) peak=live;
        }
        CHECK(complete,"sustained boss fight never truncates a volley due to pool exhaustion");
        CHECK(warned && volleys>30,"sustained boss volleys retain a reaction window");
        TraceLog(LOG_INFO,"STRESS depth=%d boss=%d volleys=%d peakShots=%d complete=%d warned=%d",depth,form+1,volleys,peak,complete,warned);
    }
    // Every uncapped run offers faster specials and meaningful offense.
    for(unsigned int seed=0;seed<48;seed++) {
        InitGameplay(&a,seed); a.boss.encounterIndex=(int)(seed%40);
        OfferGameplayUpgrade(&a);
        CHECK(UpgradeFitsLane(a.upgradeOffers[0],0),"salvage includes faster specials");
        CHECK(UpgradeFitsLane(a.upgradeOffers[1],1),"salvage includes an offensive upgrade");
        a.upgrades[UPG_HARVEST]=a.upgrades[UPG_CAPACITOR]=a.upgrades[UPG_GRAZE]=3;
        OfferGameplayUpgrade(&a);
        CHECK(a.upgradeOffers[0]>=0 && a.upgrades[a.upgradeOffers[0]]<3 &&
              a.upgradeOffers[0]!=a.upgradeOffers[1] && a.upgradeOffers[0]!=a.upgradeOffers[2],
              "capped resource lane falls back without duplicate/capped offers");
    }
    for(int level=0;level<=3;level++) {
        InitGameplay(&a,94); a.upgrades[UPG_HARVEST]=level;
        Enemy *e=SpawnEnemyAt(&a,ENEMY_DRIFTER,(Vector3){0,2.4f,-18},0,1);
        DamageEnemy(&a,e,10,false,&events);
        CHECK(a.chainLinks==1+level,"harvester improves ordinary pulse kills");
        e=SpawnEnemyAt(&a,ENEMY_DRIFTER,(Vector3){0,2.4f,-18},0,2);
        DamageEnemy(&a,e,10,true,&events);
        CHECK(a.chainLinks==2*(1+level),"harvester improves ordinary charge kills");
        a.chainLinks=100;
        SpawnEnemyAt(&a,ENEMY_DRIFTER,(Vector3){0,2.4f,-18},0,3);
        FireLinkBeam(&a,&events);
        CHECK(a.chainLinks==50,"harvester cannot refund beam kills");
        a.chainLinks=100;
        SpawnEnemyAt(&a,ENEMY_DRIFTER,(Vector3){0,2.4f,-18},0,4);
        FireLinkBomb(&a,&events);
        CHECK(a.chainLinks==0,"harvester cannot refund bomb kills");
        a.chainLinks=99;
        e=SpawnEnemyAt(&a,ENEMY_DRIFTER,(Vector3){0,2.4f,-18},0,5);
        DamageEnemy(&a,e,10,false,&events);
        CHECK(a.chainLinks==100,"harvester respects resource cap");
    }
    // Real projectile flight through exposed cores, not a direct-damage mock.
    for(int variant=0;variant<4;variant++) {
        InitGameplay(&a,94); SpawnBoss(&a,0,&events);
        a.boss.phase=BOSS_EXPOSED; a.boss.shieldNodes=0;
        for(int n=0;n<4;n++) a.enemies[a.boss.nodeSlots[n]].active=false;
        Enemy *core=&a.enemies[a.boss.coreSlot]; core->position=(Vector3){0,2.4f,-18};
        float health=core->health;
        if(variant==1 || variant==2) a.upgrades[UPG_PIERCE]=3;
        if(variant==2) a.upgrades[UPG_TWIN]=3;
        if(variant==3) { a.upgrades[UPG_FOCUS]=3; core->locked=true; FireCharge(&a,&events); }
        else FireTap(&a,&events);
        for(int step=0;step<60;step++) UpdatePlayerProjectiles(&a,1.0f/60,&events);
        float expected=variant==0?1:variant==1?1.75f:variant==2?3.5f:5;
        CHECK(fabsf(health-core->health-expected)<.001f,"pulse/charge upgrades actually damage the exposed boss");
        TraceLog(LOG_INFO,"UPGRADE boss damage variant=%d actual=%.2f expected=%.2f",variant,health-core->health,expected);
    }
    InitGameplay(&a,94); SpawnBoss(&a,0,&events); a.boss.phase=BOSS_SHIELDED;
    Enemy *shielded=&a.enemies[a.boss.coreSlot]; shielded->position=(Vector3){0,2.4f,-18};
    float shieldedHealth=shielded->health; a.upgrades[UPG_PIERCE]=3;
    FireTap(&a,&events);
    for(int step=0;step<60;step++) UpdatePlayerProjectiles(&a,1.0f/60,&events);
    CHECK(shielded->health==shieldedHealth,"damage upgrade does not bypass boss shield");

    // Saturated enemy pool: every identity remains visible on the first frame,
    // withdraws without attacks/rewards, then frees five slots for the boss.
    InitGameplay(&a,94); a.boss.nextSpawnDistance=0; a.combo=8; a.comboTimer=2;
    for(int i=0;i<MAX_ENEMIES;i++) {
        EnemyType type=i%2?ENEMY_CARRIER:ENEMY_CHASER;
        Enemy *e=SpawnEnemyAt(&a,type,(Vector3){(i%2?4.0f:-4.0f),2.4f,-24-(float)i},0,(uint32_t)i);
        e->phase=ENEMY_TELEGRAPH; e->phaseTime=2; e->locked=true;
    }
    a.projectiles[0]=(EnemyProjectile){.active=true,.homing=true,.position={0,2.4f,-.1f},.velocity={0,0,35},.life=6};
    a.upgrades[UPG_HARVEST]=3;
    UpdateGameplay(&a,1.0f/60,1,false);
    int visible=0;
    for(int i=0;i<MAX_ENEMIES;i++) visible+=a.enemies[i].active&&a.enemies[i].phase==ENEMY_WITHDRAW;
    CHECK(visible==MAX_ENEMIES && a.boss.arrivalPending && !a.boss.active,"boss entrance preserves visible ships before withdrawal");
    CHECK(a.projectiles[0].active && a.projectiles[0].dissipating && !a.projectiles[0].homing && a.player.energy==100,
          "incoming projectiles dissipate visibly without a cutscene hit");
    CHECK(!IsEnemyTargetable(&a,&a.enemies[0]) && !CanGameplayBoost(&a),"withdrawal disables targeting and boost");
    float comboTime=a.comboTimer;
    float beforeX=a.enemies[0].position.x,beforeZ=a.enemies[0].position.z;
    int serial=a.spawnSerial;
    for(int frame=0;frame<30;frame++) UpdateGameplay(&a,1.0f/60,1,false);
    CHECK(a.enemies[0].position.x<beforeX && a.enemies[0].position.z<beforeZ,"ships visibly peel outward and away");
    CHECK(a.spawnSerial==serial && a.chainLinks==0 && a.score==0 && a.player.energy==100,
          "withdrawal cannot spawn, fire, damage, or award rewards");
    for(int frame=0;frame<115;frame++) UpdateGameplay(&a,1.0f/60,1,false);
    CHECK(a.boss.active && !a.boss.arrivalPending && a.boss.coreSlot>=0 && a.boss.shieldNodes==4,
          "saturated pool releases every departing ship before complete boss spawn");
    CHECK(a.comboTimer==comboTime && a.combo==8,"weapon chain survives forced boss handoff");
    CHECK(!a.projectiles[0].active && a.nearMisses==0,"dissipating projectiles expire without graze rewards");

    TraceLog(failures?LOG_ERROR:LOG_INFO,"PLAYABILITY: %d checks, %d failures",checks,failures);
#undef CHECK
    return failures==0;
}
