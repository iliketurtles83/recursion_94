#include "render/shipmodel.h"
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
    LineGeo *all[]={&hull,&lights,&nozzle,&flame};
    for(int g=0;g<4;g++){if(all[g]->batchCount!=1)failures++;for(int i=0;i<all[g]->vcount*3;i++)if(!isfinite(all[g]->pos[i])||!isfinite(all[g]->nrm[i]))failures++;}
    printf("ship: %d hull vertices, nozzle aperture %.3f -> %.3f, %d failures\n",hull.vcount,closed,open,failures);
    for(int g=0;g<4;g++)lg_free(all[g]);
    return failures?1:0;
}
