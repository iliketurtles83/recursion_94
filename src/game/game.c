#include "game.h"
#include "mission.h"
#include "techno.h"
#include "render/linegeo.h"
#include "render/shaderlib.h"
#include "raymath.h"
#include "rlgl.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct Game {
    CtTunnel *ct;
    Mission m;
    ShaderLib shaders;
    int shader;
    Material material;
    Mesh ring, glowRing, targetRing, sphere, cube;
    Font font, smallFont;
    bool ownFont, audio, mute, paused, boost, quit;
    Sound engine, collect, hit, finish;
    AudioStream music;
    Techno *techno;
    float uiTime, hitFlash, lastX, lastY;
    MissionPhase lastPhase;
    CtSceneView view;
} Game;

static const Color HUD_WHITE={222,237,241,255}, DIM={129,164,179,255};
static const Color CYAN={67,206,232,255}, AMBER={255,187,86,255}, HUD_RED={255,88,101,255};
static const Color LASER_COLOR={178,108,255,255};
static float clamp(float x,float lo,float hi) {return fminf(fmaxf(x,lo),hi);}

static Mesh ring_mesh(float tube)
{
    LineGeo g={0};
    for(int i=0;i<96;i++) for(int j=0;j<12;j++) {
        int b=lg_reserve(&g,4,6);
        const int di[4]={0,1,1,0},dj[4]={0,0,1,1};
        for(int k=0;k<4;k++) {
            float a=(i+di[k])*2*PI/96,c=(j+dj[k])*2*PI/12;
            Vector3 n={cosf(a)*cosf(c),sinf(a)*cosf(c),sinf(c)};
            Vector3 p={(1+tube*cosf(c))*cosf(a),(1+tube*cosf(c))*sinf(a),tube*sinf(c)};
            lg_vertex(&g,p,(Vector2){0},(Vector2){0},n,(Vector4){0},HUD_WHITE);
        }
        lg_quad(&g,b,0,1,2,3);
    }
    Mesh mesh=lg_upload_mesh(&g,0,false); lg_free(&g);return mesh;
}
static Matrix basis(CtPathSample p,Vector3 pos)
{
    Matrix m=MatrixIdentity();
    m.m0=p.binormal.x;m.m1=p.binormal.y;m.m2=p.binormal.z;
    m.m4=p.normal.x;m.m5=p.normal.y;m.m6=p.normal.z;
    m.m8=p.tangent.x;m.m9=p.tangent.y;m.m10=p.tangent.z;
    m.m12=pos.x;m.m13=pos.y;m.m14=pos.z;return m;
}
static Vector3 event_pos(Game *g,const CtSceneView *v,double s,float x,float y)
{
    CtPathSample p=ct_path_sample(g->ct,s);
    return (Vector3){(float)(p.pos[0]-v->origin[0])+p.binormal.x*x+p.normal.x*y,
                     (float)(p.pos[1]-v->origin[1])+p.binormal.y*x+p.normal.y*y,
                     (float)(p.pos[2]-v->origin[2])+p.binormal.z*x+p.normal.z*y};
}
static void actor(Game *g,Mesh mesh,Matrix parent,Vector3 offset,Vector3 scale,float angle,Color color,float emission)
{
    Matrix local=MatrixMultiply(MatrixScale(scale.x,scale.y,scale.z),MatrixRotateZ(angle));
    local=MatrixMultiply(local,MatrixTranslate(offset.x,offset.y,offset.z));
    Vector3 linear={powf(color.r/255.0f,2.2f),powf(color.g/255.0f,2.2f),powf(color.b/255.0f,2.2f)};
    sl_v3(&g->shaders,g->shader,"uColor",linear);
    sl_f1(&g->shaders,g->shader,"uEmission",emission);
    DrawMesh(mesh,g->material,MatrixMultiply(local,parent));
}
static void laser_wall(Game *g,Matrix xf,const MissionEvent *e,bool emissive)
{
    if(!emissive) {
        actor(g,g->ring,xf,(Vector3){0},(Vector3){7.8f,7.8f,2},0,(Color){74,72,99,255},0);
        for(int side=-1;side<=1;side+=2)actor(g,g->cube,xf,(Vector3){side*7,0,0},(Vector3){.32f,9,.5f},0,(Color){93,86,118,255},0);
        actor(g,g->ring,xf,(Vector3){e->x,e->y,-.05f},(Vector3){e->radius,e->radius,1},0,(Color){43,115,131,255},0);
        return;
    }
    for(int row=-6;row<=6;row++) {
        float y=row,half=sqrtf(fmaxf(7.5f*7.5f-y*y,0));
        float dy=y-e->y;
        if(fabsf(dy)<e->radius) {
            float gap=sqrtf(e->radius*e->radius-dy*dy),left=e->x-gap,right=e->x+gap;
            if(left>-half)actor(g,g->cube,xf,(Vector3){(-half+left)*.5f,y,0},(Vector3){left+half,.045f,.045f},0,LASER_COLOR,1.05f);
            if(right<half)actor(g,g->cube,xf,(Vector3){(half+right)*.5f,y,0},(Vector3){half-right,.045f,.045f},0,LASER_COLOR,1.05f);
        } else actor(g,g->cube,xf,(Vector3){0,y,0},(Vector3){2*half,.045f,.045f},0,LASER_COLOR,1.05f);
    }
    actor(g,g->targetRing,xf,(Vector3){e->x,e->y,-.10f},(Vector3){e->radius,e->radius,1},0,CYAN,2.2f);
}
static void scene(void *user,const CtSceneView *view,bool emissive)
{
    Game *g=user;
    if(!view->reflection) g->view=*view;
    sl_v3(&g->shaders,g->shader,"uCamPos",view->camera.position);
    double ship=ct_ship_s(g->ct);
    for(int i=0;i<g->m.eventCount;i++) {
        MissionEvent *e=&g->m.events[i];
        if(e->resolved || e->s<ship-6 || e->s>ship+340) continue;
        float ex,ey;mission_event_position(e,g->m.elapsed,&ex,&ey);
        Matrix xf=basis(ct_path_sample(g->ct,e->s),event_pos(g,view,e->s,ex,ey));
        float pulse=1+.06f*sinf(g->uiTime*3+i);
        if(e->kind==EVENT_BARRIER) {
            Matrix wall=basis(ct_path_sample(g->ct,e->s),event_pos(g,view,e->s,0,0));
            laser_wall(g,wall,e,emissive);continue;
        }
        if(e->kind==EVENT_PACKET) {
            if(!emissive) {
                actor(g,g->ring,xf,(Vector3){0},(Vector3){2.26f,2.26f,1},0,(Color){113,90,54,255},0);
                for(int k=0;k<4;k++) {
                    float a=k*PI/2;
                    actor(g,g->cube,xf,(Vector3){cosf(a)*2.26f,sinf(a)*2.26f,0},(Vector3){.40f,.23f,.3f},a,(Color){176,150,107,255},0);
                }
            } else {
                actor(g,g->targetRing,xf,(Vector3){0,0,-.10f},(Vector3){2.26f,2.26f,1},0,AMBER,2.4f*pulse);
                actor(g,g->cube,xf,(Vector3){0},(Vector3){.42f,.42f,.12f},g->uiTime,AMBER,1.8f);
                actor(g,g->glowRing,xf,(Vector3){0},(Vector3){.55f,.55f,1},-g->uiTime,CYAN,1.3f);
            }
        } else if(e->kind==EVENT_MINE) {
            if(!emissive) {
                actor(g,g->sphere,xf,(Vector3){0},(Vector3){.65f,.65f,.65f},0,(Color){105,45,63,255},0);
                for(int k=0;k<8;k++) {
                    float a=k*PI/4+g->m.elapsed*.5f;
                    actor(g,g->cube,xf,(Vector3){cosf(a)*.68f,sinf(a)*.68f,0},(Vector3){.46f,.12f,.16f},a,(Color){135,97,112,255},0);
                }
            } else {
                actor(g,g->targetRing,xf,(Vector3){0},(Vector3){.76f,.76f,1},0,HUD_RED,2);
                actor(g,g->sphere,xf,(Vector3){0,0,-.58f},(Vector3){.18f,.18f,.08f},0,HUD_RED,2.8f);
            }
        } else {
            if(!emissive) {
                actor(g,g->sphere,xf,(Vector3){0},(Vector3){.8f,.6f,.65f},0,(Color){128,55,65,255},0);
                actor(g,g->cube,xf,(Vector3){0},(Vector3){2.5f,.18f,.3f},0,(Color){146,73,82,255},0);
                for(int side=-1;side<=1;side+=2)
                {
                    actor(g,g->cube,xf,(Vector3){side*1.1f,0,0},(Vector3){.25f,.55f,.3f},0,(Color){72,89,111,255},0);
                    actor(g,g->ring,xf,(Vector3){side*1.1f,0,0},(Vector3){.35f,.35f,1},0,(Color){142,119,130,255},0);
                    for(int k=0;k<3;k++)actor(g,g->cube,xf,(Vector3){side*.48f,.36f-k*.15f,-.42f},(Vector3){.18f,.045f,.06f},0,(Color){49,26,37,255},0);
                }
            } else {
                actor(g,g->sphere,xf,(Vector3){0,0,-.60f},(Vector3){.23f,.13f,.09f},0,HUD_RED,3*pulse);
                actor(g,g->targetRing,xf,(Vector3){0},(Vector3){1.1f,1.1f,1},g->uiTime,HUD_RED,1.8f);
                if(e->kind==EVENT_SWEEPER)for(int side=-1;side<=1;side+=2)
                    actor(g,g->glowRing,xf,(Vector3){side*1.1f,0,0},(Vector3){.29f,.29f,1},0,HUD_RED,1.4f);
            }
        }
    }
    if(MISSION_FINISH>=ship-8 && MISSION_FINISH<ship+360) {
        Matrix xf=basis(ct_path_sample(g->ct,MISSION_FINISH),event_pos(g,view,MISSION_FINISH,0,1.5f));
        Color color=g->m.packets>=MISSION_REQUIRED?CYAN:AMBER;
        actor(g,emissive?g->glowRing:g->ring,xf,(Vector3){0},(Vector3){4.35f,4.35f,2},0,color,emissive?3:0);
        if(emissive) actor(g,g->glowRing,xf,(Vector3){0,0,1},(Vector3){3.9f,3.9f,1},0,color,1.2f);
    }
}

