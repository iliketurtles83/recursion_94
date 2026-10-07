#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec2 vertexTexCoord2;
uniform mat4 mvp;
uniform float uBoost, uTime;
out vec2 fUV;
void main() {
    fUV=vertexTexCoord;
    float u=fUV.x,a=fUV.y*6.2831853;
    float jitter=1.0+.08*sin(a*5.0-u*24.0+uTime*39.0)*u;
    float r=(.10+.11*uBoost)*(1.0-u)*jitter+.08*uBoost*sin(u*3.14159);
    vec3 p=vec3(vertexTexCoord2.x*.48+cos(a)*r,-.05+sin(a)*r,-1.78-u*(.46+2.6*uBoost));
    gl_Position=mvp*vec4(p,1.0);
}
