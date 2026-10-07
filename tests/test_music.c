#include "game/techno.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int checks,failures;
#define CHECK(x) do{checks++;if(!(x)){failures++;printf("FAIL line %d: %s\n",__LINE__,#x);}}while(0)
static double exercise(Techno *m,int seconds,float drive)
{
    float block[1024*2];double sum=0;float peak=0;
    techno_control(m,drive,true,false,false);
    int remaining=seconds*TECHNO_RATE;
    while(remaining>0){int n=remaining>1024?1024:remaining;techno_render(m,block,n);remaining-=n;
        for(int i=0;i<n*2;i++){if(!isfinite(block[i])){CHECK(false);return 0;}peak=fmaxf(peak,fabsf(block[i]));sum+=block[i]*block[i];}}
    CHECK(peak<.99f && peak>.2f);return sqrt(sum/(seconds*TECHNO_RATE*2));
}
int main(void)
{
    Techno *base=techno_create(83),*boost=techno_create(83);CHECK(base&&boost);if(!base||!boost)return 1;
    double a=exercise(base,8,0),b=exercise(boost,8,1);
    CHECK(techno_bpm(base)==TECHNO_CRUISE_BPM && techno_bpm(boost)>TECHNO_BOOST_BPM-.2f);
    CHECK(a>.04 && b>.04);printf("techno: base %.1f BPM RMS %.3f, boost %.1f BPM RMS %.3f\n",techno_bpm(base),a,techno_bpm(boost),b);
    float x[4096*2],y[4096*2];uint64_t before=techno_frames(boost);
    techno_control(boost,1,true,true,false);techno_render(boost,x,4096);
    CHECK(techno_frames(boost)==before);memset(y,0,sizeof(y));CHECK(!memcmp(x,y,sizeof(x)));
    techno_control(boost,1,true,false,true);techno_render(boost,x,4096);CHECK(!memcmp(x,y,sizeof(x)));CHECK(techno_frames(boost)==before+4096);
    techno_control(boost,1,true,false,false);techno_render(boost,x,4096);CHECK(memcmp(x,y,sizeof(x))!=0);
    techno_destroy(base);techno_destroy(boost);
    // Device buffer size must not change the composition or add boundary clicks.
    base=techno_create(17);boost=techno_create(17);techno_control(base,.6f,true,false,false);techno_control(boost,.6f,true,false,false);
    techno_render(base,x,4096);for(int i=0;i<4096;i+=128)techno_render(boost,y+i*2,128);
    CHECK(!memcmp(x,y,sizeof(x)));
    techno_destroy(base);techno_destroy(boost);
    printf("music: %d checks, %d failures\n",checks,failures);return failures?1:0;
}