// Original generated audio; no downloaded music or sound effects.
static Sound synth(float seconds,float frequency,int mode)
{
    int count=(int)(44100*seconds);
    short *data=MemAlloc(count*sizeof(short));
    for(int i=0;i<count;i++) {
        float t=i/44100.0f, a=t/seconds;
        float envelope=mode<2?1:sinf(PI*a)*expf(-3*a);
        float f=frequency*(mode==2?(1+1.8f*a):mode==3?(1-.7f*a):1);
        float v=sinf(2*PI*f*t)*.38f+sinf(2*PI*f*2*t)*.12f;
        if(mode==1) v=(sinf(2*PI*55*t)+sinf(2*PI*82.5f*t)+sinf(2*PI*110*t))*.12f*(.65f+.35f*sinf(2*PI*.5f*t));
        if(mode==0) v*=.65f+.35f*sinf(2*PI*8*t);
        // Fade the endpoints to keep looping and one-shot effects click-free.
        v*=envelope*clamp(t/.01f,0,1)*clamp((seconds-t)/.02f,0,1);
        data[i]=(short)(v*23000);
    }
    Wave w={.frameCount=count,.sampleRate=44100,.sampleSize=16,.channels=1,.data=data};
    Sound s=LoadSoundFromWave(w);MemFree(data);return s;
}
static Techno *callbackMusic;
static void music_callback(void *buffer,unsigned int frames){techno_render(callbackMusic,buffer,frames);}
static void audio_frame(Game *g)
{
    if(!g->audio)return;
    CtStats stats;ct_get_stats(g->ct,&stats);
    float base=ct_param_get(g->ct,"baseSpeed"),range=ct_param_get(g->ct,"boostSpeed")-base;
    float drive=clamp((stats.speed-base)/fmaxf(range,.01f),0,1);
    techno_control(g->techno,g->m.phase==MISSION_RUNNING?drive:0,g->m.phase==MISSION_RUNNING,g->paused,g->mute);
    if(g->mute || g->paused) {SetMasterVolume(0);return;}
    SetMasterVolume(.65f);
    if(g->m.phase==MISSION_RUNNING) {
        if(!IsSoundPlaying(g->engine))PlaySound(g->engine);
        SetSoundPitch(g->engine,1+.55f*drive);
        SetSoundVolume(g->engine,.12f);
    } else StopSound(g->engine);
    if(g->m.justCollected)PlaySound(g->collect);
    if(g->m.justHit)PlaySound(g->hit);
    if(g->m.phase==MISSION_WON && g->lastPhase!=MISSION_WON)PlaySound(g->finish);
}

