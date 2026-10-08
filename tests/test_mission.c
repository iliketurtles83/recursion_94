#include "game/mission.h"
#include "core/flight.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
static int checks,failures;
#define CHECK(x) do{checks++;if(!(x)){failures++;printf("FAIL line %d: %s\n",__LINE__,#x);}}while(0)
static Mission running(void) {Mission m;mission_reset(&m,42);mission_start(&m);mission_tick(&m,MISSION_COUNTDOWN_SECONDS,(MissionSample){0},false);return m;}
static void one_event(Mission *m,MissionEvent e) {m->eventCount=1;m->events[0]=e;}
static void test_continuous_boost(bool pickups)
{
    // Use real flight physics and the authored gate spacing. Put gates on the
    // centerline to isolate successful pickups from steering and drone damage.
    Mission m=running();
    for(int i=0;i<m.eventCount;i++) {
        if(m.events[i].kind!=EVENT_PACKET)m.events[i].resolved=true;
        else m.events[i].x=m.events[i].y=0;
    }
    if(!pickups){m.eventCount=0;m.packets=8;}
    PathStyle style={12,.12f,60,14,20,.15f,.015f,true};
    PathSystem path;path_init(&path,42,style);path_ensure(&path,0,3500);
    ParamRegistry reg={0};FlightParams fp;
    flight_register_params(&fp,&reg);fp.baseSpeed=32;fp.boostSpeed=65;
    FlightState ship;flight_reset(&ship,&fp,&path,0);
    const float dt=1/120.0f;int gaps=0;float minSpeed=1000;
    for(int i=0;i<120*90 && m.phase==MISSION_RUNNING;i++) {
        FlightInput in={.boost=mission_can_boost(&m)};
        if(m.elapsed>2 && !in.boost)gaps++;
        flight_step(&ship,&fp,&path,&in,dt);
        mission_tick(&m,dt,(MissionSample){ship.s,ship.x,ship.y},in.boost);
        if(m.elapsed>2)minSpeed=fminf(minSpeed,ship.speed);
    }
    CHECK(m.phase==MISSION_WARP && m.packets==(pickups?25:8));
    CHECK(gaps==0 && minSpeed>64.7f);
    printf("held boost %s pickups: minimum %.2f m/s, %d unrequested boost interruptions\n",pickups?"with":"without",minSpeed,gaps);
    path_free(&path);
}
static void test_endless(void)
{
    Mission m;mission_reset(&m,83);int64_t total=5000000000ll;
    for(int number=1;number<=16;number++) {
        CHECK(m.number==number&&m.enemyKinds==mission_enemy_count(number));
        int kinds[11]={0};
        CHECK(m.eventCount<MISSION_EVENTS);
        for(int i=0;i<m.eventCount;i++) {
            MissionEvent *e=&m.events[i];CHECK((int)e->kind<=m.enemyKinds);kinds[e->kind]++;
            if(i)CHECK(e->s>m.events[i-1].s);
        }
        CHECK(kinds[0]==m.packetCount);
        for(int kind=1;kind<=m.enemyKinds;kind++)CHECK(kinds[kind]>0);
        if(number>5)CHECK(m.boostSpeed>65&&m.packetCount>25&&m.duration<90);
        uint64_t route=mission_route_seed(&m);float hue=mission_theme_hue(number);
        m.score=total;m.phase=MISSION_RUNNING;m.packets=m.required;m.shield=2;m.eventCount=0;
        m.previous=(MissionSample){2999,0,1.5f};
        mission_tick(&m,.1f,(MissionSample){3001,0,1.5f},true);
        CHECK(m.phase==MISSION_WARP&&m.completed==number&&m.score>total);
        total=m.score;mission_tick(&m,1,(MissionSample){3500},true);
        CHECK(m.phase==MISSION_WARP&&m.elapsed<.1f&&m.score==total);
        mission_tick(&m,1.5f,(MissionSample){4000},true);
        CHECK(m.phase==MISSION_COUNTDOWN&&m.number==number+1&&m.countdown==5);
        CHECK(m.score==total&&m.completed==number&&m.shield==3&&m.packets==0&&m.elapsed==0);
        CHECK(m.previous.s==0&&mission_route_seed(&m)!=route&&mission_theme_hue(m.number)!=hue);
        mission_tick(&m,4.99f,(MissionSample){0},false);CHECK(m.phase==MISSION_COUNTDOWN);
        mission_tick(&m,.02f,(MissionSample){0},false);CHECK(m.phase==MISSION_RUNNING&&m.elapsed==0);
    }
    Mission a,b;mission_reset_at(&a,17,5);mission_reset_at(&b,17,5);
    CHECK(a.enemyKinds==10&&memcmp(a.events,b.events,sizeof(a.events))==0);
    mission_reset_at(&b,17,6);CHECK(memcmp(a.events,b.events,sizeof(a.events))!=0);
    mission_reset_at(&a,17,10000);CHECK(a.enemyKinds==10&&a.eventCount<MISSION_EVENTS);
    // Shape-specific collision must agree with the rendered moving geometry.
    for(int kind=EVENT_ORBITER;kind<=EVENT_INTERCEPTOR;kind++) {
        MissionEvent e={.kind=(EventKind)kind,.amplitude=2,.frequency=.7f,.radius=kind==EVENT_SHUTTER?2.65f:1};
        float x,y;mission_event_position(&e,1.3f,&x,&y);
        CHECK(mission_event_danger(&e,1.3f,x,y,.55f)==!mission_event_opening(&e));
        CHECK(mission_event_danger(&e,1.3f,x+5,y,.55f)==mission_event_opening(&e));
    }
    MissionEvent rotor={.kind=EVENT_ROTOR,.radius=.22f,.frequency=1};
    CHECK(mission_event_danger(&rotor,0,3,0,.55f));CHECK(!mission_event_danger(&rotor,0,0,3,.55f));
    CHECK(mission_event_danger(&rotor,3.14159265f/2,0,3,.55f));
    MissionEvent pulse={.kind=EVENT_PULSAR,.radius=.22f,.frequency=1};float radius=mission_event_radius(&pulse,.5f);
    CHECK(mission_event_danger(&pulse,.5f,radius,0,.55f));CHECK(!mission_event_danger(&pulse,.5f,0,0,.55f));
    MissionEvent split={.kind=EVENT_SPLITTER,.amplitude=2.5f,.frequency=1,.radius=.8f};
    for(int side=-1;side<=1;side+=2){float x,y;mission_event_split(&split,.8f,side,&x,&y);CHECK(mission_event_danger(&split,.8f,x,y,.55f));}
    CHECK(!mission_event_danger(&split,0,0,0,.55f));
}
int main(void)
{
    Mission m,n;mission_reset(&m,42);mission_reset(&n,42);
    CHECK(m.eventCount==47 && m.shield==3 && m.phase==MISSION_TITLE);
    CHECK(memcmp(m.events,n.events,sizeof(m.events))==0);
    mission_reset(&n,43);CHECK(memcmp(m.events,n.events,sizeof(m.events))!=0);
    for(int i=1;i<m.eventCount;i++) CHECK(m.events[i].s>m.events[i-1].s);
    mission_start(&m);mission_tick(&m,2,(MissionSample){0},false);CHECK(m.phase==MISSION_COUNTDOWN);
    mission_tick(&m,3,(MissionSample){0},false);CHECK(m.phase==MISSION_RUNNING && m.elapsed==0);
    CHECK(mission_can_boost(&m));
    m=running();one_event(&m,(MissionEvent){.s=100,.x=0,.y=0,.radius=2.1f,.kind=EVENT_PACKET});
    m.previous=(MissionSample){90,-3,0};
    mission_tick(&m,.5f,(MissionSample){110,3,0},false);
    CHECK(m.packets==1 && m.events[0].collected && m.combo==2 && m.score==650);
    mission_tick(&m,.5f,(MissionSample){120,0,0},false);CHECK(m.packets==1);
    m=running();one_event(&m,(MissionEvent){.s=10,.x=4,.radius=2,.kind=EVENT_PACKET});
    mission_tick(&m,1,(MissionSample){20},false);CHECK(m.misses==1 && m.packets==0);
    m=running();m.eventCount=3;
    for(int i=0;i<3;i++)m.events[i]=(MissionEvent){.s=10+i*10,.radius=1,.kind=EVENT_DRONE};
    mission_tick(&m,3,(MissionSample){30},false);
    CHECK(m.shield==1 && m.justHit==2 && fabsf(m.immunity-1.2f)<.01f);
    m.events[3]=(MissionEvent){.s=50,.radius=1,.kind=EVENT_DRONE};m.eventCount=4;
    mission_tick(&m,3,(MissionSample){60},false);CHECK(m.phase==MISSION_LOST && m.shield==0);
    m=running();one_event(&m,(MissionEvent){.s=10,.x=2,.radius=1,.kind=EVENT_DRONE});
    mission_tick(&m,1,(MissionSample){20},false);CHECK(m.nearMisses==1 && m.shield==3 && m.score==250);
    m=running();m.eventCount=0;
    mission_tick(&m,5,(MissionSample){10},true);CHECK(mission_can_boost(&m));
    mission_tick(&m,2,(MissionSample){20},false);CHECK(mission_can_boost(&m));
    mission_tick(&m,1,(MissionSample){30},false);CHECK(mission_can_boost(&m));
    m=running();
    one_event(&m,(MissionEvent){.s=10,.radius=2.1f,.kind=EVENT_PACKET});
    mission_tick(&m,.1f,(MissionSample){20},false);
    CHECK(m.packets==1 && mission_can_boost(&m));
    m=running();m.eventCount=0;m.packets=8;m.previous=(MissionSample){2990,0,1};m.elapsed=89;
    mission_tick(&m,2,(MissionSample){3010,0,1},false);CHECK(m.phase==MISSION_WARP && fabsf(m.elapsed-90)<.001f);
    m=running();m.eventCount=0;m.packets=8;m.previous=(MissionSample){2980,0,1};m.elapsed=89;
    mission_tick(&m,2,(MissionSample){3010,0,1},false);CHECK(m.phase==MISSION_LOST && !strcmp(m.failure,"UPLINK EXPIRED"));
    m=running();m.eventCount=0;mission_tick(&m,90,(MissionSample){2999},false);CHECK(m.phase==MISSION_LOST);
    m=running();m.eventCount=0;mission_tick(&m,60,(MissionSample){3001,0,1},false);CHECK(m.phase==MISSION_LOST && !strcmp(m.failure,"NOT ENOUGH DATA"));
    m=running();m.eventCount=0;m.packets=8;mission_tick(&m,60,(MissionSample){3001,6,1},false);CHECK(m.phase==MISSION_LOST && !strcmp(m.failure,"UPLINK MISSED"));
    m=running();m.eventCount=0;m.packets=8;mission_tick(&m,60,(MissionSample){3001,0,1},false);CHECK(m.phase==MISSION_WARP && m.score>3600);
    int64_t score=m.score;mission_tick(&m,5,(MissionSample){4000},false);CHECK(m.score==score);
    mission_reset(&m,42);CHECK(m.packets==0 && m.elapsed==0 && m.shield==3 && mission_next_packet(&m)==0);
    test_continuous_boost(true);
    test_continuous_boost(false);
    // Moving enemies collide at the crossing time, even with a large step.
    m=running();one_event(&m,(MissionEvent){.s=10,.radius=1,.kind=EVENT_SWEEPER,.amplitude=3,.frequency=1});
    float ex,ey;mission_event_position(&m.events[0],.5f,&ex,&ey);
    m.previous=(MissionSample){0,ex,ey};
    mission_tick(&m,1,(MissionSample){20,ex,ey},false);
    CHECK(m.shield==2 && m.events[0].hit);
    m=running();one_event(&m,(MissionEvent){.s=10,.radius=1,.kind=EVENT_SWEEPER,.amplitude=3,.frequency=1});
    mission_tick(&m,1,(MissionSample){20,-3,-2},false);CHECK(m.shield==3);
    m=running();one_event(&m,(MissionEvent){.s=10,.x=2,.y=-1,.radius=2.65f,.kind=EVENT_BARRIER});
    m.previous=(MissionSample){0,2,-1};mission_tick(&m,1,(MissionSample){20,2,-1},false);
    CHECK(m.shield==3 && m.justNear==1 && !m.events[0].hit);
    m=running();one_event(&m,(MissionEvent){.s=10,.x=2,.y=-1,.radius=2.65f,.kind=EVENT_BARRIER});
    mission_tick(&m,1,(MissionSample){20,-4,2},false);CHECK(m.shield==2 && m.events[0].hit);
    mission_reset(&m,42);int kinds[11]={0};
    for(int i=0;i<m.eventCount;i++){kinds[m.events[i].kind]++;CHECK(fabsf(m.events[i].x)<4&&fabsf(m.events[i].y)<3);}
    CHECK(kinds[0]==25 && kinds[1]>0 && kinds[2]>0 && kinds[3]==0 && kinds[4]==0);
    test_endless();
    printf("mission: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
