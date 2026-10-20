#include "flight.h"
#include "noise.h"
#include <math.h>
#include <string.h>

#define PI_F 3.14159265f

void flight_register_params(FlightParams *p, ParamRegistry *reg)
{
    const char *g = "Flight";
    params_float(reg, g, "baseSpeed", &p->baseSpeed, 25.0f, 0.0f, 300.0f);
    params_float(reg, g, "boostSpeed", &p->boostSpeed, 40.0f, 0.0f, 400.0f);
    params_float(reg, g, "speedResponse", &p->speedResponse, 2.5f, 0.1f, 20.0f);
    params_float(reg, g, "steerLimit", &p->steerLimit, 0.55f, 0.0f, 0.9f);
    params_float(reg, g, "steerAccel", &p->steerAccel, 60.0f, 1.0f, 300.0f);
    params_float(reg, g, "steerDamping", &p->steerDamping, 5.0f, 0.1f, 30.0f);
    p->dartShip = false;   // registered by the module as the Sim parameter shipDart
    // Off by default (original flight model); presets/mission*.ini enable it for DATA // RUN.
    params_float(reg,g,"vectoringStrength",&p->vectoringStrength,0.0f,0,2);
    params_float(reg,g,"vectoringAngleDeg",&p->vectoringAngleDeg,14,0,22);
    params_float(reg, g, "maxBankDeg", &p->maxBankDeg, 35.0f, 0.0f, 80.0f);
    params_float(reg, g, "maxShipPitchDeg", &p->maxPitchDeg, 15.0f, 0.0f, 45.0f);
    params_bool(reg, g, "autopilot", &p->autopilot, false);
    g = "Camera";
    params_float(reg, g, "camDistance", &p->camDistance, 6.0f, 1.0f, 30.0f);
    params_float(reg, g, "camHeight", &p->camHeight, 1.6f, -5.0f, 10.0f);
    params_float(reg, g, "camStiffness", &p->camStiffness, 7.0f, 0.5f, 40.0f);
    params_float(reg, g, "camUpStiffness", &p->camUpStiffness, 5.0f, 0.5f, 40.0f);
    params_float(reg, g, "camFollowLateral", &p->camFollowLateral, 0.75f, 0.0f, 1.0f);
    params_float(reg, g, "lookAhead", &p->lookAhead, 25.0f, 1.0f, 100.0f);
    params_float(reg, g, "lookBlend", &p->lookBlend, 0.55f, 0.0f, 1.0f);
    params_float(reg, g, "fovBase", &p->fovBase, 70.0f, 30.0f, 120.0f);
    params_float(reg, g, "fovBoost", &p->fovBoost, 15.0f, 0.0f, 40.0f);
    params_float(reg, g, "shakeAmount", &p->shakeAmount, 0.06f, 0.0f, 0.5f);
    params_float(reg, g, "cameraRollFollow", &p->cameraRollFollow, 0.35f, 0.0f, 1.0f);
    params_float(reg, g, "barrelRollTime", &p->barrelRollTime, 0.7f, 0.2f, 3.0f);
}

float flight_speed_norm(const FlightParams *p, float speed)
{
    float d = p->boostSpeed - p->baseSpeed;
    if (d <= 1e-3f) return 0.0f;
    float t = (speed - p->baseSpeed) / d;
    return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
}

void flight_engine_offsets(bool dart, Vector3 out[2])
{
    if (dart) {
        out[0] = (Vector3){ -0.16f, 0.02f, -0.62f };
        out[1] = (Vector3){ 0.16f, 0.02f, -0.62f };
    } else {
        out[0] = (Vector3){ -0.48f, -0.05f, -1.56f };
        out[1] = (Vector3){ 0.48f, -0.05f, -1.56f };
    }
}

Vector3 flight_gimbal_rotate(Vector3 p,Vector2 a)
{
    float cx=cosf(a.x),sx=sinf(a.x),cy=cosf(a.y),sy=sinf(a.y);
    Vector3 q={p.x,cx*p.y-sx*p.z,sx*p.y+cx*p.z};
    return (Vector3){cy*q.x+sy*q.z,q.y,-sy*q.x+cy*q.z};
}
Vector3 flight_exhaust_offset(int engine,Vector2 angles,float plumeLength)
{
    Vector3 p=flight_gimbal_rotate((Vector3){0,0,-.385f-plumeLength},angles);
    p.x+=engine?.48f:-.48f;p.y-=.05f;p.z-=1.40f;return p;
}
static float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }

