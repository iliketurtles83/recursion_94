#include "shipmodel.h"
#include "raymath.h"
#include <math.h>

static void face(LineGeo *g, Vector3 a, Vector3 b, Vector3 c, Vector3 d, Vector3 outward, Color color, float material)
{
    Vector3 n = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(b,a), Vector3Subtract(c,a)));
    if (Vector3DotProduct(n,outward)<0) { Vector3 t=b; b=d; d=t; n=Vector3Negate(n); }
    lg_fill_quad(g,a,b,c,d,n,(LineAttr){.color=color,.intensity=1,.group=material});
}
typedef struct Section { float z,w,h,y; } Section;
static float curved(float a,float b,float c,float d,float t)
{
    return .5f*((2*b)+(-a+c)*t+(2*a-5*b+4*c-d)*t*t+(-a+3*b-3*c+d)*t*t*t);
}
static void loft(LineGeo *g, const Section *s, int count, float cx, Color color, float material)
{
    Section profile[64];int sections=(count-1)*4+1;
    for(int j=0;j<sections;j++) {
        int i=j/4;if(i>=count-1){profile[j]=s[count-1];continue;}
        int lo=i?i-1:0,hi=i+2<count?i+2:count-1;float t=(j%4)/4.0f;
        profile[j]=(Section){s[i].z+(s[i+1].z-s[i].z)*t,
            fmaxf(.005f,curved(s[lo].w,s[i].w,s[i+1].w,s[hi].w,t)),
            fmaxf(.005f,curved(s[lo].h,s[i].h,s[i+1].h,s[hi].h,t)),
            curved(s[lo].y,s[i].y,s[i+1].y,s[hi].y,t)};
    }
    Vector3 ring[64][64],normal[64][64];
    for(int i=0;i<sections;i++)for(int k=0;k<64;k++) {
        float a=k*2*PI/64,sa=sinf(a),ca=cosf(a);
        Section p=profile[i];int lo=i?i-1:0,hi=i+1<sections?i+1:sections-1;
        float dz=profile[hi].z-profile[lo].z;
        float dw=(profile[hi].w-profile[lo].w)/dz,dh=(profile[hi].h-profile[lo].h)/dz,dy=(profile[hi].y-profile[lo].y)/dz;
        ring[i][k]=(Vector3){cx+p.w*sa,p.y+p.h*ca,p.z};
        normal[i][k]=Vector3Normalize((Vector3){sa/p.w,ca/p.h,-sa*sa*dw/p.w-ca*ca*dh/p.h-ca*dy/p.h});
    }
    for(int i=0;i<sections-1;i++)for(int k=0;k<64;k++) {
        int b=lg_reserve(g,4,6),next=(k+1)%64;
        const int si[4]={i,i+1,i+1,i},ki[4]={k,k,next,next};
        for(int j=0;j<4;j++)lg_vertex(g,ring[si[j]][ki[j]],(Vector2){0},(Vector2){material,0},normal[si[j]][ki[j]],(Vector4){0,1,0,0},color);
        lg_quad(g,b,0,1,2,3);
    }
    for(int end=0;end<2;end++) {
        int i=end?sections-1:0;
        Vector3 n={0,0,end?-1:1}, p={cx,profile[i].y,profile[i].z};
        Color cap=color;
        if(end&&material!=1){cap.r*=.44f;cap.g*=.44f;cap.b*=.44f;}
        for(int k=0;k<64;k++) {
            Vector3 a=ring[i][k],b=ring[i][(k+1)%64];
            if(!end) { Vector3 t=a;a=b;b=t; }
            lg_fill_tri(g,p,a,b,n,(LineAttr){.color=cap,.intensity=1,.group=end&&material!=1?2:material});
        }
    }
}
void shipmodel_build(LineGeo *hull, LineGeo *lights, float engineIntensity)
{
    lg_clear(hull); lg_clear(lights);
    const Section body[]={{2.05f,.025f,.025f,0},{1.75f,.12f,.065f,0},{1.20f,.24f,.14f,0},{.78f,.35f,.20f,0},{.30f,.43f,.26f,0},{-.15f,.42f,.27f,0},{-.72f,.36f,.24f,0},{-1.05f,.30f,.19f,0},{-1.37f,.24f,.14f,0}};
    const Section canopy[]={{1.08f,.018f,.015f,.20f},{.78f,.12f,.095f,.25f},{.38f,.225f,.17f,.29f},{.04f,.23f,.17f,.29f},{-.31f,.21f,.11f,.27f},{-.59f,.12f,.018f,.26f}};
    loft(hull,body,9,0,(Color){225,229,229,255},0);
    loft(hull,canopy,6,0,(Color){23,63,91,255},1);
    for(int side=-1;side<=1;side+=2) {
        Vector3 top[4]={{side*.25f,.01f,.90f},{side*1.48f,.01f,-.65f},{side*1.12f,.08f,-1.26f},{side*.23f,.10f,-.83f}},bottom[4];
        for(int i=0;i<4;i++) {bottom[i]=top[i];bottom[i].y-=.12f;}
        Vector3 inset[4];Vector3 middle={0};for(int i=0;i<4;i++)middle=Vector3Add(middle,Vector3Scale(top[i],.25f));
        for(int i=0;i<4;i++){inset[i]=Vector3Lerp(top[i],middle,.09f);inset[i].y+=.055f;}
        face(hull,inset[0],inset[1],inset[2],inset[3],(Vector3){0,1,0},(Color){217,221,224,255},0);
        for(int i=0;i<4;i++)face(hull,top[i],top[(i+1)%4],inset[(i+1)%4],inset[i],(Vector3){0,1,0},(Color){133,145,154,255},3);
        face(hull,bottom[0],bottom[1],bottom[2],bottom[3],(Vector3){0,-1,0},(Color){44,62,77,255},0);
        for(int i=0;i<4;i++) face(hull,top[i],bottom[i],bottom[(i+1)%4],top[(i+1)%4],(Vector3){side,0,0},(Color){61,87,106,255},0);
        const Section pod[]={{.10f,.10f,.10f,-.07f},{-.15f,.16f,.15f,-.06f},{-.50f,.21f,.20f,-.05f},{-.90f,.22f,.20f,-.05f},{-1.20f,.20f,.18f,-.05f},{-1.42f,.18f,.17f,-.05f}};
        loft(hull,pod,6,side*.48f,(Color){154,161,166,255},3);
        // Raised armor plates, recessed intake slats and canopy rails.
        face(hull,(Vector3){side*.36f,.125f,.22f},(Vector3){side*1.27f,.13f,-.69f},(Vector3){side*1.0f,.17f,-1.08f},(Vector3){side*.38f,.19f,-.71f},(Vector3){0,1,0},(Color){211,220,226,255},0);
        for(int k=0;k<7;k++) {
            float z=-.21f-k*.12f;
            face(hull,(Vector3){side*.36f,.185f,z},(Vector3){side*.60f,.185f,z-.015f},(Vector3){side*.60f,.19f,z-.05f},(Vector3){side*.36f,.19f,z-.045f},(Vector3){0,1,0},(Color){24,38,49,255},0);
        }
        for(int k=0;k<5;k++) {
            Section a=canopy[k],b=canopy[k+1];
            Vector3 p={side*a.w*.83f,a.y+a.h*.57f,a.z},q={side*b.w*.83f,b.y+b.h*.57f,b.z};
            face(hull,p,q,Vector3Add(q,(Vector3){side*.018f,.009f,0}),Vector3Add(p,(Vector3){side*.018f,.009f,0}),
                (Vector3){side,1,0},(Color){63,72,82,255},2);
        }
        lg_fill_tri(hull,(Vector3){side*1.0f,.07f,-.90f},(Vector3){side*1.13f,.44f,-1.22f},(Vector3){side*.82f,.12f,-1.23f},(Vector3){side,0,0},(LineAttr){.color={86,115,134,255},.intensity=1});
        LineAttr strip={.color={69,220,245,255},.intensity=.85f,.group=-1};
        lg_facing_segment(lights,(Vector3){side*.40f,.12f,.49f},(Vector3){side*1.16f,.13f,-.87f},.016f,.016f,.85f,.85f,strip);
        LineAttr nav={.color=side<0?(Color){255,86,70,255}:(Color){110,235,183,255},.intensity=1.5f,.group=-1};
        lg_facing_segment(lights,(Vector3){side*1.4f,.08f,-.65f},(Vector3){side*1.32f,.09f,-.80f},.025f,.025f,1.5f,1.5f,nav);
    }
    // Recessed aft service bulkhead replaces the bright flat tail cap.
    for(int k=0;k<5;k++) {
        float x=-.16f+k*.064f;
        face(hull,(Vector3){x,-.065f,-1.382f},(Vector3){x+.035f,-.065f,-1.382f},(Vector3){x+.035f,.065f,-1.382f},(Vector3){x,.065f,-1.382f},
            (Vector3){0,0,-1},(Color){38,45,55,255},2);
    }
}

