// Patterns aim at the flight plane and retain safe openings. Warning geometry
// and live shots use the same cached lane/aim; neither tracks after committing.
static void FireBossPattern(GameplaySystem *game) {
    BossState *b=&game->boss;
    int form=b->encounterIndex%BOSSES_PER_RECURSION;
    int depth=GetGameplayRecursionDepth(game);
    bool advanced=depth>1;
    float pressure=ClampFloat((b->encounterTime-65.0f)/90.0f,0,1);
    float speed=27.0f+game->difficulty*5+LoopPressure(game)*7+pressure*5;
    Vector3 origin=b->position;
    if(form==0) {
        // A broad wall with a visibly marked moving corridor.
        for(int column=0;column<15;column++) {
            float x=-14.0f+column*2.0f;
            if(fabsf(x-b->safeLane)<3.4f) continue;
            for(int row=0;row<5;row++)
                SpawnAimedProjectile(game,origin,(Vector3){x,1.0f+row*1.5f,0},speed,3);
        }
        if(advanced && b->attackPattern%3==2) {
            Vector3 rear=origin; rear.z-=12;
            SpawnAimedProjectile(game,rear,b->attackAim,speed,2);
        }
    } else if(form==1) {
        // Each intact cannon contributes a different pattern. Removing a gun
        // really removes its attack; the exposed central core has a light fan.
        int live=0;
        for(int node=0;node<4;node++) {
            int slot=b->nodeSlots[node];
            if(slot<0 || !game->enemies[slot].active) continue;
            live++;
            Vector3 gun=game->enemies[slot].position;
            if(node==0 || node==3) {
                for(int j=-1;j<=1;j++) {
                    Vector3 aim=b->attackAim; aim.x+=j*2.5f;
                    SpawnAimedProjectile(game,gun,aim,speed+(node==3?6:0),2);
                }
            } else for(int j=0;j<4;j++) {
                Vector3 aim={node==1?-11.5f+j*3.0f:2.5f+j*3.0f,2.0f+(b->attackPattern%2)*3.0f,0};
                SpawnAimedProjectile(game,gun,aim,speed,2);
            }
        }
        if(live==0) for(int j=-2;j<=2;j++) {
            Vector3 aim=b->attackAim; aim.x+=j*2.5f;
            SpawnAimedProjectile(game,origin,aim,speed+3,2);
        }
    } else if(form==2) {
        // Identity follows the moving body, not the central screen slot.
        for(int copy=0;copy<3;copy++) {
            Vector3 p=PrismBodyPosition(b,copy);
            for(int j=-1;j<=1;j++) {
                Vector3 aim=b->attackAim; aim.x+=(copy==0?0:(copy==1?-6:6))+j*2.4f;
                SpawnAimedProjectile(game,p,aim,speed+(copy==0?3:0),2);
            }
        }
        if(advanced) for(int j=-1;j<=1;j+=2) {
            Vector3 aim=b->attackAim; aim.x+=j*5;
            SpawnAimedProjectile(game,PrismBodyPosition(b,0),aim,speed+3,2);
        }
    } else if(form==3) {
        // Alternating claw banks rake one side. A broad safe side is retained.
        float side=b->attackPattern%2?1.0f:-1.0f;
        for(int col=0;col<8;col++) for(int row=0;row<5;row++) {
            Vector3 claw=origin; claw.x+=side*8;
            Vector3 aim={side*(-3.0f+col*2.4f),1.0f+row*1.5f,0};
            SpawnAimedProjectile(game,claw,aim,speed,3);
        }
        if(advanced && b->attackPattern%3==2) {
            Vector3 aim=b->attackAim; Vector3 delayed=origin; delayed.z-=15;
            SpawnAimedProjectile(game,delayed,aim,speed,2);
        }
    } else {
        // Wall -> aimed spread -> height sweep. Later depths add a second
        // height row and a staggered crossfire, not unavoidable hit damage.
        int pattern=b->attackPattern%3;
        if(pattern==0) {
            for(int col=0;col<15;col++) {
                float x=-14+col*2.0f;
                if(fabsf(x-b->safeLane)<3.5f) continue;
                for(int row=0;row<5;row++) SpawnAimedProjectile(game,origin,(Vector3){x,1+row*1.5f,0},speed+2,3);
            }
        } else if(pattern==1) {
            for(int j=-4;j<=4;j++) {
                Vector3 aim=b->attackAim; aim.x+=j*2.8f;
                Vector3 p=origin; p.x+=j*1.2f;
                SpawnAimedProjectile(game,p,aim,speed+4,2);
            }
            if(advanced) for(int side=-1;side<=1;side+=2) {
                Vector3 p=origin; p.x+=side*11; p.z-=14;
                for(int j=-1;j<=1;j++) {
                    Vector3 aim=b->attackAim; aim.x+=side*5+j*2.4f;
                    SpawnAimedProjectile(game,p,aim,speed+1,2);
                }
            }
        } else {
            // Lock the sweep to the player's height, so sitting at mid-height
            // is no longer a permanent solution. Always leave vertical room.
            float y=ClampFloat(b->attackAim.y,1.5f,6.3f);
            for(int row=0;row<(depth>=3?2:1);row++) for(int j=0;j<13;j++) {
                float rowY=row?y+(y>3.8f?-1.9f:1.9f):y;
                Vector3 aim={-14+j*(28.0f/12),rowY,0};
                SpawnAimedProjectile(game,origin,aim,speed+2,2);
            }
            if(depth>=4) for(int side=-1;side<=1;side+=2) {
                Vector3 p=origin; p.x+=side*10; p.z-=16;
                Vector3 aim=b->attackAim; aim.x+=side*3.5f;
                SpawnAimedProjectile(game,p,aim,speed,2);
            }
        }
    }
    b->attackPattern++;
    float cooldown=form==1?2.5f:form==2?2.3f:form==3?2.8f:form==4?2.15f:2.7f;
    cooldown/=1.0f+.20f*(float)(depth-1);
    if(b->phase==BOSS_ENRAGED) cooldown*=.82f;
    if(b->phase==BOSS_SINGULARITY) cooldown*=.70f;
    // Wall-heavy bosses retain spacing; other volleys have at least .8s to
    // telegraph plus physical flight time. No point-blank/unwarned follow-up.
    float floor=form==0||form==3?1.65f:.80f;
    b->fireTimer=fmaxf(floor,cooldown-pressure*.25f-LoopPressure(game)*.10f);
    if(form==2 && b->attackPattern%2==0) {
        ShufflePrism(game);
        b->fireTimer=fmaxf(b->fireTimer,2.25f); // complete crossing before aim commits
    }
    b->warningArmed=false;
}