// Implicit critically damped spring step.
static void spring(float *x, float *v, float target, float omega, float dt)
{
    float f = 1.0f + 2.0f * dt * omega;
    float oo = omega * omega;
    float hoo = dt * oo;
    float hhoo = dt * hoo;
    float detInv = 1.0f / (f + hhoo);
    float nx = (f * *x + dt * *v + hhoo * target) * detInv;
    float nv = (*v + hoo * (target - *x)) * detInv;
    *x = nx; *v = nv;
}

static void derive(FlightState *st, const FlightParams *p, const PathSystem *path, bool snapCamera, double dt)
{
    PathSample ps = path_sample(path, st->s);
    st->shipPos = dv_add(ps.pos, dv_add(dv_scale(ps.B, st->x), dv_scale(ps.N, st->y)));

    // Ship orientation: forward = T, then bank (+barrel) around T, tilt around right.
    double roll = st->bank + st->barrel;
    DVec3 up = dv_rotate(ps.N, ps.T, roll);
    DVec3 right = dv_cross(ps.T, up);
    DVec3 fwd = dv_rotate(ps.T, right, st->tilt);
    up = dv_rotate(up, right, st->tilt);
    st->shipFwd = fwd; st->shipUp = up; st->shipRight = dv_cross(fwd, up);

    // Camera local offset target in the path frame at the ship.
    float target[3] = { st->x * p->camFollowLateral, st->y * p->camFollowLateral + p->camHeight, -p->camDistance };
    for (int i = 0; i < 3; i++) {
        if (snapCamera) { st->camLocal[i] = target[i]; st->camLocalVel[i] = 0.0f; }
        else spring(&st->camLocal[i], &st->camLocalVel[i], target[i], p->camStiffness, (float)dt);
    }
    st->camPos = dv_add(ps.pos, dv_add(dv_add(dv_scale(ps.B, st->camLocal[0]), dv_scale(ps.N, st->camLocal[1])), dv_scale(ps.T, st->camLocal[2])));

    // Camera up: tunnel roll plus a fraction of ship bank/barrel roll.
    DVec3 upTarget = dv_rotate(ps.N, ps.T, roll * p->cameraRollFollow);
    if (snapCamera) st->camUp = upTarget;
    else {
        double k = 1.0 - exp(-p->camUpStiffness * dt);
        st->camUp = dv_norm(dv_lerp(st->camUp, upTarget, k));
    }

    PathSample ahead = path_sample(path, st->s + p->lookAhead);
    st->camTarget = dv_lerp(st->shipPos, ahead.pos, p->lookBlend);

    float sn = flight_speed_norm(p, st->speed);
    st->fov = p->fovBase + p->fovBoost * sn;
    if (p->shakeAmount > 0.0f && sn > 0.01f) {
        double t = st->time * 9.0;
        double a = p->shakeAmount * sn;
        DVec3 sh = dv_add(dv_scale(ps.B, a * ct_noise1(11, t)), dv_scale(ps.N, a * ct_noise1(23, t)));
        st->camPos = dv_add(st->camPos, sh);
    }
}

void flight_reset(FlightState *st, const FlightParams *p, const PathSystem *path, double s)
{
    memset(st, 0, sizeof(*st));
    st->s = s;
    st->speed = p->baseSpeed;
    st->barrelT = -1.0f;
    derive(st, p, path, true, 0.0);
    st->trailHead = 0;
    st->trailCount = 0;
}

