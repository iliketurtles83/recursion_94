#include "mission.h"
#include "core/rng.h"
#include <math.h>
#include <string.h>

int mission_enemy_count(int number)
{
    return number < 1 ? 2 : number < 5 ? number * 2 : MISSION_ENEMY_KINDS;
}
float mission_difficulty(int number)
{
    if(number<=5)return 1;
    if(number<=10)return 1+(number-5)*.08f;
    if(number<=20)return 1.4f+(number-10)*.14f;
    return 2.8f+((float)number-20)*.18f;
}
uint64_t mission_route_seed(const Mission *m)
{
    return m->number==1?m->seed:ct_hash64_2(m->seed,(uint64_t)m->number*0x9e3779b97f4a7c15ull);
}
float mission_theme_hue(int number)
{
    const float hues[]={0,.21f,.83f,.60f,.08f};
    unsigned int n=(unsigned int)(number>0?number-1:0);
    return fmodf(hues[n%5]+(n/5)*.073f,1);
}
const char *mission_enemy_name(EventKind kind)
{
    const char *names[]={"DATA", "GUARD DRONE", "SWEEPER", "PAIRED MINES", "LASER WALL",
        "ORBITER", "MOVING SHUTTER", "INTERCEPTOR", "LASER ROTOR", "PULSAR", "SPLITTER"};
    return kind>=EVENT_PACKET&&kind<=EVENT_SPLITTER?names[kind]:"UNKNOWN";
}
static void layout(Mission *m)
{
    m->difficulty=mission_difficulty(m->number);
    // Approach playable limits smoothly, even thousands of missions into a run.
    float pressure=1-expf(-(m->difficulty-1)*.5f);
    m->enemyKinds=mission_enemy_count(m->number);
    m->packetCount=25+(int)ceilf(15*pressure);
    m->required=(int)ceilf(m->packetCount*(.32f+.43f*pressure));
    m->duration=MISSION_DURATION-30*pressure;
    m->cruiseSpeed=32+25*pressure;
    m->boostSpeed=65+39*pressure;
    Pcg32 rng=pcg32_seed(ct_hash64_2(mission_route_seed(m),0x4441544152554eull));
    const float lanes[]={0,-3.4f,3.4f,0,3.4f,-3.4f};
    double spacing=(MISSION_FINISH-200)/m->packetCount;
    for(int i=0;i<m->packetCount;i++) {
        float x=lanes[i%6],y=i<3?.7f:pcg32_range(&rng,-1.8f,2.4f);
        double s=100+i*spacing;
        m->events[m->eventCount++]=(MissionEvent){.s=s,.x=x,.y=y,.radius=2.1f-.65f*pressure,.kind=EVENT_PACKET};
        if(i<=1||i>=m->packetCount-1)continue;
        // Every unlocked kind occurs; the newest pair appears early in its debut.
        int offset=m->number<=5?m->enemyKinds-2:(m->number*3)%10;
        EventKind kind=(EventKind)(1+(i-2+offset)%m->enemyKinds);
        // After ten, compound encounters require a second steering decision.
        // Keep these separated in depth and never chain two full laser walls.
        bool doubleWave=m->number>30||(m->number>20&&i%2==0)||(m->number>10&&i%4==0);
        float wave=doubleWave?.34f:.48f;
        MissionEvent e={.s=s+spacing*wave,.x=(i%3==0?0:-x),.y=.7f,.radius=1+.25f*pressure,.kind=kind,
            .frequency=.9f+1.8f*pressure,.phase=pcg32_range(&rng,0,6.2831853f)};
        if(kind==EVENT_MINE) {
            e.x=-3.35f;e.y=y*.5f;e.radius=.85f+.12f*pressure;e.s=s+spacing*(wave-.04f);
            m->events[m->eventCount++]=e;
            e.x=3.35f;e.s=s+spacing*(wave+.06f);
        } else if(kind==EVENT_BARRIER||kind==EVENT_SHUTTER) {
            e.x=kind==EVENT_SHUTTER?0:(x+lanes[(i+1)%6])*.5f;
            e.y=kind==EVENT_SHUTTER?.4f:(i%2?2.0f:-1.6f);
            e.radius=2.65f-.6f*pressure;e.amplitude=kind==EVENT_SHUTTER?1.9f+.3f*pressure:0;
            e.frequency=.5f+pressure*.9f;
        } else if(kind==EVENT_SWEEPER||kind==EVENT_ORBITER||kind==EVENT_INTERCEPTOR) {
            e.x=0;e.y=y*.3f;e.amplitude=kind==EVENT_ORBITER?3.1f:2.8f;
            e.radius=(kind==EVENT_INTERCEPTOR?1.1f:1.05f)+.20f*pressure;
            if(kind==EVENT_ORBITER)e.frequency=.65f+pressure*1.3f;
            if(kind==EVENT_INTERCEPTOR)e.frequency=1.1f+pressure*1.6f;
        } else if(kind==EVENT_ROTOR||kind==EVENT_PULSAR||kind==EVENT_SPLITTER) {
            e.x=0;e.y=.4f;e.amplitude=2.5f;e.radius=kind==EVENT_SPLITTER?.8f:.22f;
            e.frequency=.6f+pressure*1.6f;
        }
        m->events[m->eventCount++]=e;
        if(doubleWave) {
            const EventKind followups[]={EVENT_DRONE,EVENT_ORBITER,EVENT_INTERCEPTOR,EVENT_SPLITTER};
            EventKind follow=followups[(i/2+m->number)%4];
            m->events[m->eventCount++]=(MissionEvent){.s=s+spacing*.72,
                .x=follow==EVENT_DRONE?(i%2?3.4f:-3.4f):0,.y=follow==EVENT_DRONE?(i%3?1.8f:-1.6f):.4f,
                .radius=(follow==EVENT_SPLITTER?.8f:1.05f)+.2f*pressure,.kind=follow,
                .amplitude=follow==EVENT_DRONE?0:2.8f,.frequency=1.0f+1.6f*pressure,
                .phase=pcg32_range(&rng,0,6.2831853f)};
        }
    }
}
void mission_reset_at(Mission *m,uint64_t seed,int number)
{
    memset(m,0,sizeof(*m));m->seed=seed;m->number=number>0?number:1;
    m->phase=MISSION_TITLE;m->shield=3;m->combo=1;layout(m);
}
void mission_reset(Mission *m,uint64_t seed){mission_reset_at(m,seed,1);}
static void advance_mission(Mission *m)
{
    uint64_t seed=m->seed;int number=m->number+1,completed=m->completed,shield=m->shield;
    int64_t score=m->score;
    mission_reset_at(m,seed,number);m->completed=completed;m->score=score;
    m->shield=shield<3?shield+1:3;mission_start(m);
}
void mission_start(Mission *m)
{
    m->phase=MISSION_COUNTDOWN;m->countdown=MISSION_COUNTDOWN_SECONDS;
    m->previous=(MissionSample){0};
}

