#version 330
in vec2 fUV;
in float fLayer, fFacing;
out vec4 finalColor;
uniform float uBoost, uTime, uPower;
void main() {
    float u=fUV.x,a=fUV.y*6.2831853;
    float diamonds=pow(.5+.5*cos(u*25.0-uTime*.9),10.0);
    float fine=sin(a*11.0+u*67.0-uTime*39.0),broad=sin(a*3.0-u*21.0+uTime*25.0);
    // Filter the fine wisps when they become smaller than a screen pixel.
    fine*=1.0-smoothstep(.5,2.0,fwidth(a*11.0+u*67.0));
    float fire=.80+.12*broad+.08*fine;
    vec3 blue=vec3(.018,.16,.66),hot=vec3(.48,.72,1.0),orange=vec3(1.0,.28,.018);
    vec3 color=mix(blue,orange,smoothstep(.40,.94,u)*uBoost);
    if(fLayer>.5)color=mix(hot,orange,smoothstep(.38,.97,u)*uBoost);
    color=mix(color,hot,diamonds*(fLayer>.5?.65:.25)*(1.0-u));
    float edge=smoothstep(.0,.25,fFacing),fade=pow(1.0-u,1.15)*smoothstep(0.0,.035,u+.012);
    float power=fLayer>.5?mix(.80,.30,smoothstep(.10,.70,u)):.62;
    finalColor=vec4(color*fade*fire*mix(.30,1.0,edge)*power*(.65+.75*uBoost)*uPower,1.0);
}
