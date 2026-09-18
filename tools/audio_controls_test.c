#include "../src/audio_synth.c"
#include <stdio.h>
int main(void) {
    static SynthSystem synth;
    short samples[2048];
    InitializeSynthState(&synth,94,DefaultMusicConfig(94));
    SetSynthVolumes(&synth,0,0); StartSynthSFX(&synth,SFX_WARNING);
    RenderAudioFrames(&synth,samples,1024);
    for(int i=0;i<2048;i++) if(samples[i]) { puts("AUDIO: mute failed"); return 1; }
    SetSynthVolumes(&synth,0,1); StartSynthSFX(&synth,SFX_WARNING);
    RenderAudioFrames(&synth,samples,1024);
    int effects=0; for(int i=0;i<2048;i++) effects|=samples[i]!=0;
    InitializeSynthState(&synth,94,DefaultMusicConfig(94));
    SetSynthVolumes(&synth,1,0);
    int music=0;
    for(int frame=0;frame<20;frame++) {
        RenderAudioFrames(&synth,samples,1024);
        for(int i=0;i<2048;i++) music|=samples[i]!=0;
    }
    printf("AUDIO CONTROLS: muted=1 independent-effects=%d independent-music=%d\n",effects,music);
    uint64_t pcm=0; bool golden=ValidateAudioSynth(&pcm);
    printf("AUDIO GOLDEN: matches=%d hash=%016llx\n",golden,(unsigned long long)pcm);
    return !effects||!music;
}