void flight_step(FlightState *st, const FlightParams *p, const PathSystem *path, const FlightInput *in, double dt)
{
    st->time += dt;
    float target = in->boost ? p->boostSpeed : p->baseSpeed;
    st->speed += (target - st->speed) * (1.0f - expf(-p->speedResponse * (float)dt));
    st->s += st->speed * dt;

    float sx = in->steerX, sy = in->steerY;
    if (p->autopilot) {
        sx = (float)ct_noise1(101, st->time * 0.35) * 0.9f;
        sy = (float)ct_noise1(202, st->time * 0.3) * 0.7f;
    }
    PathSample ps = path_sample(path, st->s);
    float limit = p->steerLimit * (float)ps.radius;
    float fdt = (float)dt;
    float servo = 1.0f - expf(-12.0f * fdt);
    st->controlX += (clampf(sx,-1,1) - st->controlX) * servo;
    st->controlY += (clampf(sy,-1,1) - st->controlY) * servo;
    float authority=in->boost&&p->vectoringStrength>0?flight_speed_norm(p,st->speed):0;
    st->vectorPower+=(authority-st->vectorPower)*(1-expf(-8*fdt));
    float maxAngle=p->vectoringAngleDeg*PI_F/180;
    // Flight computer holds the requested tunnel-plane direction while the
    // body banks/rolls. The exhaust points opposite the desired lateral force.
    DVec3 desired=dv_add(dv_scale(ps.B,clampf(sx,-1,1)),dv_scale(ps.N,clampf(sy,-1,1)));
    float localX=(float)dv_dot(desired,st->shipRight),localY=(float)dv_dot(desired,st->shipUp);
    float rollCommand=.35f*clampf(sx,-1,1);
    if(st->barrelT>=0)rollCommand+=.65f*st->barrelDir*sinf(2*PI_F*st->barrelT);
    rollCommand=clampf(rollCommand,-1,1);
    Vector3 reaction={0};
    for(int e=0;e<2;e++) {
        float side=e?1:-1;
        Vector2 goal={(-localY+side*.42f*rollCommand)*maxAngle*st->vectorPower,
                      localX*maxAngle*st->vectorPower};
        float cone=hypotf(goal.x,goal.y);
        if(cone>maxAngle&&cone>0){goal.x*=maxAngle/cone;goal.y*=maxAngle/cone;}
        st->gimbal[e].x+=(goal.x-st->gimbal[e].x)*servo;
        st->gimbal[e].y+=(goal.y-st->gimbal[e].y)*servo;
        Vector3 jet=flight_gimbal_rotate((Vector3){0,0,-1},st->gimbal[e]);
        reaction.x-=jet.x*.5f;reaction.y-=jet.y*.5f;
    }
    DVec3 force=dv_add(dv_scale(st->shipRight,reaction.x),dv_scale(st->shipUp,reaction.y));
    float thrust=p->steerAccel*p->vectoringStrength*st->vectorPower;
    st->vx += (sx*p->steerAccel+thrust*(float)dv_dot(force,ps.B)-st->vx*p->steerDamping)*fdt;
    st->vy += (sy*p->steerAccel+thrust*(float)dv_dot(force,ps.N)-st->vy*p->steerDamping)*fdt;
    st->x += st->vx * fdt;
    st->y += st->vy * fdt;
    float r = sqrtf(st->x * st->x + st->y * st->y);
    if (r > limit && r > 1e-4f) {
        float k = limit / r;
        st->x *= k; st->y *= k;
        // remove outward velocity
        float nx = st->x / limit, ny = st->y / limit;
        float vo = st->vx * nx + st->vy * ny;
        if (vo > 0.0f) { st->vx -= vo * nx; st->vy -= vo * ny; }
    }
    // Floor / ceiling (open environments): a flat clamp on the vertical offset.
    const PathStyle *style = &path->style;
    if (style->floorDepth > 0.0f) {
        float yMin = fminf(-style->floorDepth + FLIGHT_CLEARANCE, 0.0f);
        if (st->y < yMin) { st->y = yMin; if (st->vy < 0.0f) st->vy = 0.0f; }
    }
    if (style->ceilingHeight > 0.0f) {
        float yMax = fmaxf(style->ceilingHeight - FLIGHT_CLEARANCE, 0.0f);
        if (st->y > yMax) { st->y = yMax; if (st->vy > 0.0f) st->vy = 0.0f; }
    }
    float vmax = p->steerAccel / p->steerDamping;
    float bankTarget = clampf(st->vx / vmax, -1.0f, 1.0f) * p->maxBankDeg * PI_F / 180.0f;
    float tiltTarget = clampf(st->vy / vmax, -1.0f, 1.0f) * p->maxPitchDeg * PI_F / 180.0f;
    // Opposing vertical jets generate a roll couple. This supplements the
    // normal attitude controller; it is bounded to six degrees of bank assist.
    float rollJet=(sinf(st->gimbal[1].x)-sinf(st->gimbal[0].x))*.5f;
    float rollTorque=clampf(rollJet/fmaxf(sinf(maxAngle*.42f),.001f),-1,1);
    bankTarget+=rollTorque*st->vectorPower*6*PI_F/180;
    float k = 1.0f - expf(-8.0f * fdt);
    st->bank += (bankTarget - st->bank) * k;
    st->tilt += (tiltTarget - st->tilt) * k;

    if (st->barrelT < 0.0f && (in->rollLeft || in->rollRight)) {
        st->barrelT = 0.0f;
        st->barrelDir = in->rollLeft ? -1 : 1;
    }
    if (st->barrelT >= 0.0f) {
        st->barrelT += fdt / p->barrelRollTime;
        if (st->barrelT >= 1.0f) { st->barrelT = -1.0f; st->barrel = 0.0f; }
        else {
            float t = st->barrelT;
            float e = t * t * (3.0f - 2.0f * t);
            st->barrel = (float)st->barrelDir * e * 2.0f * PI_F;
        }
    }

    derive(st, p, path, false, dt);

    // Engine trail history.
    Vector3 eng[2];
    if (p->dartShip) flight_engine_offsets(true, eng);
    else {
        float plumeLength=.48f+2.6f*flight_speed_norm(p,st->speed);
        for(int e=0;e<2;e++)eng[e]=flight_exhaust_offset(e,st->gimbal[e],plumeLength);
    }
    for (int e = 0; e < 2; e++) {
        DVec3 w = dv_add(st->shipPos, dv_add(dv_add(dv_scale(st->shipRight, eng[e].x), dv_scale(st->shipUp, eng[e].y)), dv_scale(st->shipFwd, eng[e].z)));
        st->trail[e][st->trailHead] = w;
    }
    st->trailHead = (st->trailHead + 1) % FLIGHT_TRAIL_LEN;
    if (st->trailCount < FLIGHT_TRAIL_LEN) st->trailCount++;
}