static MissionSample ship_sample(Game *g)
{
    double s=ct_ship_s(g->ct),p[3];ct_ship_world(g->ct,p);
    CtPathSample path=ct_path_sample(g->ct,s);
    Vector3 delta={(float)(p[0]-path.pos[0]),(float)(p[1]-path.pos[1]),(float)(p[2]-path.pos[2])};
    return (MissionSample){s,Vector3DotProduct(delta,path.binormal),Vector3DotProduct(delta,path.normal)};
}
static void restart(Game *g,bool start)
{
    mission_reset(&g->m,ct_get_seed(g->ct));
    ct_jump_to(g->ct,0);ct_set_paused(g->ct,true);ct_set_input(g->ct,&(CtInput){0});
    g->paused=false;g->boost=false;g->lastX=g->lastY=0;g->hitFlash=0;
    if(start)mission_start(&g->m);
}
static bool select_route(Game *g,int environment)
{
    ct_set_environment(g->ct,environment);
    bool loaded=ct_preset_load(g->ct,environment==CT_ENV_CITY?"mission":"mission_tunnel");
    restart(g,false);
    SetWindowTitle(environment==CT_ENV_CITY?"DATA // RUN - Grid City":"DATA // RUN - Data Vortex");
    return loaded;
}
static CtInput bot(Game *g,MissionSample p,float dt)
{
    CtInput in={0};float x=0,y=.7f;
    int next=mission_next_packet(&g->m);
    if(next>=0) {x=g->m.events[next].x;y=g->m.events[next].y;}
    CtStats stats;ct_get_stats(g->ct,&stats);
    for(int i=0;i<g->m.eventCount;i++) {
        MissionEvent *e=&g->m.events[i];
        if(e->resolved||e->kind==EVENT_PACKET||e->s<=p.s||e->s-p.s>72)continue;
        float time=g->m.elapsed+(float)(e->s-p.s)/fmaxf(stats.speed,32);
        if(e->kind==EVENT_BARRIER){x=e->x;y=e->y;break;}
        if(!mission_event_danger(e,time,x,y,1.1f))continue;
        float best=1e9f,bx=x,by=y;
        const float candidates[]={-4.4f,-3.4f,-1.7f,0,1.7f,3.4f,4.4f};
        for(int ix=0;ix<7;ix++)for(int iy=0;iy<5;iy++) {
            float tx=candidates[ix],ty=-2.8f+iy*1.3f;
            float cost=hypotf(tx-x,ty-y)+.15f*hypotf(tx-p.x,ty-p.y);
            for(int j=i;j<g->m.eventCount;j++) {
                MissionEvent *hazard=&g->m.events[j];
                if(hazard->resolved||hazard->kind==EVENT_PACKET||hazard->s>e->s+8)continue;
                float ht=g->m.elapsed+(float)(hazard->s-p.s)/fmaxf(stats.speed,32);
                if(mission_event_danger(hazard,ht,tx,ty,1.1f))cost+=100;
            }
            if(cost<best){best=cost;bx=tx;by=ty;}
        }
        x=bx;y=by;break;
    }
    float vx=(p.x-g->lastX)/fmaxf(dt,.001f),vy=(p.y-g->lastY)/fmaxf(dt,.001f);
    in.steerX=clamp((x-p.x)*.8f-vx*.10f,-1,1);
    in.steerY=clamp((y-p.y)*.8f-vy*.10f,-1,1);
    in.boost=mission_can_boost(&g->m);
    return in;
}
static void frame(Game *g,float dt,bool automated,bool idle)
{
    dt=clamp(dt,0,.25f);g->uiTime+=dt;
    if(!automated) {
        if(g->m.phase==MISSION_TITLE||g->m.phase>=MISSION_WON) {
            float routeScale=fminf(GetScreenWidth()/1280.0f,GetScreenHeight()/720.0f);
            Vector2 mouse=GetMousePosition();
            float mx=(mouse.x-(GetScreenWidth()-1280*routeScale)*.5f)/routeScale;
            float my=(mouse.y-(GetScreenHeight()-720*routeScale)*.5f)/routeScale;
            bool click=IsMouseButtonPressed(MOUSE_BUTTON_LEFT)&&my>=572&&my<=596;
            if(IsKeyPressed(KEY_ONE)||(click&&mx>=76&&mx<=254))select_route(g,CT_ENV_VORTEX);
            if(IsKeyPressed(KEY_TWO)||(click&&mx>=276&&mx<=486))select_route(g,CT_ENV_CITY);
        }
        if(IsKeyPressed(KEY_M))g->mute=!g->mute;
        if(IsKeyPressed(KEY_F11))ToggleFullscreen();
        if(IsKeyPressed(KEY_R))restart(g,true);
        if(IsKeyPressed(KEY_BACKSPACE))restart(g,false);
        if(IsKeyPressed(KEY_ESCAPE)) {
            if(g->m.phase==MISSION_RUNNING||g->m.phase==MISSION_COUNTDOWN)g->paused=!g->paused;
            else if(g->m.phase==MISSION_TITLE)g->quit=true;
            else restart(g,false);
        }
        if(IsKeyPressed(KEY_ENTER)) {
            if(g->paused)g->paused=false;
            else if(g->m.phase==MISSION_TITLE||g->m.phase>=MISSION_WON)restart(g,true);
        }
        float scale=fminf(GetScreenWidth()/1280.0f,GetScreenHeight()/720.0f);
        Rectangle button={(GetScreenWidth()-1280*scale)*.5f+76*scale,(GetScreenHeight()-720*scale)*.5f+504*scale,332*scale,54*scale};
        if((g->m.phase==MISSION_TITLE||g->m.phase>=MISSION_WON)&&IsMouseButtonPressed(MOUSE_BUTTON_LEFT)&&CheckCollisionPointRec(GetMousePosition(),button))restart(g,true);
        if(!IsWindowFocused()&&(g->m.phase==MISSION_RUNNING||g->m.phase==MISSION_COUNTDOWN))g->paused=true;
    }
    bool flying=g->m.phase==MISSION_RUNNING&&!g->paused;
    ct_set_paused(g->ct,!flying);
    MissionSample p=ship_sample(g);
    CtInput in={0};
    if(flying) {
        if(automated&&!idle)in=bot(g,p,dt);
        else if(!automated) {
            in.steerX=(float)((IsKeyDown(KEY_RIGHT)||IsKeyDown(KEY_D))-(IsKeyDown(KEY_LEFT)||IsKeyDown(KEY_A)));
            in.steerY=(float)((IsKeyDown(KEY_UP)||IsKeyDown(KEY_W))-(IsKeyDown(KEY_DOWN)||IsKeyDown(KEY_S)));
            in.boost=IsKeyDown(KEY_LEFT_SHIFT)||IsKeyDown(KEY_RIGHT_SHIFT)||IsKeyDown(KEY_SPACE);
            in.rollLeft=IsKeyPressed(KEY_Q);in.rollRight=IsKeyPressed(KEY_E);
        }
        in.boost=in.boost&&mission_can_boost(&g->m);
    }
    g->lastX=p.x;g->lastY=p.y;g->boost=in.boost;
    // Consume the whole rendered frame in safe slices. The previous 50 ms cap
    // made a 120 ms frame advance only 50 ms of flight: visible slow motion.
    int collected=0,hit=0,near=0;
    if(dt<=1e-6f)ct_update(g->ct,0.0f);
    for(float remaining=dt;remaining>1e-6f;) {
        float slice=fminf(remaining,1/60.0f);remaining-=slice;
        ct_set_paused(g->ct,g->paused||g->m.phase!=MISSION_RUNNING);
        ct_set_input(g->ct,&in);in.rollLeft=in.rollRight=false;
        ct_update(g->ct,slice);
        if(!g->paused){
            mission_tick(&g->m,slice,ship_sample(g),in.boost);
            collected+=g->m.justCollected;hit+=g->m.justHit;near+=g->m.justNear;
        }
    }
    g->m.justCollected=collected;g->m.justHit=hit;g->m.justNear=near;
    g->hitFlash=fmaxf(g->hitFlash-dt,0);
    if(hit)g->hitFlash=.4f;
    audio_frame(g);g->lastPhase=g->m.phase;
}

