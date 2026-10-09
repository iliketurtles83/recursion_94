#include "render/shipmodel.h"
#include "core/flight.h"
#include <math.h>
#include <stdio.h>
int main(void)
{
    LineGeo hull={0},lights={0},nozzle={0},flame={0};int failures=0;
    shipmodel_build(&hull,&lights,5);shipmodel_flames(&flame);shipmodel_nozzles(&nozzle,0);
    int capacity=nozzle.vcount;float closed=0;
    for(int i=0;i<capacity;i++)if(nozzle.pos[i*3+2]<-1.7f){float x=nozzle.pos[i*3],y=nozzle.pos[i*3+1];float center=x>0?.48f:-.48f;closed=fmaxf(closed,hypotf(x-center,y+.05f));}
    shipmodel_nozzles(&nozzle,1);float open=0;
    if(capacity!=nozzle.vcount || nozzle.batchCount!=1)failures++;
    for(int i=0;i<nozzle.vcount;i++)if(nozzle.pos[i*3+2]<-1.7f){float x=nozzle.pos[i*3],y=nozzle.pos[i*3+1];float center=x>0?.48f:-.48f;open=fmaxf(open,hypotf(x-center,y+.05f));}
    if(open<closed*1.7f)failures++;
    int nozzleParts[2]={0};
    for(int i=0;i<nozzle.vcount;i++){
        float part=nozzle.uv2[i*2+1];
        if(fabsf(part)!=4)failures++;else nozzleParts[part>0]++;
    }
    if(!nozzleParts[0]||nozzleParts[0]!=nozzleParts[1])failures++;
    LineGeo *all[]={&hull,&lights,&nozzle,&flame};
    for(int g=0;g<4;g++){if(all[g]->batchCount!=1)failures++;for(int i=0;i<all[g]->vcount*3;i++)if(!isfinite(all[g]->pos[i])||!isfinite(all[g]->nrm[i]))failures++;}
    // Retain the prior geometry upgrade and keep within one indexed mesh.
    int bytes=(hull.vcount+nozzle.vcount+flame.vcount)*60+(hull.icount+nozzle.icount+flame.icount)*2;
    int addedBytes=bytes-2056728;
    if(bytes<2260680||hull.vcount>=LG_MAX_BATCH_VERTS)failures++;
    int parts[7]={0};
    for(int i=0;i<hull.vcount;i++) {
        int part=(int)hull.uv2[i*2+1];
        if(part<-3||part>3){failures++;continue;}parts[part+3]++;
        // The fuselage/canopy and engine pods must stay rigid.
        float x=hull.pos[i*3],z=hull.pos[i*3+2];
        if(z>1.1f&&part!=0)failures++;
        if(fabsf(x)<.20f&&part!=0)failures++;
    }
    for(int i=0;i<7;i++)if(!parts[i])failures++;
    for(int i=0;i<lights.vcount;i++)if(fabsf(lights.uv2[i*2+1])!=1)failures++;
    // Steering servos settle, reverse with the controls and return to neutral.
    ParamRegistry reg={0};FlightParams fp;flight_register_params(&fp,&reg);fp.autopilot=false;
    PathStyle style={12,.12f,60,14,20,.15f,.015f,true};PathSystem path;
    path_init(&path,42,style);path_ensure(&path,0,1000);
    FlightState a,b;flight_reset(&a,&fp,&path,0);b=a;
    for(int i=0;i<60;i++)flight_step(&b,&fp,&path,&(FlightInput){.steerX=1,.steerY=-1},1/120.0);
    if(b.controlX<.99f||b.controlY>-.99f)failures++;
    FlightView mid=flight_interpolate(&a,&b,.5f);
    if(fabsf(mid.controlX-b.controlX*.5f)>.0001f)failures++;
    for(int i=0;i<60;i++)flight_step(&b,&fp,&path,&(FlightInput){.steerX=-1,.steerY=1},1/120.0);
    if(b.controlX>-.99f||b.controlY<.99f)failures++;
    for(int i=0;i<120;i++)flight_step(&b,&fp,&path,&(FlightInput){0},1/120.0);
    if(fabsf(b.controlX)>.001f||fabsf(b.controlY)>.001f)failures++;
    path_free(&path);
    int coreVertices=0;
    for(int i=0;i<flame.vcount;i++)if(flame.uv2[i*2+1]>.5f)coreVertices++;
    if(!coreVertices||coreVertices==flame.vcount)failures++;
    printf("ship: %d hull vertices, %d added geometry bytes, six hinged surfaces, damped steering, layered exhaust, nozzle aperture %.3f -> %.3f, %d failures\n",hull.vcount,addedBytes,closed,open,failures);
    for(int g=0;g<4;g++)lg_free(all[g]);
    return failures?1:0;
}
