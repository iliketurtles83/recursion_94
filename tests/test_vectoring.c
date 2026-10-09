#include "core/flight.h"
#include <math.h>
#include <stdio.h>

static int checks,failures;
#define CHECK(x) do{checks++;if(!(x)){failures++;printf("FAIL line %d: %s\n",__LINE__,#x);}}while(0)
static const double dt=1.0/FLIGHT_HZ;
static void advance(FlightState *s,const FlightParams *p,const PathSystem *path,FlightInput in,int steps)
{
    for(int i=0;i<steps;i++)flight_step(s,p,path,&in,dt);
}
static void test_forces(const FlightParams *p,const PathSystem *path)
{
    FlightParams plain=*p;plain.vectoringStrength=0;
    const Vector2 directions[]={{1,0},{-1,0},{0,1},{0,-1}};
    for(int d=0;d<4;d++) {
        FlightState a,b;flight_reset(&a,&plain,path,0);flight_reset(&b,p,path,0);
        FlightInput in={.steerX=directions[d].x,.steerY=directions[d].y};
        advance(&a,&plain,path,in,72);advance(&b,p,path,in,72);
        CHECK(a.x==b.x&&a.y==b.y&&a.vx==b.vx&&a.vy==b.vy&&a.bank==b.bank&&a.tilt==b.tilt);
        CHECK(b.vectorPower==0&&b.gimbal[0].x==0&&b.gimbal[1].y==0);
        flight_reset(&a,&plain,path,0);flight_reset(&b,p,path,0);
        advance(&a,&plain,path,(FlightInput){.boost=true},480);
        advance(&b,p,path,(FlightInput){.boost=true},480);
        CHECK(b.x==0&&b.y==0&&b.vectorPower>.99f);
        in.boost=true;advance(&a,&plain,path,in,72);advance(&b,p,path,in,72);
        float distance=hypotf(a.x,a.y),vectored=hypotf(b.x,b.y);
        CHECK(vectored>distance*1.10f&&vectored<distance*1.30f);
        CHECK(a.speed==b.speed&&a.s==b.s&&b.speed>64.99f);
        printf("vector steering %d: %.3f -> %.3f metres, %.1f%% gain, identical forward speed\n",d,distance,vectored,(vectored/distance-1)*100);
        PathSample frame=path_sample(path,b.s);Vector3 exhaust={0};
        for(int e=0;e<2;e++){
            Vector3 jet=flight_gimbal_rotate((Vector3){0,0,-1},b.gimbal[e]);
            exhaust.x+=jet.x*.5f;exhaust.y+=jet.y*.5f;
            CHECK(hypotf(b.gimbal[e].x,b.gimbal[e].y)<=p->vectoringAngleDeg*3.14159265f/180+.00001f);
        }
        DVec3 jet=dv_add(dv_scale(b.shipRight,exhaust.x),dv_scale(b.shipUp,exhaust.y));
        CHECK(dv_dot(jet,frame.B)*directions[d].x+dv_dot(jet,frame.N)*directions[d].y<-.15);
        // Releasing boost recenters the nozzles even while speed is still high.
        in.boost=false;advance(&b,p,path,in,120);
        CHECK(b.vectorPower<.001f&&hypotf(b.gimbal[0].x,b.gimbal[0].y)<.001f&&hypotf(b.gimbal[1].x,b.gimbal[1].y)<.001f);
    }
}
static void test_roll(const FlightParams *p,const PathSystem *path)
{
    for(int side=-1;side<=1;side+=2){
        FlightState s;flight_reset(&s,p,path,0);advance(&s,p,path,(FlightInput){.boost=true},480);
        FlightInput in={.boost=true,.rollLeft=side<0,.rollRight=side>0};flight_step(&s,p,path,&in,dt);
        advance(&s,p,path,(FlightInput){.boost=true},20);
        CHECK(s.barrelT>0&&s.barrelT<1);
        CHECK((s.gimbal[1].x-s.gimbal[0].x)*side>.04f);
        CHECK(s.gimbal[0].x*s.gimbal[1].x<0&&fabsf(s.gimbal[0].y)<.001f&&fabsf(s.gimbal[1].y)<.001f);
        CHECK(fabsf(s.x)<.001f&&fabsf(s.y)<.001f);
        advance(&s,p,path,(FlightInput){.boost=true},36);
        // The differential changes sign during the second half to brake roll.
        CHECK((s.gimbal[1].x-s.gimbal[0].x)*side<0);
        advance(&s,p,path,(FlightInput){.boost=true},120);
        CHECK(s.barrelT<0&&fabsf(s.gimbal[0].x)<.001f&&fabsf(s.gimbal[1].x)<.001f);
    }
}
static void test_attachments(const FlightParams *p,const PathSystem *path)
{
    FlightState s;flight_reset(&s,p,path,0);advance(&s,p,path,(FlightInput){.boost=true},480);
    advance(&s,p,path,(FlightInput){.boost=true,.steerX=1,.steerY=1},60);
    for(int e=0;e<2;e++) {
        Vector2 a=s.gimbal[e];CHECK(hypotf(a.x,a.y)<=14*3.14159265f/180+.00001f);
        Vector3 lip=flight_exhaust_offset(e,a,0),tip=flight_exhaust_offset(e,a,3.08f);
        Vector3 jet=flight_gimbal_rotate((Vector3){0,0,-1},a);
        CHECK(fabsf(tip.x-lip.x-jet.x*3.08f)<.00001f&&fabsf(tip.y-lip.y-jet.y*3.08f)<.00001f&&fabsf(tip.z-lip.z-jet.z*3.08f)<.00001f);
        Vector3 rim=flight_gimbal_rotate((Vector3){.21f,0,0},a);
        CHECK(fabsf(sqrtf(rim.x*rim.x+rim.y*rim.y+rim.z*rim.z)-.21f)<.00001f);
    }
    FlightView mid=flight_interpolate(&(FlightState){0},&s,.5f);
    CHECK(fabsf(mid.gimbal[0].x-s.gimbal[0].x*.5f)<.00001f&&fabsf(mid.vectorPower-s.vectorPower*.5f)<.00001f);
    for(int e=0;e<2;e++) {
        int newest=(s.trailHead-1+FLIGHT_TRAIL_LEN)%FLIGHT_TRAIL_LEN;
        Vector3 local=flight_exhaust_offset(e,s.gimbal[e],.48f+2.6f*flight_speed_norm(p,s.speed));
        DVec3 anchor=dv_add(s.shipPos,dv_add(dv_add(dv_scale(s.shipRight,local.x),dv_scale(s.shipUp,local.y)),dv_scale(s.shipFwd,local.z)));
        CHECK(dv_len(dv_sub(anchor,s.trail[e][newest]))<.00001);
    }
}
static void test_fixed_steps(const FlightParams *p,const PathSystem *path)
{
    const double fps[]={30,60,144};FlightState result[3];
    for(int n=0;n<3;n++){
        FlightState previous;double acc=0;int steps=0;flight_reset(&result[n],p,path,0);previous=result[n];
        while(steps<240){FlightInput in={.boost=true,.steerX=.5f,.steerY=.25f};steps+=flight_advance(&previous,&result[n],p,path,&in,&acc,1/fps[n],240-steps);}
    }
    for(int n=1;n<3;n++){
        CHECK(result[n].x==result[0].x&&result[n].y==result[0].y&&result[n].speed==result[0].speed);
        CHECK(result[n].gimbal[0].x==result[0].gimbal[0].x&&result[n].gimbal[1].y==result[0].gimbal[1].y&&result[n].vectorPower==result[0].vectorPower);
    }
}
int main(void)
{
    ParamRegistry reg={0};FlightParams p;flight_register_params(&p,&reg);
    p.baseSpeed=32;p.boostSpeed=65;p.steerAccel=48;p.steerDamping=7;
    PathSystem path;path_init(&path,83,(PathStyle){.baseRadius=100,.radiusNoiseScale=60,.hasWalls=true});path_ensure(&path,0,2000);
    test_forces(&p,&path);test_roll(&p,&path);test_attachments(&p,&path);test_fixed_steps(&p,&path);path_free(&path);
    printf("thrust vectoring: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