typedef struct Hud {Game *g;float s,ox,oy,w,h;} Hud;
static void text_at(Hud *h,const char *t,float x,float y,float size,Color c)
{
    DrawTextEx(size<40?h->g->smallFont:h->g->font,t,(Vector2){h->ox+x*h->s,h->oy+y*h->s},size*h->s,1*h->s,c);
}
static void rect(Hud *h,float x,float y,float w,float height,Color c)
{
    DrawRectangleRec((Rectangle){h->ox+x*h->s,h->oy+y*h->s,w*h->s,height*h->s},c);
}
static void line(Hud *h,float x,float y,float x2,float y2,Color c)
{
    DrawLineEx((Vector2){h->ox+x*h->s,h->oy+y*h->s},(Vector2){h->ox+x2*h->s,h->oy+y2*h->s},h->s,c);
}
static void target_marker(Hud *h,const MissionEvent *e,bool packet,int width,int height)
{
    Game *g=h->g;
    if(g->view.camera.fovy<=0)return;
    float ex,ey;mission_event_position(e,g->m.elapsed,&ex,&ey);
    Vector3 pos=event_pos(g,&g->view,e->s,ex,ey);
    Vector3 fwd=Vector3Normalize(Vector3Subtract(g->view.camera.target,g->view.camera.position));
    if(Vector3DotProduct(Vector3Subtract(pos,g->view.camera.position),fwd)<=0)return;
    Vector2 p=GetWorldToScreenEx(pos,g->view.camera,width,height);
    if(p.x<20*h->s||p.x>width-20*h->s||p.y<h->oy+85*h->s||p.y>h->oy+640*h->s)return;
    CtPathSample path=ct_path_sample(g->ct,e->s);
    Vector3 edge=Vector3Add(pos,Vector3Scale(path.binormal,e->radius));
    float r=clamp(Vector2Distance(p,GetWorldToScreenEx(edge,g->view.camera,width,height)),12*h->s,112*h->s)+6*h->s;
    Color color=packet?AMBER:e->kind==EVENT_BARRIER?CYAN:HUD_RED;
    for(int sx=-1;sx<=1;sx+=2)for(int sy=-1;sy<=1;sy+=2) {
        Vector2 corner={p.x+sx*r,p.y+sy*r};
        Vector2 horizontal={corner.x-sx*12*h->s,corner.y},vertical={corner.x,corner.y-sy*12*h->s};
        DrawLineEx(corner,horizontal,4*h->s,(Color){3,10,18,240});DrawLineEx(corner,vertical,4*h->s,(Color){3,10,18,240});
        DrawLineEx(corner,horizontal,2*h->s,color);DrawLineEx(corner,vertical,2*h->s,color);
    }
}
static void readout(Hud *h,const char *text,float x,float y,float size,Color color)
{
    text_at(h,text,x+1.4f,y+1.4f,size,(Color){2,7,13,245});
    text_at(h,text,x,y,size,color);
}
static void hud(Game *g,int w,int height)
{
    Hud h={g,fminf(w/1280.0f,height/720.0f),0,0,1280,720};
    h.ox=(w-1280*h.s)*.5f;h.oy=(height-720*h.s)*.5f;
    Mission *m=&g->m;
    if(m->phase==MISSION_TITLE || m->phase>=MISSION_WON) {
        DrawRectangleGradientH(0,0,w,height,(Color){3,11,20,235},(Color){3,11,20,0});
        rect(&h,76,72,36,3,CYAN);
        bool tunnel=ct_get_environment(g->ct)==CT_ENV_VORTEX;
        text_at(&h,tunnel?"DATA VORTEX   /   COURIER PROTOCOL 01":"GRID CITY   /   COURIER PROTOCOL 01",76,91,19,DIM);
        bool result=m->phase>=MISSION_WON;
        text_at(&h,result?(m->phase==MISSION_WON?"DELIVERY":"SIGNAL"):"DATA",72,146,112,HUD_WHITE);
        text_at(&h,result?(m->phase==MISSION_WON?"CONFIRMED":"LOST"):"// RUN",72,243,112,result&&m->phase==MISSION_LOST?HUD_RED:AMBER);
        if(!result) {
            text_at(&h,"The network is closing. Make the uplink.",78,384,26,HUD_WHITE);
            text_at(&h,"Collect 8 amber gates. Dodge drones, sweepers and mines.",78,426,20,DIM);
            text_at(&h,"Aim through cyan laser openings. Reach the uplink in 90s.",78,456,20,DIM);
        } else {
            text_at(&h,m->phase==MISSION_WON?"Your payload made it beyond the firewall.":m->failure,78,386,25,HUD_WHITE);
            text_at(&h,TextFormat("%06d   SCORE     %02d   DATA     %.1fs   TIME",m->score,m->packets,m->elapsed),78,436,26,DIM);
        }
        float yy=504;
        bool hover=CheckCollisionPointRec(GetMousePosition(),(Rectangle){h.ox+76*h.s,h.oy+yy*h.s,332*h.s,54*h.s});
        rect(&h,76,yy,332,54,hover?HUD_WHITE:AMBER);
        text_at(&h,result?"RETRY RUN   /   ENTER":"START RUN   /   ENTER",98,yy+13,25,(Color){13,27,36,255});
        text_at(&h,"1  TUNNEL",78,575,20,tunnel?CYAN:DIM);
        text_at(&h,"2  GRID CITY",278,575,20,tunnel?DIM:CYAN);
        line(&h,tunnel?76:276,599,tunnel?231:466,599,CYAN);
        text_at(&h,"WASD / ARROWS  steer     SHIFT / SPACE  boost",78,614,20,DIM);
        text_at(&h,"Q / E  roll     ESC  pause     M  audio     F11  fullscreen",78,642,18,DIM);
        text_at(&h,tunnel?"01  /  VORTEX TRANSIT":"02  /  NIGHT TRANSIT",1045,663,16,DIM);
        return;
    }
    // Essential readouts float directly over the scene. Scoring and controls
    // live on the menu/result screens; target colors carry their meaning.
    Color payload=m->packets>=MISSION_REQUIRED?CYAN:AMBER;
    DrawPoly((Vector2){h.ox+43*h.s,h.oy+44*h.s},4,7*h.s,0,payload);
    readout(&h,TextFormat("%02d / 08",m->packets),60,28,29,payload);
    int remaining=(int)ceilf(fmaxf(90-m->elapsed-.0001f,0));
    readout(&h,TextFormat("%02d : %02d",remaining/60,remaining%60),593,28,29,m->elapsed>75?HUD_RED:HUD_WHITE);
    for(int i=0;i<3;i++)rect(&h,1148+i*31,39,22,6,i<m->shield?CYAN:(Color){44,64,78,255});
    float progress=(float)clamp((float)(ct_ship_s(g->ct)/MISSION_FINISH),0,1);
    line(&h,390,693,890,693,(Color){54,85,98,150});
    line(&h,390,693,390+500*progress,693,CYAN);
    CtStats stats;ct_get_stats(g->ct,&stats);
    readout(&h,TextFormat("%03d",(int)roundf(stats.speed)),1138,645,36,HUD_WHITE);
    readout(&h,"m/s",1210,662,15,DIM);
    if(g->boost)for(int i=0;i<3;i++) {
        float x=38+i*12;
        line(&h,x,666,x+6,660,AMBER);line(&h,x+6,660,x,654,AMBER);
    }
    int next=mission_next_packet(m);
    if(next>=0)target_marker(&h,&m->events[next],true,w,height);
    for(int i=0;i<m->eventCount;i++) {
        MissionEvent *e=&m->events[i];
        if(e->kind!=EVENT_PACKET&&!e->resolved&&e->s>=ct_ship_s(g->ct)&&e->s<ct_ship_s(g->ct)+145) {
            target_marker(&h,e,false,w,height);
            break;
        }
    }
    if(g->hitFlash>0)DrawRectangle(0,0,w,height,Fade(HUD_RED,g->hitFlash*.23f));
    if(m->phase==MISSION_COUNTDOWN) {
        text_at(&h,"UPLINK WINDOW OPENING",492,270,22,DIM);
        text_at(&h,TextFormat("%d",(int)ceilf(m->countdown)),602,316,108,AMBER);
    }
    if(g->paused) {
        DrawRectangle(0,0,w,height,(Color){3,10,18,190});
        text_at(&h,"RUN PAUSED",473,270,50,HUD_WHITE);
        text_at(&h,"ENTER / ESC resume    R restart",435,346,24,DIM);
        text_at(&h,"Your mission clock is stopped.",482,395,20,CYAN);
        text_at(&h,"BACKSPACE  return to menu",473,449,21,DIM);
    }
}

