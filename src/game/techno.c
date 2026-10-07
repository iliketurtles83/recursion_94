#include "techno.h"
#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define TAU 6.283185307179586f
#define DELAY 24000
struct Techno {
    atomic_uint control, bpm;
    atomic_uint_fast64_t frames;
    uint32_t noise;
    double beat;
    float drive, gain, kickTime, kickPhase, hatTime, clapTime, bassTime, leadTime, chordTime;
    float bassPhase, leadPhase[2], chordPhase[3], bassHz, leadHz, chordHz[3];
    float bassFilter, leadFilter[2], noiseLow, hatLow, delay[2][DELAY];
    int step, delayHead;
};
static float bound(float x,float lo,float hi){return fminf(fmaxf(x,lo),hi);}
static float note(int n){return 440.0f*powf(2.0f,(n-69)/12.0f);}
static float random_bipolar(Techno *m){uint32_t x=m->noise;x^=x<<13;x^=x>>17;x^=x<<5;m->noise=x;return (x>>8)*(2.0f/16777216.0f)-1;}
static float blep(float p,float d){if(p<d){p/=d;return p+p-p*p-1;}if(p>1-d){p=(p-1)/d;return p*p+p+p+1;}return 0;}
static float saw(float *phase,float hz){float d=hz/TECHNO_RATE;*phase+=d;if(*phase>=1)*phase-=1;return 2*(*phase)-1-blep(*phase,d);}
static float envelope(float t,float attack,float decay){return bound(t/attack,0,1)*expf(-t/decay);}
static float limit(float x){return x/(1.0f+.6f*fabsf(x));}

