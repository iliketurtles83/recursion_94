#include "mission.h"
#include "core/rng.h"
#include <math.h>
#include <string.h>

void mission_reset(Mission *m, uint64_t seed)
{
    memset(m, 0, sizeof(*m));
    m->phase = MISSION_TITLE;
    m->shield = 3;
    m->combo = 1;
    Pcg32 rng = pcg32_seed(ct_hash64_2(seed, 0x4441544152554eull));
    const float lanes[] = { 0, -3.4f, 3.4f, 0, 3.4f, -3.4f };
    for (int i = 0; i < 25; i++) {
        float x = lanes[i % 6];
        float y = i < 3 ? 0.7f : pcg32_range(&rng, -1.8f, 2.4f);
        double s = 100.0 + i * 112.0;
        m->events[m->eventCount++] = (MissionEvent){ s, x, y, 2.1f, EVENT_PACKET, false, false, false };
        if (i > 1 && i < 24) {
            float hx = i % 3 == 0 ? 0.0f : -x;
            if(i%4==0) {
                // A wall of lasers with a large, explicit cyan safe opening.
                float gapX=(x+lanes[(i+1)%6])*.5f;
                float gapY=(i%8==0)?2.3f:-1.8f;
                m->events[m->eventCount++] = (MissionEvent){.s=s+54,.x=gapX,.y=gapY,.radius=2.65f,.kind=EVENT_BARRIER};
            } else if(i%4==1) {
                m->events[m->eventCount++] = (MissionEvent){.s=s+54,.x=0,.y=y*.4f,.radius=1.05f,.kind=EVENT_SWEEPER,
                    .amplitude=2.8f,.frequency=.9f,.phase=pcg32_range(&rng,0,6.2831853f)};
            } else if(i%4==2 && i>5) {
                // Paired mines close the two outer lanes; the center stays open.
                m->events[m->eventCount++] = (MissionEvent){.s=s+52,.x=-3.35f,.y=y*.5f,.radius=.85f,.kind=EVENT_MINE};
                m->events[m->eventCount++] = (MissionEvent){.s=s+56,.x=3.35f,.y=y*.5f,.radius=.85f,.kind=EVENT_MINE};
            } else {
                m->events[m->eventCount++] = (MissionEvent){.s=s+54,.x=hx,.y=.7f,.radius=1.0f,.kind=EVENT_DRONE};
            }
        }
    }
}

void mission_start(Mission *m)
{
    m->phase = MISSION_COUNTDOWN;
    m->countdown = 3;
    m->previous = (MissionSample){ 0, 0, 0 };
}

bool mission_can_boost(const Mission *m)
{
    // Held afterburner is a flight control, independent of pickup success.
    return m->phase == MISSION_RUNNING;
}

void mission_event_position(const MissionEvent *e,float time,float *x,float *y)
{
    *x=e->x;*y=e->y;
    if(e->kind==EVENT_SWEEPER) {
        *x+=e->amplitude*sinf(time*e->frequency+e->phase);
        *y+=.55f*cosf(time*e->frequency*.7f+e->phase);
    }
}
bool mission_event_danger(const MissionEvent *e,float time,float x,float y,float clearance)
{
    float ex,ey;mission_event_position(e,time,&ex,&ey);
    float distance=hypotf(x-ex,y-ey);
    if(e->kind==EVENT_PACKET)return false;
    return e->kind==EVENT_BARRIER?distance>fmaxf(e->radius-clearance,0):distance<e->radius+clearance;
}

void mission_tick(Mission *m, float dt, MissionSample p, bool boosting)
{
    m->justCollected = m->justHit = m->justNear = 0;
    if (dt < 0) dt = 0;
    if (m->phase == MISSION_COUNTDOWN) {
        m->countdown -= dt;
        if (m->countdown <= 0) { m->phase = MISSION_RUNNING; m->previous = p; }
        return;
    }
    if (m->phase != MISSION_RUNNING) return;

    MissionSample a = m->previous;
    // Stop exactly at the deadline, even if one large frame crosses both it and the finish.
    float step = fminf(dt, fmaxf(MISSION_DURATION - m->elapsed, 0));
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
                immuneUntil = step*t+1.2f;
                m->justHit++;
            }
        } else if (e->kind==EVENT_BARRIER || distance < e->radius + 1.8f) {
            m->nearMisses++;
            m->score += 250 * m->combo;
            m->justNear++;
        }
    }
    m->immunity = fmaxf(immuneUntil-step,0);
    m->previous = p;
    if (m->shield <= 0) { m->phase = MISSION_LOST; m->failure = "SHIP DISABLED"; }
    else if (p.s >= MISSION_FINISH) {
        if (m->packets >= MISSION_REQUIRED && p.x*p.x+(p.y-1.5f)*(p.y-1.5f)<16) {
            m->phase = MISSION_WON;
            m->score += (int)((MISSION_DURATION - m->elapsed) * 120) + m->shield * 1200;
        } else { m->phase = MISSION_LOST; m->failure = m->packets < MISSION_REQUIRED?"NOT ENOUGH DATA":"UPLINK MISSED"; }
    } else if (m->elapsed >= MISSION_DURATION) { m->phase = MISSION_LOST; m->failure = "UPLINK EXPIRED"; }
}

int mission_next_packet(const Mission *m)
{
    int next = -1;
    for (int i = 0; i < m->eventCount; i++)
        if (m->events[i].kind == EVENT_PACKET && !m->events[i].resolved &&
            (next < 0 || m->events[i].s < m->events[next].s)) next = i;
    return next;
}
