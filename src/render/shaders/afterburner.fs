#version 330
in vec2 fUV;
out vec4 finalColor;
uniform float uBoost, uTime, uPower;
void main() {
    float u=fUV.x,a=fUV.y*6.2831853;
    float diamonds=pow(.5+.5*cos(u*30.0-uTime*3.0),6.0);
    float fire=.78+.22*sin(a*7.0+u*41.0-uTime*52.0);
    vec3 blue=vec3(.025,.22,.85),hot=vec3(.20,.53,1.0),orange=vec3(1.0,.19,.008);
    vec3 color=mix(blue,orange,smoothstep(.35,.88,u)*uBoost);
    color=mix(color,hot,diamonds*.36*(1.0-u));
    float fade=pow(1.0-u,.8)*smoothstep(0.0,.025,u+.012);
    finalColor=vec4(color*fade*fire*(.6+.75*uBoost)*uPower,1.0);
}