Techno *techno_create(uint64_t seed)
{
    Techno *m=calloc(1,sizeof(*m));if(!m)return NULL;
    atomic_init(&m->control,0);atomic_init(&m->bpm,TECHNO_CRUISE_BPM*10);atomic_init(&m->frames,0);
    m->noise=(uint32_t)(seed^(seed>>32)^0x9e3779b9u);if(!m->noise)m->noise=1;
    m->step=-1;m->kickTime=m->hatTime=m->clapTime=m->bassTime=m->leadTime=m->chordTime=10;
    m->bassHz=note(29);m->leadHz=note(65);
    for(int i=0;i<3;i++)m->chordHz[i]=note(53+i*3);
    return m;
}
void techno_destroy(Techno *m){free(m);}
void techno_control(Techno *m,float speed,bool running,bool paused,bool muted)
{
    if(m)atomic_store_explicit(&m->control,(unsigned int)(bound(speed,0,1)*4095)+(running?4096:0)+(paused?8192:0)+(muted?16384:0),memory_order_relaxed);
}
float techno_bpm(const Techno *m){return m?atomic_load_explicit(&m->bpm,memory_order_relaxed)*.1f:TECHNO_CRUISE_BPM;}
uint64_t techno_frames(const Techno *m){return m?atomic_load_explicit(&m->frames,memory_order_relaxed):0;}
void techno_render(Techno *m,float *out,unsigned int frames)
{
    if(!m){memset(out,0,frames*2*sizeof(float));return;}
    unsigned int control=atomic_load_explicit(&m->control,memory_order_relaxed);
    if(control&8192){memset(out,0,frames*2*sizeof(float));return;}
    float target=(control&4095)/4095.0f,active=(control&4096)?1:.35f;
    const float dt=1.0f/TECHNO_RATE;
    // F minor / Db / Ab / Eb; a 16-bar arrangement with fills and breakdowns.
    const int roots[4]={29,25,32,27};
    const int acid[16]={0,0,12,7,0,3,10,7,0,12,3,7,10,7,3,12};
    const int arp[16]={0,7,12,15,7,12,19,15,0,7,10,15,12,7,19,22};
    for(unsigned int i=0;i<frames;i++) {
        m->drive+=(target-m->drive)*(1-expf(-dt/1.2f));
        m->gain+=(active-m->gain)*(1-expf(-dt/.05f));
        float bpm=TECHNO_CRUISE_BPM+(TECHNO_BOOST_BPM-TECHNO_CRUISE_BPM)*m->drive;
        int absoluteStep=(int)(m->beat*4),step=absoluteStep&15,bar=(absoluteStep/16)&15;
        int root=roots[(absoluteStep/32)&3];
        bool breakdown=bar==7||bar==15;
        if(absoluteStep!=m->step) {
            m->step=absoluteStep;
            if(step%4==0 && (!breakdown||step==0||m->drive>.65f)){m->kickTime=0;m->kickPhase=0;}
            m->hatTime=0;
            if(step==4||step==12||((bar==3||bar==11)&&step>=14))m->clapTime=0;
            m->bassTime=0;m->bassHz=note(root+acid[step]);
            m->leadTime=0;m->leadHz=note(root+36+arp[(step+bar*2)&15]);
            if(step==2||step==8||step==14){m->chordTime=0;m->chordHz[0]=note(root+24);m->chordHz[1]=note(root+27);m->chordHz[2]=note(root+31);}
        }
        float beatFrac=(float)(m->beat-floor(m->beat));
        float duck=.32f+.68f*(1-expf(-beatFrac*8));
        float noise=random_bipolar(m);m->hatLow+=(noise-m->hatLow)*.18f;
        float bright=noise-m->hatLow;
        float kt=m->kickTime;
        m->kickPhase+=TAU*(47+115*expf(-kt*36))*dt;
        float kick=(sinf(m->kickPhase)*expf(-kt*12)+bright*.12f*expf(-kt*170))*.70f;
        float offbeat=(step%4==2)?1:0;
        float hat=bright*envelope(m->hatTime,.0007f,offbeat?.115f:.025f)*(.05f+.07f*m->drive+offbeat*.035f);
        float ct=m->clapTime;
        float clap=bright*(expf(-ct*27)+.65f*expf(-fabsf(ct-.013f)*180)+.5f*expf(-fabsf(ct-.028f)*160))*envelope(ct,.0008f,.18f)*.14f;
        float bassSaw=saw(&m->bassPhase,m->bassHz);
        float bassEnv=envelope(m->bassTime,.003f,.115f);
        float cutoff=180+(600+1800*m->drive)*bassEnv*(.75f+.25f*sinf((float)m->beat*.3f));
        m->bassFilter+=(bassSaw-m->bassFilter)*(1-expf(-TAU*cutoff*dt));
        float bass=(m->bassFilter*.12f+sinf(TAU*m->bassPhase)*.17f)*bassEnv*duck;
        float leadEnv=envelope(m->leadTime,.002f,.07f)*(.015f+.075f*m->drive)*duck;
        float lead[2];
        for(int ch=0;ch<2;ch++) {
            float source=saw(&m->leadPhase[ch],m->leadHz*(ch?.998f:1.002f));
            m->leadFilter[ch]+=(source-m->leadFilter[ch])*(1-expf(-TAU*(700+3400*m->drive)*dt));
            lead[ch]=m->leadFilter[ch]*leadEnv;
        }
        float chord=0;
        for(int n=0;n<3;n++)chord+=saw(&m->chordPhase[n],m->chordHz[n]*(1+(n-1)*.002f));
        chord*=envelope(m->chordTime,.009f,.20f)*(.018f+.025f*m->drive)*duck;
        float lift=(bar%4==3)?bound((step-8)/8.0f,0,1):0;
        m->noiseLow+=(noise-m->noiseLow)*.025f;
        float riser=(noise-m->noiseLow)*lift*m->drive*.025f;
        float center=kick+clap+bass;
        int tap=(m->delayHead+DELAY-(int)(TECHNO_RATE*60/bpm*.75f))%DELAY;
        float echoL=m->delay[0][tap],echoR=m->delay[1][tap];
        m->delay[0][m->delayHead]=lead[0]+chord+echoR*.35f;
        m->delay[1][m->delayHead]=lead[1]+chord+echoL*.35f;
        bool muted=(control&16384)!=0;
        out[i*2]=muted?0:limit((center+hat*.8f+lead[0]+chord+echoL*.28f+riser)*m->gain);
        out[i*2+1]=muted?0:limit((center+hat+lead[1]+chord+echoR*.28f-riser*.5f)*m->gain);
        m->delayHead=(m->delayHead+1)%DELAY;
        m->beat+=bpm/(60.0*TECHNO_RATE);
        m->kickTime+=dt;m->hatTime+=dt;m->clapTime+=dt;m->bassTime+=dt;m->leadTime+=dt;m->chordTime+=dt;
    }
    atomic_store_explicit(&m->bpm,(unsigned int)((TECHNO_CRUISE_BPM+(TECHNO_BOOST_BPM-TECHNO_CRUISE_BPM)*m->drive)*10+.5f),memory_order_relaxed);
    atomic_fetch_add_explicit(&m->frames,frames,memory_order_relaxed);
}
static void le(FILE *f,uint32_t v,int bytes){for(int i=0;i<bytes;i++)fputc((v>>(i*8))&255,f);}
bool techno_preview(const char *file,uint64_t seed)
{
    Techno *m=techno_create(seed);if(!m)return false;
    FILE *f=fopen(file,"wb");if(!f){techno_destroy(m);return false;}
    const unsigned int frames=TECHNO_RATE*24,bytes=frames*4;
    fwrite("RIFF",1,4,f);le(f,bytes+36,4);fwrite("WAVEfmt ",1,8,f);le(f,16,4);le(f,1,2);le(f,2,2);
    le(f,TECHNO_RATE,4);le(f,TECHNO_RATE*4,4);le(f,4,2);le(f,16,2);fwrite("data",1,4,f);le(f,bytes,4);
    float block[512*2];
    for(unsigned int i=0;i<frames;) {
        unsigned int n=frames-i;if(n>512)n=512;
        float seconds=(float)i/TECHNO_RATE;
        techno_control(m,bound((seconds-8)/4,0,1),true,false,false);
        techno_render(m,block,n);
        for(unsigned int j=0;j<n*2;j++)le(f,(uint16_t)(int16_t)(bound(block[j],-1,1)*32767),2);
        i+=n;
    }
    bool okay=!ferror(f);if(fclose(f))okay=false;techno_destroy(m);return okay;
}
