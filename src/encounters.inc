static void UpdateSpecialEnemy(GameplaySystem *game,Enemy *enemy,float dt,bool boosting,GameplayEvents *events) {
    enemy->age+=dt; enemy->phaseTime+=dt;
    if(enemy->age>30) { enemy->active=false; return; }
    float hold=enemy->type==ENEMY_SNIPER?-48.0f:enemy->type==ENEMY_CARRIER?-37.0f:-30.0f;
    if(enemy->phase==ENEMY_APPROACH) {
        enemy->position.z+=20*dt;
        if(enemy->position.z>=hold) { enemy->position.z=hold; SetEnemyPhase(enemy,ENEMY_HOVER); }
        return;
    }
    if(enemy->phase==ENEMY_HOVER) {
        enemy->position.x=enemy->basePosition.x+sinf(enemy->age*.65f+enemy->identity)*1.5f;
        enemy->fireTimer-=dt*(boosting?1.7f:1.0f)*(1.0f+LoopPressure(game)*.40f);
        if(enemy->fireTimer<=0) {
            enemy->attackAim=game->player.position;
            SetEnemyPhase(enemy,ENEMY_TELEGRAPH);
            if(enemy->type==ENEMY_SNIPER) events->attackWarning=true;
        }
    } else if(enemy->phase==ENEMY_TELEGRAPH && enemy->phaseTime>=1.05f) {
        if(enemy->type==ENEMY_CARRIER) {
            for(int side=-1;side<=1;side+=2) {
                Vector3 p=enemy->position; p.x+=side*2.2f; p.z-=2;
                Enemy *child=SpawnEnemyAt(game,ENEMY_CHASER,p,1,
                    game->runSeed^(uint32_t)game->spawnSerial*31u);
                if(child) { child->phase=ENEMY_HOVER; child->fireTimer=1.4f; }
            }
        } else SpawnAimedProjectile(game,enemy->position,enemy->attackAim,
            enemy->type==ENEMY_SNIPER?46.0f:24.0f,enemy->type==ENEMY_SNIPER?1:0);
        enemy->fireTimer=enemy->type==ENEMY_CARRIER?4.4f:enemy->type==ENEMY_SNIPER?2.8f:2.3f;
        SetEnemyPhase(enemy,ENEMY_HOVER);
    }
}

static void DrawRoleEnemy(const GameplaySystem *game,const Enemy *enemy) {
    Vector3 p=enemy->position;
    Color hull={58,71,80,255},edge={151,163,159,255};
    if(enemy->type==ENEMY_SNIPER) {
        DrawCube(p,1.6f,1.0f,3.7f,hull);
        DrawCylinderEx((Vector3){p.x,p.y,p.z+1},(Vector3){p.x,p.y,p.z+4},.28f,.19f,8,edge);
        DrawCube((Vector3){p.x,p.y+.8f,p.z},2.8f,.18f,1.3f,hull);
    } else if(enemy->type==ENEMY_CARRIER) {
        for(int side=-1;side<=1;side+=2) {
            DrawCube((Vector3){p.x+side*1.55f,p.y,p.z},1.3f,1.6f,4.8f,hull);
            for(int bay=0;bay<3;bay++) DrawCube((Vector3){p.x+side*1.55f,p.y-.3f,p.z-1.4f+bay*1.35f},1.4f,.65f,.65f,(Color){27,38,42,255});
        }
        DrawCube(p,2.6f,.6f,2,edge);
    } else {
        DrawSphereEx(p,1.1f,6,8,hull);
        for(int i=0;i<4;i++) {
            float a=i*PI*.5f+enemy->age*.7f;
            Vector3 tip={p.x+cosf(a)*2.1f,p.y+sinf(a)*2.1f,p.z};
            DrawCylinderEx(p,tip,.13f,.08f,6,edge);
            DrawCube(tip,.45f,.45f,1.3f,hull);
        }
        DrawSphereWires(p,2.5f,4,8,(Color){104,154,179,190});
        for(int i=0;i<MAX_ENEMIES;i++) {
            const Enemy *other=&game->enemies[i];
            if(other!=enemy && other->active && other->type!=ENEMY_BOSS_CORE && other->type!=ENEMY_BOSS_NODE &&
                other->type!=ENEMY_SUPPORT && Vector3DistanceSqr(other->position,p)<225)
                DrawLine3D(p,other->position,(Color){99,155,177,160});
        }
    }
    if(enemy->phase==ENEMY_TELEGRAPH) {
        DrawSphereWires(p,1.9f,6,8,(Color){244,191,128,255});
        if(enemy->type==ENEMY_SNIPER) DrawLine3D(p,enemy->attackAim,(Color){244,170,111,210});
    }
}
