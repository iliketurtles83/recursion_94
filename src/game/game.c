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
    uint64_t runSeed;
    int startMission;
    ShaderLib shaders;
    int shader;
    Material material;
    Mesh ring, glowRing, targetRing, sphere, cube;
    Font font, smallFont;
    bool ownFont, audio, mute, paused, boost, quit;
    Sound engine, collect, hit, finish, repair;
    AudioStream music;
    Techno *techno;
    float uiTime, hitFlash, lastX, lastY, transitionFlash, menuOpacity, routeFade;
    double missionOrigin;
    RenderTexture2D routeSnapshot;
    bool poseOverride; // screenshot/QA controls, never enabled during play
    Vector2 pose;
    MissionPhase lastPhase;
    CtSceneView view;
} Game;

static const Color HUD_WHITE={222,237,241,255}, DIM={129,164,179,255};
static const Color CYAN={67,206,232,255}, AMBER={255,187,86,255}, HUD_RED={255,88,101,255};
static const Color LASER_COLOR={178,108,255,255};
static const Color REPAIR_GREEN={82,244,126,255};
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
    s+=g->missionOrigin;
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
static bool advanced_enemy(Game *g,const MissionEvent *e,Matrix xf,bool emissive)
{
    float a=mission_event_angle(e,g->m.elapsed);
    if(e->kind==EVENT_ORBITER) {
        actor(g,emissive?g->targetRing:g->ring,xf,(Vector3){0},(Vector3){1.25f,1.25f,.8f},a,(Color){255,86,143,255},emissive?1.6f:0);
        if(!emissive){actor(g,g->sphere,xf,(Vector3){0},(Vector3){.7f,.7f,.6f},0,(Color){108,50,100,255},0);
            for(int k=0;k<3;k++)actor(g,g->cube,xf,(Vector3){cosf(a+k*2*PI/3),sinf(a+k*2*PI/3),0},(Vector3){.5f,.18f,.3f},a+k*2*PI/3,HUD_RED,0);}
        return true;
    }
    if(e->kind==EVENT_INTERCEPTOR) {
        if(!emissive){actor(g,g->sphere,xf,(Vector3){0},(Vector3){.6f,.32f,1.2f},0,(Color){120,54,66,255},0);
            for(int side=-1;side<=1;side+=2)actor(g,g->cube,xf,(Vector3){side*.95f,0,.4f},(Vector3){1.3f,.15f,.7f},side*.55f,(Color){180,70,82,255},0);}
        else for(int side=-1;side<=1;side+=2)actor(g,g->sphere,xf,(Vector3){side*.6f,0,-.7f},(Vector3){.18f,.10f,.13f},0,HUD_RED,3);
        return true;
    }
    if(e->kind==EVENT_ROTOR) {
        actor(g,emissive?g->targetRing:g->sphere,xf,(Vector3){0},(Vector3){.6f,.6f,.6f},0,HUD_RED,emissive?2:0);
        actor(g,g->cube,xf,(Vector3){0},(Vector3){13.2f,emissive?.09f:.17f,.12f},a,emissive?HUD_RED:(Color){92,41,68,255},emissive?2:0);
        if(!emissive)for(int side=-1;side<=1;side+=2)actor(g,g->sphere,xf,(Vector3){side*6.4f*cosf(a),side*6.4f*sinf(a),0},(Vector3){.27f,.27f,.27f},0,HUD_RED,0);
        return true;
    }
    if(e->kind==EVENT_PULSAR) {
        float radius=mission_event_radius(e,g->m.elapsed);
        actor(g,emissive?g->targetRing:g->ring,xf,(Vector3){0},(Vector3){radius,radius,.8f},0,(Color){255,72,153,255},emissive?2.2f:0);
        if(emissive)actor(g,g->glowRing,xf,(Vector3){0,0,.13f},(Vector3){radius+.24f,radius+.24f,.8f},0,HUD_RED,.6f);
        return true;
    }
    if(e->kind==EVENT_SPLITTER) {
        float cx,cy;mission_event_position(e,g->m.elapsed,&cx,&cy);
        for(int side=-1;side<=1;side+=2){float x,y;mission_event_split(e,g->m.elapsed,side,&x,&y);
            Vector3 offset={x-cx,y-cy,0};
            actor(g,emissive?g->targetRing:g->sphere,xf,offset,(Vector3){.8f,.8f,.65f},a,emissive?HUD_RED:(Color){128,45,76,255},emissive?2:0);
            if(!emissive)actor(g,g->cube,xf,offset,(Vector3){1.5f,.16f,.2f},a,HUD_RED,0);}
        if(emissive)actor(g,g->cube,xf,(Vector3){0},(Vector3){2*hypotf(e->amplitude*cosf(a),1.6f*sinf(a)),.025f,.025f},atan2f(1.6f*sinf(a),e->amplitude*cosf(a)),HUD_RED,.6f);
        return true;
    }
    return false;
}
static void scene(void *user,const CtSceneView *view,bool emissive)
{
    Game *g=user;
    if(!view->reflection) g->view=*view;
    if(g->m.phase==MISSION_WARP||g->m.phase==MISSION_TITLE)return;
    sl_v3(&g->shaders,g->shader,"uCamPos",view->camera.position);
    double ship=ct_ship_s(g->ct)-g->missionOrigin;
    for(int i=0;i<g->m.eventCount;i++) {
        MissionEvent *e=&g->m.events[i];
        if(e->resolved || e->s<ship-6 || e->s>ship+340) continue;
        float ex,ey;mission_event_position(e,g->m.elapsed,&ex,&ey);
        Matrix xf=basis(ct_path_sample(g->ct,e->s+g->missionOrigin),event_pos(g,view,e->s,ex,ey));
        float pulse=1+.06f*sinf(g->uiTime*3+i);
        if(mission_event_opening(e)) {
            Matrix wall=basis(ct_path_sample(g->ct,e->s+g->missionOrigin),event_pos(g,view,e->s,0,0));
            MissionEvent moving=*e;moving.x=ex;moving.y=ey;
            laser_wall(g,wall,&moving,emissive);continue;
        }
        if(advanced_enemy(g,e,xf,emissive))continue;
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
        } else if(e->kind==EVENT_REPAIR) {
            if(!emissive) {
                actor(g,g->cube,xf,(Vector3){0},(Vector3){.85f,.70f,.40f},0,(Color){36,78,52,255},0);
                actor(g,g->cube,xf,(Vector3){0,0,-.215f},(Vector3){.64f,.54f,.025f},0,(Color){16,28,23,255},0);
                for(int x=-1;x<=1;x+=2)for(int y=-1;y<=1;y+=2)
                    actor(g,g->cube,xf,(Vector3){x*.39f,y*.31f,0},(Vector3){.13f,.13f,.46f},0,(Color){154,181,164,255},0);
            }else{
                actor(g,g->cube,xf,(Vector3){0,0,-.25f},(Vector3){.43f,.115f,.028f},0,REPAIR_GREEN,1.55f);
                actor(g,g->cube,xf,(Vector3){0,0,-.25f},(Vector3){.115f,.43f,.028f},0,REPAIR_GREEN,1.55f);
                actor(g,g->targetRing,xf,(Vector3){0},(Vector3){.75f,.75f,1},0,REPAIR_GREEN,1.15f*pulse);
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
        Matrix xf=basis(ct_path_sample(g->ct,MISSION_FINISH+g->missionOrigin),event_pos(g,view,MISSION_FINISH,0,1.5f));
        Color color=g->m.packets>=g->m.required?CYAN:AMBER;
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
    bool warping=g->m.phase==MISSION_WARP;
    techno_scene(g->techno,g->m.number,warping);
    techno_control(g->techno,warping?1:g->m.phase==MISSION_RUNNING?drive:0,g->m.phase==MISSION_RUNNING||warping,g->paused,g->mute);
    if(g->mute || g->paused) {SetMasterVolume(0);return;}
    SetMasterVolume(.65f);
    if(g->m.phase==MISSION_RUNNING||g->m.phase==MISSION_TITLE||g->m.phase==MISSION_COUNTDOWN||warping) {
        if(!IsSoundPlaying(g->engine))PlaySound(g->engine);
        SetSoundPitch(g->engine,1+.55f*drive);
        SetSoundVolume(g->engine,g->m.phase==MISSION_TITLE?.035f:g->m.phase==MISSION_COUNTDOWN?.07f:.12f);
    } else StopSound(g->engine);
    if(g->m.justCollected)PlaySound(g->collect);
    if(g->m.justHit)PlaySound(g->hit);
    if(g->m.justRepaired)PlaySound(g->repair);
    if(g->m.phase==MISSION_WARP && g->lastPhase!=MISSION_WARP)PlaySound(g->finish);
}

static MissionSample ship_sample(Game *g)
{
    double s=ct_ship_s(g->ct),p[3];ct_ship_world(g->ct,p);
    CtPathSample path=ct_path_sample(g->ct,s);
    Vector3 delta={(float)(p[0]-path.pos[0]),(float)(p[1]-path.pos[1]),(float)(p[2]-path.pos[2])};
    return (MissionSample){s-g->missionOrigin,Vector3DotProduct(delta,path.binormal),Vector3DotProduct(delta,path.normal)};
}
static void begin_countdown(Game *g)
{
    // Reserve the cruise-in distance before the mission's local zero. Keep the
    // route, camera, velocity, trails and audio transport already on screen.
    CtStats stats;ct_get_stats(g->ct,&stats);
    float response=ct_param_get(g->ct,"speedResponse");
    double drift=(stats.speed-g->m.cruiseSpeed)*(1-exp(-response*MISSION_COUNTDOWN_SECONDS))/(120*expm1(response/120));
    g->missionOrigin=ct_ship_s(g->ct)+MISSION_COUNTDOWN_SECONDS*g->m.cruiseSpeed+drift;
    mission_start(&g->m);g->paused=false;
    ct_set_paused(g->ct,false);
}
static void configure_mission(Game *g)
{
    ct_preset_load(g->ct,ct_get_environment(g->ct)==CT_ENV_CITY?"mission":"mission_tunnel");
    ct_param_set(g->ct,"baseSpeed",g->m.cruiseSpeed);ct_param_set(g->ct,"boostSpeed",g->m.boostSpeed);
    float hue=mission_theme_hue(g->m.number);ct_param_set(g->ct,"themeHue",hue);
    Color core=ColorFromHSV(fmodf(188+hue*360,360),.35f,1);
    ct_param_set(g->ct,"coreR",core.r/255.0f);ct_param_set(g->ct,"coreG",core.g/255.0f);ct_param_set(g->ct,"coreB",core.b/255.0f);
    ct_reset_route(g->ct,mission_route_seed(&g->m));ct_set_paused(g->ct,false);ct_set_input(g->ct,&(CtInput){0});
    g->missionOrigin=0;
    ct_set_whiteout(g->ct,0);g->lastX=g->lastY=0;g->boost=false;
    if(g->techno)techno_scene(g->techno,g->m.number,false);
}
static void restart(Game *g,bool start)
{
    mission_reset_at(&g->m,g->runSeed,g->startMission);configure_mission(g);
    g->paused=false;g->boost=false;g->lastX=g->lastY=0;g->hitFlash=0;g->transitionFlash=0;
    g->menuOpacity=start?0:1;
    g->routeFade=0;
    if(start)begin_countdown(g);
}
static void return_to_menu(Game *g)
{
    mission_reset_at(&g->m,g->runSeed,g->startMission);
    ct_param_set(g->ct,"baseSpeed",g->m.cruiseSpeed);ct_param_set(g->ct,"boostSpeed",g->m.boostSpeed);
    ct_param_set(g->ct,"speedResponse",2.5f);ct_param_set(g->ct,"fovBoost",3);
    ct_param_set(g->ct,"zoomBlurOn",0);ct_param_set(g->ct,"coreIntensity",2.6f);
    g->paused=false;g->boost=false;g->menuOpacity=1;g->hitFlash=g->transitionFlash=0;
    ct_set_whiteout(g->ct,0);ct_set_paused(g->ct,false);
}
static void warp_parameters(Game *g)
{
    float t=clamp(1-g->m.warpRemaining/MISSION_WARP_SECONDS,0,1),surge=sinf(PI*t);
    ct_param_set(g->ct,"boostSpeed",fminf(g->m.boostSpeed*1.5f+290*surge,400));
    ct_param_set(g->ct,"speedResponse",8);ct_param_set(g->ct,"fovBoost",22);
    ct_param_set(g->ct,"zoomBlurOn",1);ct_param_set(g->ct,"zoomBlur",.10f+.14f*surge);
    ct_param_set(g->ct,"coreIntensity",2.6f+3*surge);
    ct_set_whiteout(g->ct,.32f*powf(t,8));
}
static bool select_route(Game *g,int environment)
{
    if(ct_get_environment(g->ct)==environment)return true;
    // Blend the old scenery over the new preview rather than cutting to it.
    int w=GetRenderWidth(),h=GetRenderHeight();
    if(!g->routeSnapshot.id||g->routeSnapshot.texture.width!=w||g->routeSnapshot.texture.height!=h) {
        if(g->routeSnapshot.id)UnloadRenderTexture(g->routeSnapshot);
        g->routeSnapshot=LoadRenderTexture(w,h);
    }
    if(g->routeSnapshot.id){ct_render(g->ct,&g->routeSnapshot,w,h);g->routeFade=1;}
    ct_set_environment(g->ct,environment);
    bool loaded=ct_preset_load(g->ct,environment==CT_ENV_CITY?"mission":"mission_tunnel");
    ct_param_set(g->ct,"baseSpeed",g->m.cruiseSpeed);ct_param_set(g->ct,"boostSpeed",g->m.boostSpeed);
    // Rebuild with the selected path/preset at the current journey distance.
    ct_rebuild(g->ct);g->lastX=g->lastY=0;ct_set_paused(g->ct,false);
    SetWindowTitle(environment==CT_ENV_CITY?"DATA // RUN - Grid City":"DATA // RUN - Data Vortex");
    return loaded;
}
static CtInput preview_input(Game *g,MissionSample p,float dt)
{
    float vx=(p.x-g->lastX)/fmaxf(dt,.001f),vy=(p.y-g->lastY)/fmaxf(dt,.001f);
    float x=.9f*sinf(g->uiTime*.23f),y=.7f+.25f*sinf(g->uiTime*.31f);
    return (CtInput){.steerX=clamp((x-p.x)*.8f-vx*.1f,-1,1),.steerY=clamp((y-p.y)*.8f-vy*.1f,-1,1)};
}
static CtInput bot(Game *g,MissionSample p,float dt)
{
    CtInput in={0};float x=0,y=.7f;
    int next=mission_next_packet(&g->m);
    if(next>=0) {x=g->m.events[next].x;y=g->m.events[next].y;}
    CtStats stats;ct_get_stats(g->ct,&stats);
    for(int i=0;i<g->m.eventCount;i++) {
        MissionEvent *e=&g->m.events[i];
        if(e->resolved||mission_event_pickup(e)||e->s<=p.s||e->s-p.s>72)continue;
        float time=g->m.elapsed+(float)(e->s-p.s)/fmaxf(stats.speed,32);
        if(mission_event_opening(e)){mission_event_position(e,time,&x,&y);break;}
        if(!mission_event_danger(e,time,x,y,1.1f))continue;
        float best=1e9f,bx=x,by=y;
        const float candidates[]={-4.4f,-3.4f,-1.7f,0,1.7f,3.4f,4.4f};
        for(int ix=0;ix<7;ix++)for(int iy=0;iy<5;iy++) {
            float tx=candidates[ix],ty=-2.8f+iy*1.3f;
            float cost=hypotf(tx-x,ty-y)+.15f*hypotf(tx-p.x,ty-p.y);
            for(int j=i;j<g->m.eventCount;j++) {
                MissionEvent *hazard=&g->m.events[j];
                if(hazard->resolved||mission_event_pickup(hazard)||hazard->s>e->s+8)continue;
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
        if(g->m.phase==MISSION_TITLE||g->m.phase==MISSION_LOST) {
            float routeScale=fminf(GetScreenWidth()/1280.0f,GetScreenHeight()/720.0f);
            Vector2 mouse=GetMousePosition();
            float mx=(mouse.x-(GetScreenWidth()-1280*routeScale)*.5f)/routeScale;
            float my=(mouse.y-(GetScreenHeight()-720*routeScale)*.5f)/routeScale;
            bool click=IsMouseButtonPressed(MOUSE_BUTTON_LEFT)&&my>=572&&my<=596;
            if(g->m.phase==MISSION_TITLE&&IsKeyPressed(KEY_TAB))select_route(g,ct_get_environment(g->ct)==CT_ENV_CITY?CT_ENV_VORTEX:CT_ENV_CITY);
            int selected=IsKeyPressed(KEY_ONE)||(click&&mx>=76&&mx<=254)?CT_ENV_VORTEX:IsKeyPressed(KEY_TWO)||(click&&mx>=276&&mx<=486)?CT_ENV_CITY:-1;
            if(selected>=0){if(g->m.phase==MISSION_LOST)return_to_menu(g);select_route(g,selected);begin_countdown(g);}
        }
        if(IsKeyPressed(KEY_M))g->mute=!g->mute;
        if(IsKeyPressed(KEY_F11))ToggleFullscreen();
        if(IsKeyPressed(KEY_R))restart(g,true);
        if(IsKeyPressed(KEY_BACKSPACE))return_to_menu(g);
        if(IsKeyPressed(KEY_ESCAPE)) {
            if(g->m.phase==MISSION_RUNNING||g->m.phase==MISSION_COUNTDOWN||g->m.phase==MISSION_WARP)g->paused=!g->paused;
            else if(g->m.phase==MISSION_TITLE)g->quit=true;
            else return_to_menu(g);
        }
        if(IsKeyPressed(KEY_ENTER)) {
            if(g->paused)g->paused=false;
            else if(g->m.phase==MISSION_TITLE)begin_countdown(g);
            else if(g->m.phase==MISSION_LOST)restart(g,true);
        }
        float scale=fminf(GetScreenWidth()/1280.0f,GetScreenHeight()/720.0f);
        Rectangle button={(GetScreenWidth()-1280*scale)*.5f+76*scale,(GetScreenHeight()-720*scale)*.5f+504*scale,332*scale,54*scale};
        if((g->m.phase==MISSION_TITLE||g->m.phase==MISSION_LOST)&&IsMouseButtonPressed(MOUSE_BUTTON_LEFT)&&CheckCollisionPointRec(GetMousePosition(),button)){
            if(g->m.phase==MISSION_TITLE)begin_countdown(g);else restart(g,true);
        }
        if(!IsWindowFocused()&&(g->m.phase==MISSION_RUNNING||g->m.phase==MISSION_COUNTDOWN||g->m.phase==MISSION_WARP))g->paused=true;
    }
    bool flying=g->m.phase==MISSION_RUNNING&&!g->paused;
    bool preview=g->m.phase==MISSION_TITLE||g->m.phase==MISSION_COUNTDOWN;
    ct_set_paused(g->ct,g->paused||(!flying&&!preview&&g->m.phase!=MISSION_WARP));
    MissionSample p=ship_sample(g);
    CtInput in={0};
    if(preview&&!g->paused)in=preview_input(g,p,dt);
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
    if(g->poseOverride&&automated&&!g->paused){in.steerX=g->pose.x;in.steerY=g->pose.y;}
    g->lastX=p.x;g->lastY=p.y;g->boost=in.boost;
    // Consume the whole rendered frame in safe slices. The previous 50 ms cap
    // made a 120 ms frame advance only 50 ms of flight: visible slow motion.
    int collected=0,hit=0,near=0,repaired=0;
    if(dt<=1e-6f)ct_update(g->ct,0.0f);
    for(float remaining=dt;remaining>1e-6f;) {
        float slice=fminf(remaining,1/60.0f);remaining-=slice;
        ct_set_paused(g->ct,g->paused||g->m.phase==MISSION_LOST);
        CtInput stepInput=in;
        if(g->m.phase==MISSION_WARP){
            warp_parameters(g);MissionSample center=ship_sample(g);
            stepInput=(CtInput){.boost=true,.steerX=clamp(-center.x*.8f,-1,1),.steerY=clamp(-center.y*.8f,-1,1)};
        }
        ct_set_input(g->ct,&stepInput);in.rollLeft=in.rollRight=false;
        ct_update(g->ct,slice);
        if(!g->paused){
            int number=g->m.number;
            mission_tick(&g->m,slice,ship_sample(g),in.boost);
            if(g->m.number!=number){configure_mission(g);begin_countdown(g);g->transitionFlash=.35f;}
            collected+=g->m.justCollected;hit+=g->m.justHit;near+=g->m.justNear;
            repaired+=g->m.justRepaired;
        }
    }
    g->m.justCollected=collected;g->m.justHit=hit;g->m.justNear=near;
    g->m.justRepaired=repaired;
    ct_set_ship_damage(g->ct,clamp((3-g->m.shield)*.5f,0,1));
    if(!g->paused){
        g->transitionFlash=fmaxf(g->transitionFlash-dt,0);
        if(g->m.phase!=MISSION_TITLE)g->menuOpacity=fmaxf(g->menuOpacity-dt/.75f,0);
        g->routeFade=fmaxf(g->routeFade-dt/.55f,0);
    }
    if(g->m.phase!=MISSION_WARP)ct_set_whiteout(g->ct,g->transitionFlash);
    g->hitFlash=fmaxf(g->hitFlash-dt,0);
    if(hit)g->hitFlash=.4f;
    audio_frame(g);g->lastPhase=g->m.phase;
}

typedef struct Hud {Game *g;float s,ox,oy,w,h,opacity;} Hud;
static void text_at(Hud *h,const char *t,float x,float y,float size,Color c)
{
    DrawTextEx(size<40?h->g->smallFont:h->g->font,t,(Vector2){h->ox+x*h->s,h->oy+y*h->s},size*h->s,1*h->s,Fade(c,h->opacity));
}
static void rect(Hud *h,float x,float y,float w,float height,Color c)
{
    DrawRectangleRec((Rectangle){h->ox+x*h->s,h->oy+y*h->s,w*h->s,height*h->s},Fade(c,h->opacity));
}
static void line(Hud *h,float x,float y,float x2,float y2,Color c)
{
    DrawLineEx((Vector2){h->ox+x*h->s,h->oy+y*h->s},(Vector2){h->ox+x2*h->s,h->oy+y2*h->s},h->s,Fade(c,h->opacity));
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
    CtPathSample path=ct_path_sample(g->ct,e->s+g->missionOrigin);
    float markerRadius=e->kind==EVENT_ROTOR?2:e->kind==EVENT_SPLITTER?e->amplitude:mission_event_radius(e,g->m.elapsed);
    Vector3 edge=Vector3Add(pos,Vector3Scale(path.binormal,markerRadius));
    float r=clamp(Vector2Distance(p,GetWorldToScreenEx(edge,g->view.camera,width,height)),12*h->s,112*h->s)+6*h->s;
    Color color=e->kind==EVENT_REPAIR?REPAIR_GREEN:packet?AMBER:mission_event_opening(e)?CYAN:HUD_RED;
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
static void route_blend(Game *g,int width,int height)
{
    if(g->routeFade<=0||!g->routeSnapshot.id)return;
    float alpha=g->routeFade*g->routeFade*(3-2*g->routeFade);
    DrawTexturePro(g->routeSnapshot.texture,(Rectangle){0,0,g->routeSnapshot.texture.width,-g->routeSnapshot.texture.height},(Rectangle){0,0,width,height},(Vector2){0},0,Fade(WHITE,alpha));
}
static void hud(Game *g,int w,int height)
{
    Hud h={g,fminf(w/1280.0f,height/720.0f),0,0,1280,720,1};
    h.ox=(w-1280*h.s)*.5f;h.oy=(height-720*h.s)*.5f;
    Mission *m=&g->m;
    if(m->phase==MISSION_TITLE||m->phase==MISSION_LOST||g->menuOpacity>0) {
        h.opacity=m->phase==MISSION_LOST?1:g->menuOpacity;
        DrawRectangleGradientH(0,0,w,height,Fade((Color){3,11,20,235},h.opacity),(Color){3,11,20,0});
        rect(&h,76,72,36,3,CYAN);
        bool tunnel=ct_get_environment(g->ct)==CT_ENV_VORTEX;
        text_at(&h,tunnel?"DATA VORTEX   /   COURIER PROTOCOL 01":"GRID CITY   /   COURIER PROTOCOL 01",76,91,19,DIM);
        bool result=m->phase==MISSION_LOST;
        text_at(&h,result?"SIGNAL":"DATA",72,146,112,HUD_WHITE);
        text_at(&h,result?"LOST":"// RUN",72,243,112,result&&m->phase==MISSION_LOST?HUD_RED:AMBER);
        if(!result) {
            text_at(&h,"One delivery is only the beginning.",78,384,26,HUD_WHITE);
            text_at(&h,"Collect amber data. Survive the security waves.",78,426,20,DIM);
            text_at(&h,"Warp into the next mission. Ten enemy types unlock.",78,456,20,DIM);
        } else {
            text_at(&h,m->failure,78,386,25,HUD_WHITE);
            text_at(&h,TextFormat("%08lld   SCORE     %02d   MISSIONS CLEARED",(long long)m->score,m->completed),78,436,26,DIM);
        }
        float yy=504;
        bool hover=CheckCollisionPointRec(GetMousePosition(),(Rectangle){h.ox+76*h.s,h.oy+yy*h.s,332*h.s,54*h.s});
        rect(&h,76,yy,332,54,hover?HUD_WHITE:AMBER);
        text_at(&h,result?"RETRY RUN   /   ENTER":"START RUN   /   ENTER",98,yy+13,25,(Color){13,27,36,255});
        text_at(&h,"1  TUNNEL",78,575,20,tunnel?CYAN:DIM);
        text_at(&h,"2  GRID CITY",278,575,20,tunnel?DIM:CYAN);
        line(&h,tunnel?76:276,599,tunnel?231:466,599,CYAN);
        text_at(&h,"WASD / ARROWS  steer     SHIFT / SPACE  boost",78,614,20,DIM);
        text_at(&h,"TAB  preview route     Q / E  roll     ESC  pause     M  audio",78,642,18,DIM);
        text_at(&h,tunnel?"01  /  VORTEX TRANSIT":"02  /  NIGHT TRANSIT",1045,663,16,DIM);
        if(m->phase==MISSION_TITLE||m->phase==MISSION_LOST)return;
        h.opacity=1-g->menuOpacity;
    }
    if(m->phase==MISSION_WARP) {
        readout(&h,"UPLINK ACCEPTED",512,220,30,CYAN);
        readout(&h,"// WARP",481,280,80,HUD_WHITE);
        readout(&h,TextFormat("MISSION %02d INBOUND",m->number+1),507,384,26,AMBER);
        if(g->paused){DrawRectangle(0,0,w,height,(Color){3,10,18,190});readout(&h,"RUN PAUSED",473,270,50,HUD_WHITE);}
        return;
    }
    // Essential readouts float directly over the scene. Scoring and controls
    // live on the menu/result screens; target colors carry their meaning.
    Color payload=m->packets>=m->required?CYAN:AMBER;
    DrawPoly((Vector2){h.ox+43*h.s,h.oy+44*h.s},4,7*h.s,0,Fade(payload,h.opacity));
    readout(&h,TextFormat("%02d / %02d",m->packets,m->required),60,28,29,payload);
    readout(&h,TextFormat("M%02d",m->number),220,32,23,DIM);
    int remaining=(int)ceilf(fmaxf(m->duration-m->elapsed-.0001f,0));
    readout(&h,TextFormat("%02d : %02d",remaining/60,remaining%60),593,28,29,m->duration-m->elapsed<15?HUD_RED:HUD_WHITE);
    for(int i=0;i<3;i++)rect(&h,1148+i*31,39,22,6,i<m->shield?CYAN:(Color){44,64,78,255});
    float progress=(float)clamp((float)((ct_ship_s(g->ct)-g->missionOrigin)/MISSION_FINISH),0,1);
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
        double ship=ct_ship_s(g->ct)-g->missionOrigin;
        if(e->kind==EVENT_REPAIR&&!e->resolved&&e->s>=ship&&e->s<ship+200&&m->shield<3)
            target_marker(&h,e,false,w,height);
    }
    for(int i=0;i<m->eventCount;i++) {
        MissionEvent *e=&m->events[i];
        double ship=ct_ship_s(g->ct)-g->missionOrigin;
        if(!mission_event_pickup(e)&&!e->resolved&&e->s>=ship&&e->s<ship+145) {
            target_marker(&h,e,false,w,height);
            break;
        }
    }
    if(g->hitFlash>0)DrawRectangle(0,0,w,height,Fade(HUD_RED,g->hitFlash*.23f));
    if(m->phase==MISSION_COUNTDOWN) {
        readout(&h,TextFormat("MISSION %02d / %02d ENEMY TYPES",m->number,m->enemyKinds),452,245,27,CYAN);
        readout(&h,TextFormat("%d",(int)ceilf(m->countdown)),602,298,108,AMBER);
        if(m->number<=5)readout(&h,TextFormat("NEW: %s + %s",mission_enemy_name((EventKind)(m->enemyKinds-1)),mission_enemy_name((EventKind)m->enemyKinds)),407,434,23,DIM);
        else readout(&h,TextFormat("%.2fx SECURITY / %s",m->difficulty,m->number>20?"VERY HARD":m->number>10?"HARD":"ESCALATING"),445,434,23,m->number>20?HUD_RED:DIM);
    }
    if(g->paused) {
        DrawRectangle(0,0,w,height,(Color){3,10,18,190});
        text_at(&h,"RUN PAUSED",473,270,50,HUD_WHITE);
        text_at(&h,"ENTER / ESC resume    R restart",435,346,24,DIM);
        text_at(&h,"Your mission clock is stopped.",482,395,20,CYAN);
        text_at(&h,"BACKSPACE  return to menu",473,449,21,DIM);
    }
}

static bool check_live_menu(Game *g)
{
    bool valid=true;int original=ct_get_environment(g->ct);
    for(int route=0;route<2;route++) {
        return_to_menu(g);double before=ct_ship_s(g->ct);
        valid=select_route(g,route?CT_ENV_CITY:CT_ENV_VORTEX)&&valid;
        valid=valid&&fabs(ct_ship_s(g->ct)-before)<1;
        for(int i=0;i<60*55;i++) {
            frame(g,1/60.0f,true,false);
            if(i<40&&i%4==0){BeginDrawing();ct_render(g->ct,NULL,GetRenderWidth(),GetRenderHeight());route_blend(g,GetScreenWidth(),GetScreenHeight());hud(g,GetScreenWidth(),GetScreenHeight());EndDrawing();}
        }
        before=ct_ship_s(g->ct);
        valid=valid&&before>1000&&g->m.phase==MISSION_TITLE&&g->m.elapsed==0&&g->m.score==0&&g->m.shield==3&&g->routeFade==0;
        double world[3],after[3];ct_ship_world(g->ct,world);
        BeginDrawing();ct_render(g->ct,NULL,GetRenderWidth(),GetRenderHeight());EndDrawing();
        Camera3D camera=g->view.camera;uint64_t seed=ct_get_seed(g->ct);CtStats a,b;ct_get_stats(g->ct,&a);
        begin_countdown(g);ct_ship_world(g->ct,after);ct_get_stats(g->ct,&b);
        BeginDrawing();ct_render(g->ct,NULL,GetRenderWidth(),GetRenderHeight());EndDrawing();
        valid=valid&&before==ct_ship_s(g->ct)&&seed==ct_get_seed(g->ct)&&a.speed==b.speed;
        for(int j=0;j<3;j++)valid=valid&&world[j]==after[j];
        valid=valid&&Vector3Distance(camera.position,g->view.camera.position)<.0001f&&Vector3Distance(camera.target,g->view.camera.target)<.0001f;
        // Pausing the moving countdown must still freeze it exactly.
        frame(g,.1f,true,false);g->paused=true;
        double pausedDistance=ct_ship_s(g->ct);float countdown=g->m.countdown,opacity=g->menuOpacity;
        for(int i=0;i<60;i++)frame(g,1/60.0f,true,false);
        valid=valid&&pausedDistance==ct_ship_s(g->ct)&&countdown==g->m.countdown&&opacity==g->menuOpacity;
        g->paused=false;int frames=0;float minSpeed=1000;
        while(g->m.phase==MISSION_COUNTDOWN&&frames<301) {
            double s=ct_ship_s(g->ct);frame(g,1/60.0f,true,false);ct_get_stats(g->ct,&b);frames++;
            minSpeed=fminf(minSpeed,b.speed);
            valid=valid&&ct_ship_s(g->ct)>s&&g->m.elapsed==0&&g->m.shield==3&&g->m.packets==0&&g->m.misses==0&&g->m.score==0;
        }
        valid=valid&&frames>=293&&frames<=295&&g->m.phase==MISSION_RUNNING&&g->menuOpacity==0&&fabs(ship_sample(g).s)<1;
        frame(g,1/60.0f,true,false);
        valid=valid&&g->m.elapsed>0&&minSpeed>=g->m.cruiseSpeed*.999f;
        double travelled=ct_ship_s(g->ct)-before;
        printf("live menu %s: preserved camera/ship/seed, moving countdown %.1f m at %.1f m/s, protected clock and pause %s\n",route?"city":"tunnel",travelled,minSpeed,valid?"passed":"FAILED");
        // Escape back to the menu must continue this journey, including at boost.
        before=ct_ship_s(g->ct);return_to_menu(g);
        valid=valid&&before==ct_ship_s(g->ct)&&g->m.phase==MISSION_TITLE;
        begin_countdown(g);
        for(int i=0;i<301;i++)frame(g,1/60.0f,true,false);
        valid=valid&&g->m.phase==MISSION_RUNNING&&fabs(ship_sample(g).s)<1;
    }
    return_to_menu(g);valid=select_route(g,original)&&valid;
    printf("live menu: long preview, both routes, fade, pause, cancel and uninterrupted handoff %s\n",valid?"passed":"FAILED");
    return valid;
}

static bool ship_capture(Game *g,const char *prefix,const char *state)
{
    int w=GetRenderWidth(),h=GetRenderHeight();RenderTexture2D rt=LoadRenderTexture(w,h);
    if(!rt.id)return false;
    BeginDrawing();ct_render(g->ct,&rt,w,h);EndDrawing();
    bool ok=true;
    if(prefix){Image img=LoadImageFromTexture(rt.texture);ImageFlipVertical(&img);ok=ExportImage(img,TextFormat("%s-%s.png",prefix,state));UnloadImage(img);}
    UnloadRenderTexture(rt);return ok;
}
static bool check_ship_systems(Game *g,const char *prefix)
{
    restart(g,true);g->m.phase=MISSION_RUNNING;g->m.countdown=0;g->missionOrigin=ct_ship_s(g->ct);
    g->m.shield=1;g->m.eventCount=1;g->m.packets=0;g->menuOpacity=0;
    MissionSample p=ship_sample(g);
    g->m.events[0]=(MissionEvent){.s=p.s+45,.x=p.x,.y=p.y,.radius=1.35f,.kind=EVENT_REPAIR};
    frame(g,0,true,false);bool valid=ship_capture(g,prefix,"critical");
    g->paused=true;for(int i=0;i<60;i++)frame(g,1/60.0f,true,false);
    valid=valid&&g->m.shield==1&&!g->m.events[0].resolved;
    g->paused=false;int repairs=0,gaps=0;
    for(int i=0;i<90&&g->m.shield<2;i++){frame(g,1/60.0f,true,false);repairs+=g->m.justRepaired;gaps+=!g->boost;}
    valid=valid&&g->m.shield==2&&repairs==1&&gaps==0&&g->m.packets==0;
    valid=ship_capture(g,prefix,"repaired")&&valid;
    p=ship_sample(g);g->m.events[0]=(MissionEvent){.s=p.s+30,.x=p.x,.y=p.y,.radius=1.35f,.kind=EVENT_REPAIR};
    for(int i=0;i<60&&g->m.shield<3;i++){frame(g,1/60.0f,true,false);repairs+=g->m.justRepaired;gaps+=!g->boost;}
    valid=valid&&g->m.shield==3&&repairs==2&&gaps==0&&g->m.phase==MISSION_RUNNING&&g->m.score==0;
    valid=ship_capture(g,prefix,"intact")&&valid;
    g->m.eventCount=0;g->poseOverride=true;
    const Vector2 directions[]={{-1,0},{1,0},{0,1},{0,-1}};
    const char *names[]={"left","right","up","down"};
    for(int d=0;d<4;d++){
        g->pose=directions[d];
        for(int i=0;i<60;i++)frame(g,1/60.0f,true,false);
        valid=ship_capture(g,prefix,names[d])&&valid;
    }
    g->poseOverride=false;
    printf("ship systems: four steering poses rendered, paused kit protected, two repairs, data/score unchanged, %d boost gaps: %s\n",gaps,valid?"PASS":"FAIL");
    return valid;
}
int game_run(CtTunnel *ct,const GameOptions *options)
{
    bool hidden=options->hidden,autoplay=options->autoplay,idleBot=options->idleBot,audioCheck=options->audioCheck,startupCheck=options->startupCheck;
    double shotSeconds=options->shotSeconds,transitionShot=options->transitionShot;
    const char *shotOut=options->shotOut;int missionLimit=options->missions>0?options->missions:1;
    Game g={.ct=ct,.runSeed=ct_get_seed(ct),.startMission=options->startMission>0?options->startMission:1};
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
        g.repair=synth(.42f,820,2);
        g.techno=techno_create(ct_get_seed(ct));g.music=LoadAudioStream(TECHNO_RATE,32,2);
        callbackMusic=g.techno;SetAudioStreamCallback(g.music,music_callback);SetAudioStreamVolume(g.music,.85f);
        techno_control(g.techno,0,false,false,g.mute);PlayAudioStream(g.music);
    }
    ct_set_scene_draw(ct,scene,&g);restart(&g,shotSeconds>0||autoplay||transitionShot>=0);
    if(shotSeconds>=0){g.poseOverride=options->shotSteer;g.pose=(Vector2){options->shotSteerX,options->shotSteerY};if(options->shotShield>=1&&options->shotShield<=3)g.m.shield=options->shotShield;}
    bool automated=shotSeconds>=0||autoplay||transitionShot>=0||options->menuShot>=0||options->introShot>=0||options->menuCheck||options->shipCheck;
    int frames=0,result=0,boostGaps=0,cleared=0;double transitionTime=-1,introTime=-1;
    float minimumSpeed=1000;double maxStreamingMs=0;
    if(audioCheck) {
        bool loaded=g.audio&&g.engine.frameCount&&g.techno&&g.music.buffer&&g.collect.frameCount&&g.hit.frameCount&&g.finish.frameCount&&g.repair.frameCount;
        printf("audio: stereo techno stream and five generated effects %s (muted verification)\n",loaded?"ready":"FAILED");
        if(!loaded)result=4;
    }
    while(!WindowShouldClose()&&!g.quit) {
        if(options->introShot>=0&&frames==240){begin_countdown(&g);introTime=0;}
        if(introTime>=0)introTime+=1/60.0;
        frame(&g,startupCheck&&frames==0?0.0f:automated?1/60.0f:GetFrameTime(),automated,idleBot);
        if(transitionTime>=0)transitionTime+=1/60.0;
        if(g.m.completed>cleared){
            cleared=g.m.completed;if(transitionTime<0)transitionTime=0;
            printf("mission %d cleared: %d enemy kinds, shield %d, total score %lld\n",g.m.number,g.m.enemyKinds,g.m.shield,(long long)g.m.score);
        }
        if(autoplay&&!idleBot&&g.m.phase==MISSION_RUNNING&&g.m.elapsed>2) {
            CtStats stats;ct_get_stats(ct,&stats);
            minimumSpeed=fminf(minimumSpeed,stats.speed);
            if(!g.boost)boostGaps++;
            maxStreamingMs=fmax(maxStreamingMs,stats.lastStreamFrameMs);
        }
        frames++;
        bool done=autoplay&&(g.m.phase==MISSION_LOST||g.m.completed>=missionLimit);
        bool screenshot=frames>=8&&((shotSeconds>=0&&(shotSeconds==0||g.m.elapsed>=shotSeconds||g.m.phase==MISSION_LOST||g.m.completed>0))||(transitionShot>=0&&(transitionTime>=transitionShot||g.m.phase==MISSION_LOST))||(options->menuShot>=0&&frames/60.0>=options->menuShot)||(options->introShot>=0&&introTime>=options->introShot));
        if(!automated||frames%4==0||done||screenshot) {
            BeginDrawing();
            if(screenshot) {
                // Offscreen captures preserve the requested resolution even when
                // Windows clips a hidden window larger than the physical monitor.
                int width=GetRenderWidth(),height=GetRenderHeight();
                RenderTexture2D capture=LoadRenderTexture(width,height);
                if(capture.id) {
                    ct_render(ct,&capture,width,height);
                    BeginTextureMode(capture);route_blend(&g,width,height);hud(&g,width,height);EndTextureMode();
                    Image img=LoadImageFromTexture(capture.texture);ImageFlipVertical(&img);
                    if(!ExportImage(img,shotOut?shotOut:"mission.png"))result=2;
                    UnloadImage(img);UnloadRenderTexture(capture);
                } else result=2;
            } else {
                ct_render(ct,NULL,GetRenderWidth(),GetRenderHeight());route_blend(&g,GetScreenWidth(),GetScreenHeight());hud(&g,GetScreenWidth(),GetScreenHeight());
            }
            if(!automated&&IsKeyPressed(KEY_F12)) {
                rlDrawRenderBatchActive();
                Image img=LoadImageFromScreen();
                ExportImage(img,"data-run-screenshot.png");UnloadImage(img);
            }
            EndDrawing();
        }
        if(screenshot) {
            if(transitionShot>=0&&g.m.phase==MISSION_LOST)result=1;
            break;
        }
        if(done)break;
        if((options->menuCheck||options->shipCheck)&&frames>=120)break;
        if(startupCheck&&frames>=8){
            CtStats stats;ct_get_stats(ct,&stats);
            bool ready=stats.drawCalls>0&&g.m.phase==MISSION_TITLE&&isfinite(g.view.camera.position.x)&&isfinite(g.view.camera.fovy)&&g.view.camera.fovy>0;
            printf("startup: zero-time first frame and 8 interactive frames %s\n",ready?"passed":"FAILED");
            if(!ready)result=3;
            break;
        }
        if(automated&&frames>(missionLimit+1)*60*110){result=3;break;}
    }
    if(options->menuCheck&&!check_live_menu(&g))result=3;
    if(options->shipCheck&&!check_ship_systems(&g,shotOut))result=3;
    if(automated) {
        printf("mission: number=%d cleared=%d phase=%d time=%.3f distance=%.2f data=%d shield=%d score=%lld misses=%d near=%d\n",g.m.number,g.m.completed,g.m.phase,g.m.elapsed,ct_ship_s(ct),g.m.packets,g.m.shield,(long long)g.m.score,g.m.misses,g.m.nearMisses);
        if(autoplay&&g.m.completed<missionLimit)result=1;
        if(autoplay&&!idleBot) {
            printf("flight: minimum after acceleration %.2f m/s, boost interruptions %d, max streaming %.3f ms\n",minimumSpeed,boostGaps,maxStreamingMs);
            if(g.m.misses==0&&boostGaps>0)result=3;
        }
    }
    if(autoplay) {
        // Exercise the actual game input/update path with no pickup refills.
        restart(&g,true);g.m.phase=MISSION_RUNNING;g.m.eventCount=0;g.m.packets=g.m.required;
        float noPickupMinimum=1000;int noPickupGaps=0;
        for(int i=0;i<60*90&&g.m.phase==MISSION_RUNNING;i++) {
            frame(&g,1/60.0f,true,false);
            if(g.m.elapsed>2&&g.m.phase==MISSION_RUNNING){
                CtStats stats;ct_get_stats(ct,&stats);noPickupMinimum=fminf(noPickupMinimum,stats.speed);
                if(!g.boost)noPickupGaps++;
            }
        }
        bool heldCorrect=g.m.phase==MISSION_WARP&&noPickupMinimum>64.7f&&noPickupGaps==0;
        printf("held boost without pickups: %.2f m/s minimum, %d interruptions (%s)\n",noPickupMinimum,noPickupGaps,heldCorrect?"passed":"FAILED");
        if(!heldCorrect)result=3;
        restart(&g,true);g.m.phase=MISSION_RUNNING;g.m.eventCount=0;
        frame(&g,.12f,true,false);
        bool timingCorrect=fabsf(g.m.elapsed-.12f)<.001f&&ct_ship_s(ct)>4;
        printf("frame timing: 120 ms frame advanced %.3f seconds and %.2f metres (%s)\n",g.m.elapsed,ct_ship_s(ct),timingCorrect?"passed":"FAILED");
        if(!timingCorrect)result=3;
        restart(&g,true);
        for(int i=0;i<325;i++)frame(&g,1/60.0f,true,false);
        g.paused=true;
        double distance=ct_ship_s(ct);float elapsed=g.m.elapsed;int64_t score=g.m.score;
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
        routeCorrect=routeCorrect&&g.m.phase==MISSION_TITLE&&ct_ship_s(ct)==0&&ct_param_get(ct,"baseSpeed")==g.m.cruiseSpeed;
        routeCorrect=select_route(&g,original)&&routeCorrect&&ct_get_environment(ct)==original&&g.m.score==0;
        printf("mission routes: tunnel/city selection %s\n",routeCorrect?"passed":"FAILED");
        if(!routeCorrect)result=3;
        // Pause through both parts of an actual engine transition. The route,
        // mission clock and cumulative score must survive the new mission.
        restart(&g,true);g.m.phase=MISSION_WARP;g.m.completed=1;
        g.m.warpRemaining=MISSION_WARP_SECONDS;g.m.score=5000000000ll;
        int nextMission=g.m.number+1;
        frame(&g,1/60.0f,true,false);g.paused=true;
        distance=ct_ship_s(ct);float warpRemaining=g.m.warpRemaining;
        for(int i=0;i<120;i++)frame(&g,1/60.0f,true,false);
        bool transitionCorrect=g.m.phase==MISSION_WARP&&g.m.warpRemaining==warpRemaining&&ct_ship_s(ct)==distance;
        g.paused=false;
        for(int i=0;i<180&&g.m.phase==MISSION_WARP;i++)frame(&g,1/60.0f,true,false);
        transitionCorrect=transitionCorrect&&g.m.phase==MISSION_COUNTDOWN&&g.m.number==nextMission&&g.m.countdown==5&&ct_ship_s(ct)==0;
        transitionCorrect=transitionCorrect&&g.m.score==5000000000ll;
        if(ct_get_environment(ct)==CT_ENV_VORTEX)
            transitionCorrect=transitionCorrect&&fabsf(ct_param_get(ct,"themeHue")-mission_theme_hue(nextMission))<.0001f;
        g.paused=true;
        for(int i=0;i<120;i++)frame(&g,1/60.0f,true,false);
        transitionCorrect=transitionCorrect&&g.m.countdown==5&&g.m.elapsed==0&&ct_ship_s(ct)==0;
        g.paused=false;int countdownFrames=0;
        while(g.m.phase==MISSION_COUNTDOWN&&countdownFrames<302){frame(&g,1/60.0f,true,false);countdownFrames++;}
        transitionCorrect=transitionCorrect&&countdownFrames>=300&&countdownFrames<=301&&g.m.phase==MISSION_RUNNING&&g.m.elapsed==0;
        frame(&g,1/60.0f,true,false);
        transitionCorrect=transitionCorrect&&g.m.elapsed>0&&ct_ship_s(ct)>0;
        printf("mission transition: warp/countdown pause, five-second countdown, score and theme %s\n",transitionCorrect?"passed":"FAILED");
        if(!transitionCorrect)result=3;
    }
    ct_set_scene_draw(ct,NULL,NULL);
    if(g.audio){
        StopAudioStream(g.music);SetAudioStreamCallback(g.music,NULL);UnloadAudioStream(g.music);
        printf("music: %llu stereo frames synthesized, %.1f BPM\n",(unsigned long long)techno_frames(g.techno),techno_bpm(g.techno));
        if(audioCheck&&techno_frames(g.techno)==0)result=4;
        callbackMusic=NULL;techno_destroy(g.techno);
        UnloadSound(g.engine);UnloadSound(g.collect);UnloadSound(g.hit);UnloadSound(g.finish);UnloadSound(g.repair);CloseAudioDevice();
    }
    if(g.ownFont){UnloadFont(g.font);UnloadFont(g.smallFont);}
    if(g.routeSnapshot.id)UnloadRenderTexture(g.routeSnapshot);
    lg_unload_mesh(g.ring);lg_unload_mesh(g.glowRing);lg_unload_mesh(g.targetRing);UnloadMesh(g.sphere);UnloadMesh(g.cube);
    MemFree(g.material.maps);sl_unload_all(&g.shaders);
    return result;
}