bool mission_can_boost(const Mission *m)
{
    // Held afterburner is a flight control, independent of pickup success.
    return m->phase == MISSION_RUNNING;
}

bool mission_event_opening(const MissionEvent *e){return e->kind==EVENT_BARRIER||e->kind==EVENT_SHUTTER;}
float mission_event_angle(const MissionEvent *e,float time){return time*e->frequency+e->phase;}
float mission_event_radius(const MissionEvent *e,float time)
{
    return e->kind==EVENT_PULSAR?1.1f+2.6f*(.5f+.5f*sinf(mission_event_angle(e,time))):e->radius;
}
void mission_event_position(const MissionEvent *e,float time,float *x,float *y)
{
    *x=e->x;*y=e->y;float a=mission_event_angle(e,time);
    if(e->kind==EVENT_SWEEPER){*x+=e->amplitude*sinf(a);*y+=.55f*cosf(time*e->frequency*.7f+e->phase);}
    if(e->kind==EVENT_ORBITER){*x+=e->amplitude*cosf(a);*y+=1.9f*sinf(a);}
    if(e->kind==EVENT_SHUTTER){*x+=e->amplitude*sinf(a);*y+=.85f*cosf(a*.73f);}
    if(e->kind==EVENT_INTERCEPTOR){*x+=e->amplitude*(2/3.14159265f)*asinf(sinf(a));*y+=.8f*sinf(a*1.3f);}
}
void mission_event_split(const MissionEvent *e,float time,int side,float *x,float *y)
{
    mission_event_position(e,time,x,y);float a=mission_event_angle(e,time);
    *x+=side*e->amplitude*cosf(a);*y+=side*1.6f*sinf(a);
}
bool mission_event_danger(const MissionEvent *e,float time,float x,float y,float clearance)
{
    float ex,ey;mission_event_position(e,time,&ex,&ey);
    float dx=x-ex,dy=y-ey,distance=hypotf(dx,dy);
    if(e->kind==EVENT_PACKET)return false;
    if(mission_event_opening(e))return distance>fmaxf(e->radius-clearance,0);
    if(e->kind==EVENT_ROTOR) {
        float a=mission_event_angle(e,time),along=dx*cosf(a)+dy*sinf(a),across=-dx*sinf(a)+dy*cosf(a);
        return distance<.65f+clearance||(fabsf(along)<6.6f+clearance&&fabsf(across)<e->radius+clearance);
    }
    if(e->kind==EVENT_PULSAR)return fabsf(distance-mission_event_radius(e,time))<e->radius+clearance;
    if(e->kind==EVENT_SPLITTER) {
        for(int side=-1;side<=1;side+=2){mission_event_split(e,time,side,&ex,&ey);if(hypotf(x-ex,y-ey)<e->radius+clearance)return true;}
        return false;
    }
    return distance<e->radius+clearance;
}

