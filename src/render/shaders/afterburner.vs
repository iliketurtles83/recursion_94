#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec2 vertexTexCoord2;
uniform mat4 mvp;
uniform mat4 matModel;
uniform vec3 uCamPos;
uniform float uBoost, uTime;
out vec2 fUV;
out float fLayer, fFacing;
void main() {
    fUV=vertexTexCoord;
    fLayer=vertexTexCoord2.y;
    float u=fUV.x,a=fUV.y*6.2831853;
    float jitter=1.0+(.025+.065*u)*(sin(a*5.0-u*19.0+uTime*24.0)+.45*sin(a*9.0+u*31.0-uTime*33.0));
    float shock=1.0-.12*uBoost*cos(u*25.0);
    float r=(.093+.084*uBoost)*pow(1.0-u,.78)*jitter*shock;
    r+=.055*uBoost*sin(u*3.14159);
    float length=.48+2.6*uBoost;
    if(fLayer>.5){r*=.46;length*=.90;}
    vec3 p=vec3(vertexTexCoord2.x*.48+cos(a)*r,-.05+sin(a)*r,-1.785-u*length);
    p.xy+=vec2(sin(u*21.0-uTime*25.0),cos(u*17.0-uTime*22.0))*.023*u*u*uBoost;
    vec3 wp=(matModel*vec4(p,1.0)).xyz;
    vec3 n=normalize(mat3(matModel)*vec3(cos(a),sin(a),.14));
    fFacing=abs(dot(n,normalize(uCamPos-wp)));
    gl_Position=mvp*vec4(p,1.0);
}
