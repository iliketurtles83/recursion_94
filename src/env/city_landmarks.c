#include "city_landmarks.h"
#include "raymath.h"

static Vector3 point(PathSample p,DVec3 origin,float x,float y,float z)
{
    return dv_to_v3(dv_sub(dv_add(p.pos,dv_add(dv_add(dv_scale(p.B,x),dv_scale(p.N,y)),dv_scale(p.T,z))),origin));
}
static void box(PathSample p, ChunkCpu *c, Vector3 center, Vector3 half, float seed)
{
    Vector3 v[8];
    for(int i=0;i<8;i++) v[i]=point(p,c->origin,center.x+((i&1)?half.x:-half.x),center.y+((i&2)?half.y:-half.y),center.z+((i&4)?half.z:-half.z));
    const int q[6][4]={{0,4,6,2},{5,1,3,7},{1,0,2,3},{4,5,7,6},{2,6,7,3},{0,1,5,4}};
    Vector3 n[6]={dv_to_v3(dv_scale(p.B,-1)),dv_to_v3(p.B),dv_to_v3(dv_scale(p.T,-1)),dv_to_v3(p.T),dv_to_v3(p.N),dv_to_v3(dv_scale(p.N,-1))};
    for(int f=0;f<6;f++) {
        int b=lg_reserve(&c->geo[0],4,6);
        float width=f<2?half.z*2:half.x*2,height=f<4?half.y*2:half.z*2;
        const Vector2 uv[4]={{0,0},{width,0},{width,height},{0,height}};
        for(int k=0;k<4;k++) lg_vertex(&c->geo[0],v[q[f][k]],uv[k],(Vector2){seed,half.y*2},n[f],(Vector4){f<4?(float)f:4,0,0,0},(Color){180,195,210,255});
        lg_quad(&c->geo[0],b,0,1,2,3);
    }
    LineAttr a={.color={68,151,184,255},.intensity=.7f,.group=-1};
    for(int i=0;i<8;i++) for(int bit=1;bit<=4;bit*=2) if(!(i&bit)) lg_facing_segment(&c->geo[1],v[i],v[i|bit],.025f,.025f,a.intensity,a.intensity,a);
}
void city_landmarks_build(const PathSystem *path,int chunk,float floorDepth,ChunkCpu *out)
{
    double start=chunk*CHUNK_LEN;
    LineAttr a={.color={41,154,191,255},.intensity=.7f,.group=-1};
    for(int side=-1;side<=1;side+=2) for(int k=0;k<4;k++) {
        PathSample p=path_sample(path,start+k*12),q=path_sample(path,start+(k+1)*12);
        lg_facing_segment(&out->geo[1],point(p,out->origin,side*8,-floorDepth+.04f,0),point(q,out->origin,side*8,-floorDepth+.04f,0),.045f,.045f,.7f,.7f,a);
    }
    if(chunk%6!=2) return;
    PathSample p=path_sample(path,start+24);
    float height=chunk%12==2?17:26;
    for(int side=-1;side<=1;side+=2) {
        box(p,out,(Vector3){side*17,(height-floorDepth)*.5f,0},(Vector3){2.8f,(height+floorDepth)*.5f,4},.35f);
        box(p,out,(Vector3){side*17,height+2,0},(Vector3){4,2,5.5f},.71f);
    }
    box(p,out,(Vector3){0,height,0},(Vector3){17,1.2f,3.5f},.64f);
    // A continuous illuminated soffit makes the crossing readable at speed.
    lg_facing_segment(&out->geo[1],point(p,out->origin,-15,height-1.3f,-3.6f),point(p,out->origin,15,height-1.3f,-3.6f),.05f,.05f,1.6f,1.6f,a);
}
