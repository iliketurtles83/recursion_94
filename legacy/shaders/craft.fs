#version 330
in vec3 fragPosition;
in vec3 fragNormal;
in vec4 fragColor;
out vec4 finalColor;
uniform float uTime;
uniform float uIntensity;
uniform vec3 uPrimaryColor;
uniform vec3 uSecondaryColor;
uniform vec3 uThreatColor;
uniform vec3 uObjectOrigin;
uniform vec3 uViewPosition;
uniform int uObjectClass;
uniform int uEnemyType;

float cellHash(ivec2 p) {
    uint h=uint(p.x)*1597334677u^uint(p.y)*3812015801u;
    h^=h>>16; h*=0x7feb352du; h^=h>>15; h*=0x846ca68bu;
    return float((h^(h>>16))&65535u)/65535.0;
}
float recessBox(vec2 p,vec2 size,float aa) {
    return 1.0-smoothstep(-aa,aa,max(abs(p.x)-size.x,abs(p.y)-size.y));
}
vec3 reliefNormal(vec3 n,float height) {
    vec3 dx=dFdx(fragPosition),dy=dFdy(fragPosition);
    vec3 rx=cross(dy,n),ry=cross(n,dx);
    float det=dot(dx,rx);
    return normalize(n*max(abs(det),1e-9)-sign(det)*(rx*dFdx(height)+ry*dFdy(height)));
}
void main() {
    vec3 base=fragColor.rgb;
    vec3 n=length(fragNormal)>0.1?normalize(fragNormal):vec3(0,1,0);
    vec3 v=normalize(uViewPosition-fragPosition);
    if(uObjectClass>=2) {
        if(uObjectClass==3) {
            vec3 p=fragPosition-uObjectOrigin;
            float phase=p.z*3.8-uTime*27.0+atan(p.y,p.x)*2.0;
            float filament=pow(0.5+0.5*sin(phase+sin(p.z*1.7)*0.8),3.0);
            float striation=0.75+0.25*sin(p.z*33.0+uTime*19.0);
            base*=0.58+filament*0.38+striation*0.12;
        } else if(uObjectClass==4) {
            vec3 p=fragPosition-uObjectOrigin;
            float shell=0.5+0.5*sin(length(p)*29.0-uTime*19.0);
            float grain=cellHash(ivec2(floor(p.xy*83.0+p.z*17.0)));
            base*=0.78+shell*0.16+grain*0.08;
        }
        finalColor=vec4(base*(0.85+0.25*pow(1.0-max(dot(n,v),0.0),3.0)),fragColor.a);
        return;
    }
    float peak=max(base.r,max(base.g,base.b));
    float saturation=peak-min(base.r,min(base.g,base.b));
    float emission=smoothstep(0.55,0.95,peak)*smoothstep(0.15,0.42,saturation);
    // Canopy glass and energy trim stay smooth; painted armor receives relief.
    float armor=smoothstep(0.055,0.14,peak)*(1.0-emission);
    vec3 p=fragPosition-uObjectOrigin,an=abs(n);
    vec2 uv=an.y>an.x&&an.y>an.z?p.xz:(an.x>an.z?p.zy:p.xy);
    float unit=(uObjectClass==1&&uEnemyType==4)?1.65:0.60;
    vec2 tile=vec2(unit,unit*1.35);
    vec2 id=floor(uv/tile);
    uv.x+=mod(id.y,2.0)*tile.x*0.5;
    id=floor(uv/tile);
    vec2 f=fract(uv/tile)*tile;
    float hash=cellHash(ivec2(id));
    float aa=max(max(fwidth(uv.x),fwidth(uv.y))*0.7,0.001);
    float fine=1.0-smoothstep(unit*0.04,unit*0.16,aa);
    vec2 edge=min(f,tile-f);
    float distance=min(edge.x,edge.y);
    float seam=(1.0-smoothstep(unit*0.009,unit*0.009+aa,distance))*fine;
    float bevel=(1.0-smoothstep(unit*0.028,unit*0.028+aa,distance))*fine;
    vec2 boltP=edge-vec2(unit*0.075);
    float bolt=(1.0-smoothstep(unit*0.020,unit*0.020+aa,length(boltP)))*fine;
    float slot=(1.0-smoothstep(unit*0.004,unit*0.004+aa,abs(boltP.x+boltP.y)))*bolt;
    float vent=recessBox(f-tile*vec2(0.50,0.66),tile*vec2(0.30,0.12),aa)*step(0.61,hash)*fine;
    float slat=0.5+0.5*cos(f.y/unit*92.0);
    float scratch=(1.0-smoothstep(0.004,0.004+aa,abs(fract(uv.y*23.0)-0.5)/23.0))*
                  step(0.96,cellHash(ivec2(floor(uv*vec2(12,23)))))*fine;
    float stencil=recessBox(f-tile*vec2(0.70,0.25),tile*vec2(0.10,0.055),aa)*step(hash,0.23)*fine;
    float barcode=step(0.48,fract(f.x/unit*43.0))*stencil;
    float height=(-seam*0.010-bevel*0.004-vent*(0.011+slat*0.008)+bolt*0.006)*armor;
    n=reliefNormal(n,height);
    vec3 paint=base*(0.77+hash*0.29);
    paint*=1.0-seam*0.76-bevel*0.08;
    paint=mix(paint,base*0.14+vec3(0.012),vent*(0.55+slat*0.32));
    paint=mix(paint,vec3(0.17,0.19,0.22)*(1.0-slot*0.82),bolt*0.85);
    paint+=vec3(0.027)*scratch+vec3(0.09)*barcode;
    base=mix(base,paint,armor);
    vec3 light=normalize(vec3(-0.45,0.8,0.42));
    vec3 halfDir=normalize(light+v);
    float diffuse=max(dot(n,light),0.0);
    float roughness=mix(0.30,0.65,hash)*armor+0.17*(1.0-armor);
    float specular=pow(max(dot(n,halfDir),0.0),mix(110.0,20.0,roughness));
    float rim=pow(1.0-max(dot(n,v),0.0),4.0);
    vec3 accent=uObjectClass==1?uThreatColor:uPrimaryColor;
    vec3 lit=base*(0.43+diffuse*0.72)*(1.0-seam*0.22*armor);
    lit+=vec3(0.78,0.85,0.93)*specular*mix(0.48,0.14,roughness)*(1.0-seam*armor);
    lit+=mix(vec3(0.16,0.19,0.23),accent*0.20,0.25)*rim;
    lit+=uSecondaryColor*max(dot(n,normalize(vec3(0.7,0.2,-0.6))),0.0)*0.016;
    // Preserve energy-trim readability without adding new emissive surfaces.
    lit=mix(lit,fragColor.rgb*(1.02+uIntensity*0.06),emission*0.80);
    finalColor=vec4(lit,fragColor.a);
}
