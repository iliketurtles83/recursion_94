#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;
uniform sampler2D texture0;
uniform sampler2D uSceneDepth;
uniform int uContactEnabled;
uniform vec2 uClipPlanes;
uniform vec2 uProjectionScale;
uniform vec2 uResolution;
uniform float uTime;
uniform float uIntensity;
uniform float uBeatPulse;
uniform float uBossTransition;
uniform int uRunSeed;
uniform vec3 uPrimaryColor;
uniform vec3 uSecondaryColor;
float luma(vec3 c) { return dot(c,vec3(0.2126,0.7152,0.0722)); }
vec3 viewPosition(vec2 uv) {
    uv=clamp(uv,0.5/uResolution,1.0-0.5/uResolution);
    float d=texture(uSceneDepth,uv).r;
    float z=2.0*uClipPlanes.x*uClipPlanes.y/
            (uClipPlanes.y+uClipPlanes.x-(d*2.0-1.0)*(uClipPlanes.y-uClipPlanes.x));
    return vec3((uv*2.0-1.0)*uProjectionScale*z,-z);
}
float contactShade(vec2 uv) {
    if(uContactEnabled==0) return 1.0;
    vec3 p=viewPosition(uv);
    if(-p.z>160.0) return 1.0;
    vec2 px=1.0/uResolution;
    vec3 r=viewPosition(uv+vec2(px.x,0)),l=viewPosition(uv-vec2(px.x,0));
    vec3 u=viewPosition(uv+vec2(0,px.y)),d=viewPosition(uv-vec2(0,px.y));
    vec3 dx=abs(r.z-p.z)<abs(p.z-l.z)?r-p:p-l;
    vec3 dy=abs(u.z-p.z)<abs(p.z-d.z)?u-p:p-d;
    vec3 normal=normalize(cross(dx,dy));
    if(dot(normal,-p)<0.0) normal=-normal;
    vec2 radius=min(vec2(60.0)*px,0.58/(max(-p.z,0.1)*uProjectionScale));
    float occlusion=0.0;
    for(int i=0;i<12;i++) {
        float angle=float(i)*2.399963;
        float distance=0.20+float(i)/11.0*0.80;
        vec3 delta=viewPosition(uv+vec2(cos(angle),sin(angle))*radius*distance)-p;
        float len=max(length(delta),0.001);
        float hemisphere=max(dot(normal,delta/len)-0.12,0.0);
        occlusion+=hemisphere*(1.0-smoothstep(0.08,1.20,len));
    }
    return clamp(1.0-occlusion*0.25,0.56,1.0);
}
vec3 sampleScene(vec2 uv) {
    return texture(texture0,clamp(uv,0.5/uResolution,1.0-0.5/uResolution)).rgb;
}
vec3 bright(vec2 uv) {
    vec3 c=sampleScene(uv);
    return c*smoothstep(0.48,0.94,max(c.r,max(c.g,c.b)));
}
uint hashUint(uint x) {
    x^=x>>16; x*=0x7feb352du; x^=x>>15; x*=0x846ca68bu; return x^(x>>16);
}
void main() {
    vec2 pixel=1.0/uResolution, uv=fragTexCoord, centered=uv*2.0-1.0;
    float edge=dot(centered,centered);
    // Preserve the whole field; clamp all texture taps to eliminate edge wrapping.
    float arrival=sin(uBossTransition*3.141593)*step(0.0001,uBossTransition);
    float hex=max(abs(centered.y),abs(centered.x)*0.8660254+abs(centered.y)*0.5);
    float rift=exp(-pow((hex-uBossTransition*1.35)*40.0,2.0))*arrival;
    uv+=normalize(centered+0.0001)*rift*0.009;
    vec3 c=sampleScene(uv);
    vec3 n=sampleScene(uv+vec2(0,pixel.y)),s=sampleScene(uv-vec2(0,pixel.y));
    vec3 e=sampleScene(uv+vec2(pixel.x,0)),w=sampleScene(uv-vec2(pixel.x,0));
    float contrast=max(max(luma(n),luma(s)),max(luma(e),luma(w)))-min(min(luma(n),luma(s)),min(luma(e),luma(w)));
    c=mix(c,(n+s+e+w)*0.25,smoothstep(0.12,0.5,contrast)*0.23);
    float preserveEnergy=smoothstep(0.55,0.92,max(c.r,max(c.g,c.b)));
    c*=mix(contactShade(uv),1.0,preserveEnergy);
    vec3 bloom=vec3(0);
    for(int ring=0;ring<3;ring++) {
        float radius=ring==0?2.0:(ring==1?6.0:15.0);
        float weight=ring==0?0.125:(ring==1?0.085:0.035);
        vec2 d=pixel*radius;
        bloom+=(bright(uv+d)+bright(uv+vec2(-d.x,d.y))+bright(uv+vec2(d.x,-d.y))+bright(uv-d))*weight;
    }
    vec3 streak=(bright(uv+vec2(pixel.x*22.0,0))+bright(uv-vec2(pixel.x*22.0,0)))*0.022;
    c+=bloom*(0.30+uIntensity*0.18+uBeatPulse*0.025)+streak;
    float boost=smoothstep(0.72,1.0,uIntensity);
    vec2 echo=centered*pixel*boost*7.0;
    c=mix(c,sampleScene(uv-echo),boost*smoothstep(0.3,1.4,edge)*0.045);
    c+=mix(uPrimaryColor,uSecondaryColor,0.6)*rift*0.40;
    // Smooth filmic response preserves dark metal and neon gradients.
    c=max(c*1.12,vec3(0));
    c=(c*(2.51*c+0.03))/(c*(2.43*c+0.59)+0.14);
    c=pow(c,vec3(0.92));
    c*=1.0-smoothstep(0.4,1.85,edge)*0.16;
    c*=0.982+0.018*sin(gl_FragCoord.y*3.141593);
    uvec2 p=uvec2(gl_FragCoord.xy);
    float grain=float(hashUint(p.x^(p.y*65537u)^uint(uRunSeed))&255u)/255.0-0.5;
    c+=grain/510.0;
    finalColor=vec4(clamp(c,0.0,1.0),1.0)*fragColor;
}
