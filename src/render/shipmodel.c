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
static void tag_part(LineGeo *g,int first,float part)
{
    for(int i=first;i<g->vcount;i++)g->uv2[i*2+1]=part;
}
// Beveled, closed control surfaces retain their thickness while deflected.
static void plate(LineGeo *g,const Vector3 p[4],Vector3 thickness,Color paint)
{
    Vector3 back[4],inset[4],center={0},n=Vector3Normalize(thickness);
    for(int i=0;i<4;i++)center=Vector3Add(center,Vector3Scale(p[i],.25f));
    for(int i=0;i<4;i++){
        back[i]=Vector3Subtract(p[i],thickness);
        inset[i]=Vector3Add(Vector3Lerp(p[i],center,.075f),Vector3Scale(thickness,.12f));
    }
    face(g,inset[0],inset[1],inset[2],inset[3],n,paint,0);
    face(g,back[0],back[1],back[2],back[3],Vector3Negate(n),(Color){52,66,75,255},2);
    for(int i=0;i<4;i++){
        int k=(i+1)%4;
        face(g,p[i],p[k],inset[k],inset[i],n,(Color){165,176,181,255},3);
        face(g,p[i],back[i],back[k],p[k],Vector3Subtract(p[i],center),(Color){82,95,105,255},3);
    }
}
// Small solid tubes retain a silhouette and specular highlight at close range.
static void tube(LineGeo *g,const Vector3 *p,int count,int sides,float radius,Color color,float material)
{
    Vector3 ring[16][8];
    for(int i=0;i<count;i++) {
        Vector3 tangent=Vector3Normalize(Vector3Subtract(p[i+1<count?i+1:i],p[i?i-1:0]));
        Vector3 ref=fabsf(tangent.z)>.9f?(Vector3){0,1,0}:(Vector3){0,0,1};
        Vector3 u=Vector3Normalize(Vector3CrossProduct(tangent,ref)),v=Vector3CrossProduct(tangent,u);
        for(int k=0;k<sides;k++)ring[i][k]=Vector3Add(p[i],Vector3Scale(Vector3Add(Vector3Scale(u,cosf(k*2*PI/sides)),Vector3Scale(v,sinf(k*2*PI/sides))),radius));
    }
    for(int i=0;i<count-1;i++)for(int k=0;k<sides;k++) {
        int next=(k+1)%sides;
        face(g,ring[i][k],ring[i][next],ring[i+1][next],ring[i+1][k],Vector3Subtract(ring[i][k],p[i]),color,material);
    }
    for(int end=0;end<2;end++) {
        int i=end?count-1:0;
        Vector3 n=Vector3Normalize(Vector3Subtract(p[i],p[end?i-1:1]));
        for(int k=0;k<sides;k++) {
            Vector3 a=ring[i][k],b=ring[i][(k+1)%sides];
            if(Vector3DotProduct(Vector3CrossProduct(Vector3Subtract(a,p[i]),Vector3Subtract(b,p[i])),n)<0){Vector3 t=a;a=b;b=t;}
            lg_fill_tri(g,p[i],a,b,n,(LineAttr){.color=color,.intensity=1,.group=material});
        }
    }
}
static void radiator(LineGeo *g,float cx,float z)
{
    for(int k=0;k<16;k++) {
        float a=k*2*PI/16,b=(k+1)*2*PI/16;
        Vector3 p[8];
        for(int j=0;j<8;j++) {
            float angle=(j&1)?b:a,r=(j&2)?.202f:.231f;
            p[j]=(Vector3){cx+r*cosf(angle),-.05f+r*sinf(angle),z+((j&4)?-.018f:0)};
        }
        face(g,p[0],p[1],p[5],p[4],(Vector3){cosf(a),sinf(a),0},(Color){95,113,123,255},3);
        face(g,p[0],p[2],p[3],p[1],(Vector3){0,0,1},(Color){123,133,137,255},3);
        face(g,p[4],p[5],p[7],p[6],(Vector3){0,0,-1},(Color){64,78,87,255},3);
    }
}
static void collar(LineGeo *g,float cx,float z,float radius)
{
    for(int k=0;k<24;k++) {
        float a=k*2*PI/24,b=(k+1)*2*PI/24;
        Vector3 p[8];
        for(int j=0;j<8;j++) {
            float angle=(j&1)?b:a,r=(j&2)?radius-.018f:radius;
            p[j]=(Vector3){cx+r*cosf(angle),-.05f+r*sinf(angle),z+((j&4)?-.055f:0)};
        }
        Vector3 radial={cosf((a+b)*.5f),sinf((a+b)*.5f),0};
        face(g,p[0],p[1],p[5],p[4],radial,(Color){174,164,145,255},3);
        face(g,p[2],p[6],p[7],p[3],Vector3Negate(radial),(Color){35,43,50,255},2);
        face(g,p[0],p[2],p[3],p[1],(Vector3){0,0,1},(Color){107,119,130,255},3);
        face(g,p[4],p[5],p[7],p[6],(Vector3){0,0,-1},(Color){152,162,170,255},3);
    }
}
static void fastener(LineGeo *g,float x,float y,float z)
{
    for(int k=0;k<8;k++) {
        float a=k*2*PI/8,b=(k+1)*2*PI/8;
        Vector3 p={x+.013f*cosf(a),y,z+.013f*sinf(a)},q={x+.013f*cosf(b),y,z+.013f*sinf(b)};
        Vector3 topP=Vector3Add(p,(Vector3){0,.006f,0}),topQ=Vector3Add(q,(Vector3){0,.006f,0});
        face(g,p,q,topQ,topP,(Vector3){cosf(a),0,sinf(a)},(Color){91,102,109,255},3);
        lg_fill_tri(g,(Vector3){x,y+.006f,z},topQ,topP,(Vector3){0,1,0},(LineAttr){.color={188,192,190,255},.intensity=1,.group=3});
    }
    for(int axis=0;axis<2;axis++) {
        float sx=axis?.002f:.008f,sz=axis?.008f:.002f;
        face(g,(Vector3){x-sx,y+.0065f,z-sz},(Vector3){x+sx,y+.0065f,z-sz},(Vector3){x+sx,y+.0065f,z+sz},(Vector3){x-sx,y+.0065f,z+sz},
            (Vector3){0,1,0},(Color){20,28,35,255},2);
    }
}
static void service_hatch(LineGeo *g,float x,float y,float z)
{
    Vector3 edge[4]={{x-.115f,y,z-.135f},{x+.115f,y,z-.135f},{x+.115f,y,z+.135f},{x-.115f,y,z+.135f}},top[4];
    for(int k=0;k<4;k++)top[k]=(Vector3){x+(edge[k].x-x)*.86f,y+.021f,z+(edge[k].z-z)*.86f};
    face(g,top[0],top[1],top[2],top[3],(Vector3){0,1,0},(Color){125,146,157,255},3);
    for(int k=0;k<4;k++)face(g,edge[k],edge[(k+1)%4],top[(k+1)%4],top[k],(Vector3){0,1,0},(Color){61,76,87,255},3);
    for(int side=-1;side<=1;side+=2)fastener(g,x+side*.075f,y+.021f,z+.075f);
    // Recessed cooling louvers in the service panel, with raised metal lips.
    for(int k=0;k<4;k++) {
        float vz=z-.084f+k*.037f;
        face(g,(Vector3){x-.073f,y+.022f,vz},(Vector3){x+.073f,y+.022f,vz},(Vector3){x+.073f,y+.022f,vz+.018f},(Vector3){x-.073f,y+.022f,vz+.018f},(Vector3){0,1,0},(Color){17,27,35,255},2);
        face(g,(Vector3){x-.073f,y+.026f,vz+.018f},(Vector3){x+.073f,y+.026f,vz+.018f},(Vector3){x+.073f,y+.022f,vz+.023f},(Vector3){x-.073f,y+.022f,vz+.023f},(Vector3){0,1,0},(Color){169,177,178,255},3);
    }
}
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
    for(int arch=0;arch<2;arch++) {
        Vector3 p[13];float width=arch?.218f:.225f,z=arch?-.20f:.32f;
        for(int k=0;k<13;k++){float a=k*PI/12;p[k]=(Vector3){width*cosf(a),.29f+.175f*sinf(a),z};}
        tube(hull,p,13,8,.009f,(Color){104,126,137,255},3);
    }
    for(int side=-1;side<=1;side+=2) {
        int first=hull->vcount;
        Vector3 top[4]={{side*.25f,.01f,.90f},{side*1.48f,.01f,-.65f},{side*1.12f,.08f,-1.26f},{side*.23f,.10f,-.83f}},bottom[4];
        for(int i=0;i<4;i++) {bottom[i]=top[i];bottom[i].y-=.12f;}
        Vector3 inset[4];Vector3 middle={0};for(int i=0;i<4;i++)middle=Vector3Add(middle,Vector3Scale(top[i],.25f));
        for(int i=0;i<4;i++){inset[i]=Vector3Lerp(top[i],middle,.09f);inset[i].y+=.055f;}
        face(hull,inset[0],inset[1],inset[2],inset[3],(Vector3){0,1,0},(Color){217,221,224,255},0);
        for(int i=0;i<4;i++)face(hull,top[i],top[(i+1)%4],inset[(i+1)%4],inset[i],(Vector3){0,1,0},(Color){133,145,154,255},3);
        face(hull,bottom[0],bottom[1],bottom[2],bottom[3],(Vector3){0,-1,0},(Color){44,62,77,255},0);
        for(int i=0;i<4;i++) face(hull,top[i],bottom[i],bottom[(i+1)%4],top[(i+1)%4],(Vector3){side,0,0},(Color){61,87,106,255},0);
        tag_part(hull,first,(float)side);
        const Section pod[]={{.10f,.10f,.10f,-.07f},{-.15f,.16f,.15f,-.06f},{-.50f,.21f,.20f,-.05f},{-.90f,.22f,.20f,-.05f},{-1.20f,.20f,.18f,-.05f},{-1.42f,.18f,.17f,-.05f}};
        loft(hull,pod,6,side*.48f,(Color){154,161,166,255},3);
        collar(hull,side*.48f,-1.24f,.205f);
        for(int k=0;k<5;k++)radiator(hull,side*.48f,-.46f-k*.12f);
        Vector3 hose[9];
        for(int k=0;k<9;k++) {
            float t=k/8.0f;
            hose[k]=(Vector3){side*(.64f-.33f*t),.14f+.10f*sinf(t*PI),-1.08f+.43f*t};
        }
        tube(hull,hose,9,6,.012f,(Color){138,116,83,255},3);
        // Raised armor plates, recessed intake slats and canopy rails.
        first=hull->vcount;
        face(hull,(Vector3){side*.36f,.125f,.22f},(Vector3){side*1.27f,.13f,-.69f},(Vector3){side*1.0f,.17f,-1.08f},(Vector3){side*.38f,.19f,-.71f},(Vector3){0,1,0},(Color){211,220,226,255},0);
        service_hatch(hull,side*.88f,.165f,-.62f);
        service_hatch(hull,side*.77f,.195f,-.95f);
        // Flush rivet rows, panel seams, and bolted wing-root pivot hardware.
        for(int k=0;k<8;k++)fastener(hull,side*(.51f+k*.085f),.151f,-.015f-k*.092f);
        for(int k=0;k<3;k++) {
            Vector3 seam[3]={{side*(.53f+k*.10f),.160f,-.26f-k*.20f},
                {side*(.81f+k*.06f),.172f,-.55f-k*.15f},{side*(.99f-k*.04f),.181f,-.77f-k*.11f}};
            tube(hull,seam,3,6,.0028f,(Color){30,41,48,255},2);
        }
        Vector3 rootPin[2]={{side*.285f,.085f,.24f},{side*.285f,.085f,-.70f}};
        tube(hull,rootPin,2,8,.038f,(Color){128,145,155,255},3);
        Vector3 flapPin[2]={{side*.72f,.13f,-1.08f},{side*1.12f,.13f,-1.08f}};
        tube(hull,flapPin,2,8,.022f,(Color){159,167,164,255},3);
        Vector3 finPin[2]={{side*1.01f,.10f,-.90f},{side*1.01f,.10f,-1.32f}};
        tube(hull,finPin,2,8,.025f,(Color){132,145,153,255},3);
        tag_part(hull,first,(float)side);
        first=hull->vcount;
        Vector3 flap[4]={{side*.73f,.13f,-1.09f},{side*1.10f,.13f,-1.09f},
            {side*1.00f,.13f,-1.47f},{side*.72f,.13f,-1.35f}};
        plate(hull,flap,(Vector3){0,.035f,0},(Color){195,207,216,255});
        for(int k=0;k<3;k++)fastener(hull,side*(.77f+k*.10f),.140f,-1.17f);
        Vector3 actuator[2]={{side*.82f,.147f,-1.10f},{side*.82f,.148f,-1.29f}};
        tube(hull,actuator,2,8,.014f,(Color){172,155,115,255},3);
        tag_part(hull,first,side*3.0f);
        first=hull->vcount;
        Vector3 winglet[4]={{side*.94f,.10f,-.88f},{side*1.05f,.52f,-1.15f},
            {side*1.08f,.50f,-1.32f},{side*.94f,.10f,-1.32f}};
        plate(hull,winglet,(Vector3){side*.035f,0,0},(Color){133,162,182,255});
        // Recessed hinge cover and a solid metallic spine on each winglet.
        Vector3 finSpine[3]={{side*.958f,.15f,-1.23f},{side*1.021f,.35f,-1.23f},{side*1.06f,.48f,-1.23f}};
        tube(hull,finSpine,3,6,.012f,(Color){73,94,109,255},3);
        tag_part(hull,first,side*2.0f);
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
        LineAttr strip={.color={69,220,245,255},.intensity=.85f,.group=-1,.anim=(float)side};
        lg_facing_segment(lights,(Vector3){side*.40f,.12f,.49f},(Vector3){side*1.16f,.13f,-.87f},.016f,.016f,.85f,.85f,strip);
        LineAttr nav={.color=side<0?(Color){255,86,70,255}:(Color){110,235,183,255},.intensity=1.5f,.group=-1,.anim=(float)side};
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
        Vector3 lipA=p[3],lipB=p[2];
        for(int j=0;j<4;j++){p[j].x=side*.48f+(p[j].x-side*.48f)*.85f;p[j].y=-.05f+(p[j].y+.05f)*.85f;}
        face(g,p[3],p[2],p[1],p[0],(Vector3){-cosf(a),-sinf(a),0},(Color){25,38,48,255},2);
        face(g,lipA,lipB,p[2],p[3],(Vector3){0,0,-1},(Color){111,115,117,255},4);
        if(k%3==0) {
            float angle=(a+b)*.5f,r=.16f+.05f*boost;
            Vector3 across={-sinf(angle)*.014f,cosf(angle)*.014f,0},out={cosf(angle)*.006f,sinf(angle)*.006f,0};
            Vector3 hinge={side*.48f+.197f*cosf(angle),-.05f+.197f*sinf(angle),-1.45f};
            Vector3 tip={side*.48f+r*cosf(angle),-.05f+r*sinf(angle),-1.67f};
            Vector3 h1=Vector3Subtract(hinge,across),h2=Vector3Add(hinge,across),t1=Vector3Subtract(tip,across),t2=Vector3Add(tip,across);
            face(g,h1,h2,t2,t1,(Vector3){cosf(angle),sinf(angle),0},(Color){177,170,147,255},3);
            face(g,h1,t1,Vector3Add(t1,out),Vector3Add(h1,out),Vector3Negate(across),(Color){69,78,86,255},3);
            face(g,h2,Vector3Add(h2,out),Vector3Add(t2,out),t2,across,(Color){69,78,86,255},3);
        }
    }
}
void shipmodel_flames(LineGeo *g)
{
    lg_clear(g);
    for(int layer=0;layer<2;layer++)for(int side=-1;side<=1;side+=2)for(int z=0;z<(layer?8:16);z++)for(int k=0;k<(layer?16:32);k++) {
        int base=lg_reserve(g,4,6);const int dz[4]={0,0,1,1},da[4]={0,1,1,0};
        for(int j=0;j<4;j++) {
            float u=(z+dz[j])/(layer?8.0f:16.0f),v=(k+da[j])/(layer?16.0f:32.0f);
            lg_vertex(g,(Vector3){cosf(v*2*PI),sinf(v*2*PI),-u},(Vector2){u,v},(Vector2){side,(float)layer},(Vector3){cosf(v*2*PI),sinf(v*2*PI),0},(Vector4){0},WHITE);
        }
        lg_quad(g,base,0,1,2,3);
    }
}