static void DrawBossPatternHints(const GameplaySystem *game,Camera3D camera,int w,int h) {
    const BossState *b=&game->boss;
    if(!b->active || b->phase==BOSS_APPROACH) return;
    int form=b->encounterIndex%BOSSES_PER_RECURSION;
    if(!b->warningArmed) return;
    Color warning={232,63,67,205};
    // Small paired ticks on the cached aim; no text over the combat space.
    Vector2 aim=GetWorldToScreenEx(b->attackAim,camera,w,h);
    if(form!=0 && form!=3 && !(form==4&&b->attackPattern%3==0)) {
        DrawLineEx((Vector2){aim.x-15,aim.y-5},(Vector2){aim.x-8,aim.y},2,warning);
        DrawLineEx((Vector2){aim.x+15,aim.y-5},(Vector2){aim.x+8,aim.y},2,warning);
    }
    if(form==0 || (form==4&&b->attackPattern%3==0) || form==3) {
        float safe=form==3?(b->attackPattern%2?-8.0f:8.0f):b->safeLane;
        Vector2 p=GetWorldToScreenEx((Vector3){safe,3.6f,0},camera,w,h);
        p.x=ClampFloat(p.x,45,(float)w-45);
        // Inward brackets communicate a gap, rather than the unexplained OPEN.
        Color safeColor={160,211,194,225};
        float y=(float)h-180;
        for(int side=-1;side<=1;side+=2) {
            float x=p.x+side*25;
            DrawLineEx((Vector2){x,y-10},(Vector2){x,y+10},2,safeColor);
            DrawLineEx((Vector2){x,y-10},(Vector2){x-side*9,y-10},2,safeColor);
            DrawLineEx((Vector2){x,y+10},(Vector2){x-side*9,y+10},2,safeColor);
        }
    }
}