void mission_tick(Mission *m, float dt, MissionSample p, bool boosting)
{
    m->justCollected = m->justHit = m->justNear = 0;
    if (dt < 0) dt = 0;
    if(m->phase==MISSION_WARP){
        m->warpRemaining=fmaxf(m->warpRemaining-dt,0);
        if(m->warpRemaining<=0)advance_mission(m);
        return;
    }
    if (m->phase == MISSION_COUNTDOWN) {
        m->countdown -= dt;
        if (m->countdown <= 0) { m->phase = MISSION_RUNNING; m->previous = p; }
        return;
    }
    if (m->phase != MISSION_RUNNING) return;

    MissionSample a = m->previous;
    // Stop exactly at the deadline, even if one large frame crosses both it and the finish.
    float step = fminf(dt, fmaxf(m->duration - m->elapsed, 0));
    if (dt > step && dt > 0) {
        float t = step / dt;
        p.s = a.s + (p.s - a.s) * t;
        p.x = a.x + (p.x - a.x) * t;
        p.y = a.y + (p.y - a.y) * t;
    }
    if(p.s>=MISSION_FINISH && p.s>a.s) {
        float t=(float)((MISSION_FINISH-a.s)/(p.s-a.s));
        p.x=a.x+(p.x-a.x)*t;p.y=a.y+(p.y-a.y)*t;
        p.s=MISSION_FINISH;step*=t;
    }
    m->elapsed += step;
    float immuneUntil=m->immunity;

    for (int i = 0; i < m->eventCount; i++) {
        MissionEvent *e = &m->events[i];
        if (e->resolved || p.s < e->s || a.s > e->s) continue;
        double span = p.s - a.s;
        float t = span > 1e-9 ? (float)((e->s - a.s) / span) : 1;
        float eventTime=m->elapsed-step+step*t;
        float ex,ey;mission_event_position(e,eventTime,&ex,&ey);
        float px=a.x+(p.x-a.x)*t,py=a.y+(p.y-a.y)*t;
        float x=px-ex,y=py-ey;
        float distance = sqrtf(x*x + y*y);
        e->resolved = true;
        if (e->kind == EVENT_PACKET) {
            if (distance < e->radius) {
                e->collected = true;
                m->packets++;
                m->score += 500 * m->combo + (int)((1 - distance / e->radius) * 150);
                if (m->combo < 6) m->combo++;
                m->justCollected++;
            } else { m->misses++; m->combo = 1; }
        } else if (mission_event_danger(e,eventTime,px,py,.55f)) {
            e->hit = true;
            if (immuneUntil <= step*t) {
                m->shield--;
                m->combo = 1;
                immuneUntil = step*t+(m->number>20?.9f:1.2f);
                m->justHit++;
            }
        } else if (mission_event_opening(e) || distance < e->radius + 1.8f) {
            m->nearMisses++;
            m->score += 250 * m->combo;
            m->justNear++;
        }
    }
    m->immunity = fmaxf(immuneUntil-step,0);
    m->previous = p;
    if (m->shield <= 0) { m->phase = MISSION_LOST; m->failure = "SHIP DISABLED"; }
    else if (p.s >= MISSION_FINISH) {
        if (m->packets >= m->required && p.x*p.x+(p.y-1.5f)*(p.y-1.5f)<16) {
            m->phase = MISSION_WARP;
            m->warpRemaining=MISSION_WARP_SECONDS;m->completed++;
            m->score += (int)((m->duration - m->elapsed) * 120) + m->shield * 1200;
        } else { m->phase = MISSION_LOST; m->failure = m->packets < m->required?"NOT ENOUGH DATA":"UPLINK MISSED"; }
    } else if (m->elapsed >= m->duration) { m->phase = MISSION_LOST; m->failure = "UPLINK EXPIRED"; }
}

int mission_next_packet(const Mission *m)
{
    int next = -1;
    for (int i = 0; i < m->eventCount; i++)
        if (m->events[i].kind == EVENT_PACKET && !m->events[i].resolved &&
            (next < 0 || m->events[i].s < m->events[next].s)) next = i;
    return next;
}
