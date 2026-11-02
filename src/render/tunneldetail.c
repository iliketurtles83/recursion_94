#include "tunneldetail.h"
#include "raymath.h"
#include <math.h>
#define TAU 6.28318530718f
static Vector3 point(const PathSystem *path,DVec3 origin,double s,float a,float radius)
{
    PathSample p=path_sample(path,s);
    DVec3 radial=dv_add(dv_scale(p.B,cosf(a)),dv_scale(p.N,sinf(a)));
    return dv_to_v3(dv_sub(dv_madd(p.pos,radial,p.radius*radius),origin));
}
static Vector3 radial(const PathSystem *path,double s,float a)
{
    PathSample p=path_sample(path,s);
    return dv_to_v3(dv_add(dv_scale(p.B,cosf(a)),dv_scale(p.N,sinf(a))));
}
static void quad(LineGeo *g,Vector3 a,Vector3 b,Vector3 c,Vector3 d,Vector3 out,float part)
{
    Vector3 n=Vector3Normalize(Vector3CrossProduct(Vector3Subtract(b,a),Vector3Subtract(c,a)));
    if(Vector3DotProduct(n,out)<0){Vector3 t=b;b=d;d=t;n=Vector3Negate(n);}
    lg_fill_quad(g,a,b,c,d,n,(LineAttr){.color=WHITE,.intensity=1,.anim=part});
}
static void surface(LineGeo *g,const PathSystem *path,DVec3 origin,double s,double end,float a,float b,float r,float part)
{
    quad(g,point(path,origin,s,a,r),point(path,origin,s,b,r),point(path,origin,end,b,r),point(path,origin,end,a,r),Vector3Negate(radial(path,(s+end)*.5f,(a+b)*.5f)),part);
}
static void rib(LineGeo *g,const PathSystem *path,DVec3 origin,double s,float a,float b,int segments,float inner,float width,float part)
{
    for(int k=0;k<segments;k++) {
        float a0=a+(b-a)*k/segments,a1=a+(b-a)*(k+1)/segments;
        Vector3 p[8];
        for(int j=0;j<8;j++)p[j]=point(path,origin,s+((j&4)?width:0),(j&1)?a1:a0,(j&2)?inner:1.248f);
        Vector3 t=dv_to_v3(path_sample(path,s).T),r=radial(path,s,(a0+a1)*.5f);
        quad(g,p[0],p[2],p[3],p[1],Vector3Negate(t),part);
        quad(g,p[4],p[5],p[7],p[6],t,part);
        quad(g,p[2],p[6],p[7],p[3],Vector3Negate(r),part);
    }
}
void tunnel_detail_build(LineGeo *g,const PathSystem *path,DVec3 origin,double s0)
{
    // Two modules per 48 m chunk, eight partial wall panels per module.
    // 3,328 vertices / 4,992 indices = 209,664 additional mesh bytes.
    for(int band=0;band<2;band++)for(int panel=0;panel<8;panel++) {
        double start=s0+3+band*24;
        float a=(panel+.10f)*TAU/8,b=a+.384f*TAU/8;
        rib(g,path,origin,start+.3,a,b,6,1.208f,.22f,1);
        for(int end=0;end<2;end++) {
            float angle=end?b:a;
            PathSample p=path_sample(path,start);
            Vector3 out=dv_to_v3(dv_add(dv_scale(p.B,-sinf(angle)),dv_scale(p.N,cosf(angle))));
            if(!end)out=Vector3Negate(out);
            quad(g,point(path,origin,start+.3,angle,1.248f),point(path,origin,start+.3,angle,1.208f),point(path,origin,start+.52,angle,1.208f),point(path,origin,start+.52,angle,1.248f),out,1);
        }
        float va=a+.042f,vb=b-.064f;
        surface(g,path,origin,start+7,start+11.5,va,vb,1.246f,2);
        for(int k=0;k<5;k++)rib(g,path,origin,start+7.4+k*.72,va+.012f,vb-.012f,1,1.228f,.26f,3);
        // Four beveled sides frame the recessed ventilation bank.
        Vector3 q[4]={point(path,origin,start+7,va,1.248f),point(path,origin,start+7,vb,1.248f),point(path,origin,start+11.5,vb,1.248f),point(path,origin,start+11.5,va,1.248f)};
        Vector3 inset[4]={point(path,origin,start+7.2,va+.012f,1.233f),point(path,origin,start+7.2,vb-.012f,1.233f),point(path,origin,start+11.3,vb-.012f,1.233f),point(path,origin,start+11.3,va+.012f,1.233f)};
        for(int k=0;k<4;k++)quad(g,q[k],q[(k+1)%4],inset[(k+1)%4],inset[k],Vector3Negate(radial(path,start+9,a)),1);
        // A hexagonal conduit with enlarged coupling ends follows the bend.
        Vector3 pipe[3][6];float angle=b-.025f;
        for(int z=0;z<3;z++) {
            double s=start+3+z*5;
            PathSample p=path_sample(path,s);
            Vector3 center=point(path,origin,s,angle,1.231f),u=radial(path,s,angle),v=dv_to_v3(dv_add(dv_scale(p.B,-sinf(angle)),dv_scale(p.N,cosf(angle))));
            float radius=z==1?.11f:.16f;
            for(int k=0;k<6;k++)pipe[z][k]=Vector3Add(center,Vector3Scale(Vector3Add(Vector3Scale(u,cosf(k*TAU/6)),Vector3Scale(v,sinf(k*TAU/6))),radius));
        }
        for(int z=0;z<2;z++)for(int k=0;k<6;k++) {
            Vector3 center=point(path,origin,start+3+z*5,angle,1.231f);
            quad(g,pipe[z][k],pipe[z][(k+1)%6],pipe[z+1][(k+1)%6],pipe[z+1][k],Vector3Subtract(pipe[z][k],center),4);
        }
    }
}