int flight_advance(FlightState *prev, FlightState *cur, const FlightParams *p, const PathSystem *path,
                   FlightInput *in, double *acc, double dt, int maxSteps)
{
    const double h = 1.0 / FLIGHT_HZ;
    *acc += dt;
    int steps = 0;
    while (*acc >= h) {
        *prev = *cur;
        flight_step(cur, p, path, in, h);
        *acc -= h;
        in->rollLeft = in->rollRight = false;
        if (++steps >= maxSteps) { *acc = 0.0; break; }
    }
    return steps;
}

static DVec3 nlerp(DVec3 a, DVec3 b, double t) { return dv_norm(dv_lerp(a, b, t)); }

FlightView flight_interpolate(const FlightState *a, const FlightState *b, float alpha)
{
    FlightView v;
    v.shipPos = dv_lerp(a->shipPos, b->shipPos, alpha);
    v.shipFwd = nlerp(a->shipFwd, b->shipFwd, alpha);
    v.shipUp = nlerp(a->shipUp, b->shipUp, alpha);
    v.shipRight = dv_norm(dv_cross(v.shipFwd, v.shipUp));
    v.shipUp = dv_cross(v.shipRight, v.shipFwd);
    v.camPos = dv_lerp(a->camPos, b->camPos, alpha);
    v.camTarget = dv_lerp(a->camTarget, b->camTarget, alpha);
    v.camUp = nlerp(a->camUp, b->camUp, alpha);
    v.fov = a->fov + (b->fov - a->fov) * alpha;
    v.s = a->s + (b->s - a->s) * alpha;
    v.speed = a->speed + (b->speed - a->speed) * alpha;
    v.controlX = a->controlX + (b->controlX-a->controlX)*alpha;
    v.controlY = a->controlY + (b->controlY-a->controlY)*alpha;
    v.vectorPower=a->vectorPower+(b->vectorPower-a->vectorPower)*alpha;
    for(int e=0;e<2;e++)v.gimbal[e]=(Vector2){a->gimbal[e].x+(b->gimbal[e].x-a->gimbal[e].x)*alpha,a->gimbal[e].y+(b->gimbal[e].y-a->gimbal[e].y)*alpha};
    return v;
}