void shipmodel_nozzles(LineGeo *g,float boost)
{
    lg_clear(g);boost=fminf(fmaxf(boost,0),1);
    float opening=.115f+.095f*boost;
    for(int side=-1;side<=1;side+=2)for(int k=0;k<24;k++) {
        float a=(k+.06f)*2*PI/24,b=(k+.94f)*2*PI/24;
        Vector3 p[4]={{side*.48f+.18f*cosf(a),-.05f+.18f*sinf(a),-1.40f},
                      {side*.48f+.18f*cosf(b),-.05f+.18f*sinf(b),-1.40f},
                      {side*.48f+opening*cosf(b),-.05f+opening*sinf(b),-1.78f},
                      {side*.48f+opening*cosf(a),-.05f+opening*sinf(a),-1.78f}};
        face(g,p[0],p[1],p[2],p[3],(Vector3){cosf(a),sinf(a),0},(Color){141,144,151,255},3);
        for(int j=0;j<4;j++){p[j].x=side*.48f+(p[j].x-side*.48f)*.85f;p[j].y=-.05f+(p[j].y+.05f)*.85f;}
        face(g,p[3],p[2],p[1],p[0],(Vector3){-cosf(a),-sinf(a),0},(Color){25,38,48,255},2);
    }
}
void shipmodel_flames(LineGeo *g)
{
    lg_clear(g);
    for(int side=-1;side<=1;side+=2)for(int z=0;z<16;z++)for(int k=0;k<32;k++) {
        int base=lg_reserve(g,4,6);const int dz[4]={0,0,1,1},da[4]={0,1,1,0};
        for(int j=0;j<4;j++) {
            float u=(z+dz[j])/16.0f,v=(k+da[j])/32.0f;
            lg_vertex(g,(Vector3){cosf(v*2*PI),sinf(v*2*PI),-u},(Vector2){u,v},(Vector2){side,0},(Vector3){cosf(v*2*PI),sinf(v*2*PI),0},(Vector4){0},WHITE);
        }
        lg_quad(g,base,0,1,2,3);
    }
}