int game_run(CtTunnel *ct,bool hidden,double shotSeconds,bool autoplay,bool idleBot,bool audioCheck,bool startupCheck,const char *shotOut)
{
    Game g={.ct=ct};
    SetWindowTitle(ct_get_environment(ct)==CT_ENV_CITY?"DATA // RUN - Grid City":"DATA // RUN - Data Vortex");
    char dir[1024],fontPath[1024];
    snprintf(dir,sizeof(dir),"%s",ct_shader_directory(ct));
    sl_init(&g.shaders,dir);g.shader=sl_load(&g.shaders,"mission.vs","mission.fs");
    if(!g.shaders.e[g.shader].ok){sl_unload_all(&g.shaders);return 2;}
    g.material=LoadMaterialDefault();g.material.shader=sl_shader(&g.shaders,g.shader);
    g.ring=ring_mesh(.075f);g.glowRing=ring_mesh(.014f);g.targetRing=ring_mesh(.038f);
    g.sphere=GenMeshSphere(1,24,32);g.cube=GenMeshCube(1,1,1);
    snprintf(fontPath,sizeof(fontPath),"%s/Rajdhani-SemiBold.ttf",ct_asset_directory(ct));
    g.font=g.smallFont=GetFontDefault();
    if(FileExists(fontPath)){
        g.font=LoadFontEx(fontPath,168,NULL,0);g.smallFont=LoadFontEx(fontPath,60,NULL,0);g.ownFont=true;
        SetTextureFilter(g.font.texture,TEXTURE_FILTER_BILINEAR);SetTextureFilter(g.smallFont.texture,TEXTURE_FILTER_BILINEAR);
    }
    if(!hidden||audioCheck||startupCheck){InitAudioDevice();g.audio=IsAudioDeviceReady();}
    g.mute=audioCheck||startupCheck;
    if(g.audio){
        g.engine=synth(1,65,0);g.collect=synth(.25f,540,2);g.hit=synth(.35f,140,3);g.finish=synth(.9f,660,2);
        g.techno=techno_create(ct_get_seed(ct));g.music=LoadAudioStream(TECHNO_RATE,32,2);
        callbackMusic=g.techno;SetAudioStreamCallback(g.music,music_callback);SetAudioStreamVolume(g.music,.85f);
        techno_control(g.techno,0,false,false,g.mute);PlayAudioStream(g.music);
    }
    ct_set_scene_draw(ct,scene,&g);restart(&g,shotSeconds>0||autoplay);
    bool automated=shotSeconds>=0||autoplay;
    int frames=0,result=0,boostGaps=0;
    float minimumSpeed=1000;double maxStreamingMs=0;
    if(audioCheck) {
        bool loaded=g.audio&&g.engine.frameCount&&g.techno&&g.music.buffer&&g.collect.frameCount&&g.hit.frameCount&&g.finish.frameCount;
        printf("audio: stereo techno stream and four generated effects %s (muted verification)\n",loaded?"ready":"FAILED");
        if(!loaded)result=4;
    }
    while(!WindowShouldClose()&&!g.quit) {
        frame(&g,startupCheck&&frames==0?0.0f:automated?1/60.0f:GetFrameTime(),automated,idleBot);
        if(autoplay&&!idleBot&&g.m.phase==MISSION_RUNNING&&g.m.elapsed>2) {
            CtStats stats;ct_get_stats(ct,&stats);
            minimumSpeed=fminf(minimumSpeed,stats.speed);
            if(!g.boost)boostGaps++;
            maxStreamingMs=fmax(maxStreamingMs,stats.lastStreamFrameMs);
        }
        frames++;
        bool done=autoplay&&g.m.phase>=MISSION_WON;
        bool screenshot=shotSeconds>=0&&frames>=8&&(shotSeconds==0||g.m.elapsed>=shotSeconds||g.m.phase>=MISSION_WON);
        if(!automated||frames%4==0||done||screenshot) {
            BeginDrawing();
            if(screenshot) {
                // Offscreen captures preserve the requested resolution even when
                // Windows clips a hidden window larger than the physical monitor.
                int width=GetRenderWidth(),height=GetRenderHeight();
                RenderTexture2D capture=LoadRenderTexture(width,height);
                if(capture.id) {
                    ct_render(ct,&capture,width,height);
                    BeginTextureMode(capture);hud(&g,width,height);EndTextureMode();
                    Image img=LoadImageFromTexture(capture.texture);ImageFlipVertical(&img);
                    if(!ExportImage(img,shotOut?shotOut:"mission.png"))result=2;
                    UnloadImage(img);UnloadRenderTexture(capture);
                } else result=2;
            } else {
                ct_render(ct,NULL,GetRenderWidth(),GetRenderHeight());hud(&g,GetScreenWidth(),GetScreenHeight());
            }
            if(!automated&&IsKeyPressed(KEY_F12)) {
                rlDrawRenderBatchActive();
                Image img=LoadImageFromScreen();
                ExportImage(img,"data-run-screenshot.png");UnloadImage(img);
            }
            EndDrawing();
        }
        if(screenshot) {
            break;
        }
        if(done)break;
        if(startupCheck&&frames>=8){
            CtStats stats;ct_get_stats(ct,&stats);
            bool ready=stats.drawCalls>0&&g.m.phase==MISSION_TITLE&&isfinite(g.view.camera.position.x)&&isfinite(g.view.camera.fovy)&&g.view.camera.fovy>0;
            printf("startup: zero-time first frame and 8 interactive frames %s\n",ready?"passed":"FAILED");
            if(!ready)result=3;
            break;
        }
        if(automated&&frames>5800){result=3;break;}
    }
    if(automated) {
        printf("mission: phase=%d time=%.3f distance=%.2f data=%d shield=%d score=%d misses=%d near=%d\n",g.m.phase,g.m.elapsed,ct_ship_s(ct),g.m.packets,g.m.shield,g.m.score,g.m.misses,g.m.nearMisses);
        if(autoplay&&g.m.phase!=MISSION_WON)result=1;
        if(autoplay&&!idleBot) {
            printf("flight: minimum after acceleration %.2f m/s, boost interruptions %d, max streaming %.3f ms\n",minimumSpeed,boostGaps,maxStreamingMs);
            if(g.m.misses==0&&boostGaps>0)result=3;
        }
    }
    if(autoplay) {
        // Exercise the actual game input/update path with no pickup refills.
        restart(&g,true);g.m.phase=MISSION_RUNNING;g.m.eventCount=0;g.m.packets=MISSION_REQUIRED;
        float noPickupMinimum=1000;int noPickupGaps=0;
        for(int i=0;i<60*90&&g.m.phase==MISSION_RUNNING;i++) {
            frame(&g,1/60.0f,true,false);
            if(g.m.elapsed>2&&g.m.phase==MISSION_RUNNING){
                CtStats stats;ct_get_stats(ct,&stats);noPickupMinimum=fminf(noPickupMinimum,stats.speed);
                if(!g.boost)noPickupGaps++;
            }
        }
        bool heldCorrect=g.m.phase==MISSION_WON&&noPickupMinimum>64.7f&&noPickupGaps==0;
        printf("held boost without pickups: %.2f m/s minimum, %d interruptions (%s)\n",noPickupMinimum,noPickupGaps,heldCorrect?"passed":"FAILED");
        if(!heldCorrect)result=3;
        restart(&g,true);g.m.phase=MISSION_RUNNING;g.m.eventCount=0;
        frame(&g,.12f,true,false);
        bool timingCorrect=fabsf(g.m.elapsed-.12f)<.001f&&ct_ship_s(ct)>4;
        printf("frame timing: 120 ms frame advanced %.3f seconds and %.2f metres (%s)\n",g.m.elapsed,ct_ship_s(ct),timingCorrect?"passed":"FAILED");
        if(!timingCorrect)result=3;
        restart(&g,true);
        for(int i=0;i<245;i++)frame(&g,1/60.0f,true,false);
        g.paused=true;
        double distance=ct_ship_s(ct);float elapsed=g.m.elapsed;int score=g.m.score;
        for(int i=0;i<120;i++)frame(&g,1/60.0f,true,false);
        bool correct=distance==ct_ship_s(ct)&&elapsed==g.m.elapsed&&score==g.m.score;
        g.paused=false;frame(&g,1/60.0f,true,false);
        correct=correct&&g.m.elapsed>elapsed&&ct_ship_s(ct)>distance;
        restart(&g,false);
        correct=correct&&g.m.phase==MISSION_TITLE&&g.m.score==0&&g.m.shield==3&&ct_ship_s(ct)==0;
        printf("mission controls: pause, resume, restart %s\n",correct?"passed":"FAILED");
        if(!correct)result=3;
        int original=ct_get_environment(ct);
        bool routeCorrect=select_route(&g,original==CT_ENV_CITY?CT_ENV_VORTEX:CT_ENV_CITY);
        routeCorrect=routeCorrect&&g.m.phase==MISSION_TITLE&&ct_ship_s(ct)==0&&ct_param_get(ct,"baseSpeed")==32;
        routeCorrect=select_route(&g,original)&&routeCorrect&&ct_get_environment(ct)==original&&g.m.score==0;
        printf("mission routes: tunnel/city selection %s\n",routeCorrect?"passed":"FAILED");
        if(!routeCorrect)result=3;
    }
    ct_set_scene_draw(ct,NULL,NULL);
    if(g.audio){
        StopAudioStream(g.music);SetAudioStreamCallback(g.music,NULL);UnloadAudioStream(g.music);
        printf("music: %llu stereo frames synthesized, %.1f BPM\n",(unsigned long long)techno_frames(g.techno),techno_bpm(g.techno));
        if(audioCheck&&techno_frames(g.techno)==0)result=4;
        callbackMusic=NULL;techno_destroy(g.techno);
        UnloadSound(g.engine);UnloadSound(g.collect);UnloadSound(g.hit);UnloadSound(g.finish);CloseAudioDevice();
    }
    if(g.ownFont){UnloadFont(g.font);UnloadFont(g.smallFont);}
    lg_unload_mesh(g.ring);lg_unload_mesh(g.glowRing);lg_unload_mesh(g.targetRing);UnloadMesh(g.sphere);UnloadMesh(g.cube);
    MemFree(g.material.maps);sl_unload_all(&g.shaders);
    return result;
}
