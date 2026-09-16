#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;
uniform vec2 uResolution;
uniform float uTime;
uniform float uIntensity;
uniform int uRunSeed;
uniform int uEncounterIndex;
uniform vec3 uPrimaryColor;
uniform vec3 uSecondaryColor;
uniform vec3 uSkyZenith;
uniform vec3 uSkyHorizon;
uniform vec3 uBodyColor;

uint hashUint(uint x) {
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    return x ^ (x >> 16);
}
float hash21(ivec2 p) {
    return float(hashUint(uint(p.x)*1597334677u ^ uint(p.y)*3812015801u ^ uint(uRunSeed)) & 65535u)/65535.0;
}
float noise(vec2 p) {
    ivec2 i = ivec2(floor(p));
    vec2 f = fract(p); f = f*f*(3.0-2.0*f);
    return mix(mix(hash21(i), hash21(i+ivec2(1,0)),f.x),
               mix(hash21(i+ivec2(0,1)),hash21(i+ivec2(1,1)),f.x),f.y);
}
float nebula(vec2 p) {
    float value=0.0, weight=0.5;
    for(int i=0;i<4;i++) {
        value += weight*noise(p);
        p = mat2(1.6,1.2,-1.2,1.6)*p+vec2(4.3,7.1);
        weight *= 0.5;
    }
    return value;
}
float line(float d,float width) {
    return 1.0-smoothstep(width,width+max(fwidth(d),0.0007),abs(d));
}
vec3 starfield(vec2 p) {
    vec3 stars=vec3(0.0);
    for(int layer=0;layer<2;layer++) {
        vec2 g=p*(105.0+float(layer)*64.0)+vec2(float(layer)*19.0,uTime*0.018);
        ivec2 cell=ivec2(floor(g));
        float h=hash21(cell);
        vec2 delta=fract(g)-vec2(0.2+0.6*h,0.2+0.6*hash21(cell+37));
        float r=length(delta);
        float core=exp(-r*r*1100.0);
        float flare=exp(-abs(delta.x)*130.0-abs(delta.y)*24.0)
                   +exp(-abs(delta.y)*130.0-abs(delta.x)*24.0);
        stars += mix(vec3(0.55,0.72,1.0),vec3(1.0,0.84,0.65),h)*
                 (core*0.85+flare*0.10*step(0.997,h))*step(0.981,h)*
                 (0.8+0.2*sin(uTime*0.7+h*200.0));
    }
    return stars;
}
vec3 skyline(vec3 sky,vec2 p,int layer) {
    float scale=layer==0 ? 40.0 : 29.0;
    float gx=(p.x+sin(uTime*0.025)*0.005*float(layer+1))*scale;
    int cell=int(floor(gx));
    float h=hash21(ivec2(cell,71+layer));
    float local=fract(gx)-0.5;
    float ground=0.54+float(layer)*0.012;
    float height=0.018+h*h*(layer==0?0.085:0.13);
    float top=ground-height+step(0.31,abs(local))*0.022;
    float body=step(top,p.y)*step(abs(local),0.42);
    float antenna=line(local/scale,0.00065)*step(top-0.025,p.y)*step(p.y,top);
    vec3 fog=mix(uSkyHorizon,uPrimaryColor,layer==0?0.11:0.035);
    vec3 metal=mix(fog,uBodyColor*0.36,layer==0?0.45:0.76);
    float windows=step(0.7,fract((p.y-top)*420.0))*step(0.50,fract(local*7.0))*
                  step(0.36,hash21(ivec2(cell,int(floor(p.y*420.0)))));
    metal += mix(uPrimaryColor,uSecondaryColor,h)*windows*(layer==0?0.055:0.10);
    metal += uSecondaryColor*line(p.y-top,0.0006)*0.18;
    sky=mix(sky,metal,max(body,antenna));
    float beacon=exp(-pow(local/scale*800.0,2.0)-pow((p.y-top+0.025)*900.0,2.0));
    sky+=mix(uSecondaryColor,vec3(1.0,0.42,0.19),0.45)*beacon*(0.5+0.5*sin(uTime*1.7+h*60.0));
    return sky;
}
void main() {
    vec2 coord=vec2(gl_FragCoord.x/uResolution.x,1.0-gl_FragCoord.y/uResolution.y);
    vec2 p=(coord-0.5)*vec2(uResolution.x/uResolution.y,1.0);
    float horizon=exp(-abs(coord.y-0.53)*7.0);
    vec3 sky=mix(uSkyZenith*0.42,uSkyHorizon*1.25,horizon);
    sky+=mix(uPrimaryColor,uSecondaryColor,0.65)*horizon*0.055;
    float clouds=nebula(p*4.0+vec2(uTime*0.004,0.0));
    float ribbon=exp(-pow((p.y+0.26+0.15*sin(p.x*2.0+clouds*2.0))*5.0,2.0));
    sky+=mix(uSecondaryColor,uPrimaryColor,clouds)*pow(clouds,3.0)*ribbon*0.22;
    sky+=starfield(p)*(1.0-smoothstep(0.40,0.57,coord.y));
    // Each five-boss recursion is a physical approach to the singularity. It
    // grows from a distant eclipse in round one to a screen-dominating horizon
    // behind the fifth-round guardian, then recedes when the universe reseeds.
    vec2 q=p-vec2(0.04,-0.36);
    int roundIndex=uEncounterIndex%5;
    int depthIndex=uEncounterIndex/5;
    float proximity=float(roundIndex)/4.0;
    float depthScale=1.0+min(float(depthIndex),6.0)*0.010;
    float horizonScale=mix(0.48,1.02,pow(proximity,1.22))*depthScale;
    q/=horizonScale;
    float r=length(q), radius=0.112, angle=atan(q.y,q.x);
    float corona=exp(-abs(r-radius)*105.0);
    float halo=exp(-abs(r-radius)*22.0);
    float recursion=sin(angle*7.0-uTime*0.11);
    float filament=0.61+0.19*sin(angle*41.0+recursion*2.4+uTime*0.35)
                        +0.13*sin(angle*73.0-recursion*3.1-uTime*0.8)
                        +0.07*sin(angle*127.0+uTime*0.19);
    vec3 hot=mix(vec3(1.0,0.72,0.42),uSecondaryColor,0.50);
    float lensShade=exp(-r*5.8)*(0.035+proximity*0.075);
    sky*=1.0-lensShade;
    sky+=hot*(halo*(0.065+proximity*0.025)+corona*filament*0.54);
    for(int shell=0;shell<4;shell++) {
        float fi=float(shell);
        float warpedRadius=radius+0.018+fi*0.010+
            sin(angle*(6.0+fi*5.0)+recursion*2.0+uTime*(0.08+fi*0.025))*0.0028;
        float thread=line(r-warpedRadius,0.00045+fi*0.00008);
        sky+=mix(uPrimaryColor,hot,0.34+fi*0.11)*thread*(0.045+proximity*0.025)/(1.0+fi);
    }
    float disc=1.0-smoothstep(radius-0.001,radius+0.001,r);
    sky=mix(sky,uSkyZenith*0.045,disc);
    vec2 diskQ=mat2(0.985,0.174,-0.174,0.985)*q;
    float ellipse=length(diskQ*vec2(1.0,4.8));
    float diskBand=exp(-abs(ellipse-0.175)*68.0);
    float lanes=0.60+0.16*sin(ellipse*1050.0+noise(diskQ*32.0)*5.0-uTime*1.4)
                     +0.11*sin(ellipse*1840.0-angle*9.0+uTime*0.33);
    float visibility=mix(1.0-disc,1.0,step(-0.006,diskQ.y));
    float doppler=0.65+0.50*smoothstep(-0.22,0.22,diskQ.x);
    sky+=hot*diskBand*lanes*visibility*doppler*(0.48+uIntensity*0.12);
    sky+=mix(uPrimaryColor,hot,0.72)*exp(-abs(diskQ.y)*200.0)*exp(-abs(diskQ.x)*6.0)*visibility*0.14;
    // Orbital construction rings frame the eclipse above the combat lane.
    float orbit=length(q*vec2(1.0,1.02));
    float ticks=step(0.73,fract((angle+3.141593)*48.0/6.283185));
    sky+=uPrimaryColor*(line(orbit-0.265,0.00065)*0.052+line(orbit-0.279,0.00065)*ticks*0.047);
    sky+=uSecondaryColor*line(orbit-0.42,0.0006)*step(0.3,sin(angle*3.0+0.8))*0.030;
    sky=skyline(sky,coord,0);
    sky=skyline(sky,coord,1);
    sky+=uPrimaryColor*exp(-abs(coord.y-0.548)*160.0)*0.08;
    finalColor=vec4(sky,1.0)*fragColor;
}
