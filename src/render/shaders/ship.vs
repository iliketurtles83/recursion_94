#version 330
in vec3 vertexPosition;
in vec3 vertexNormal;
in vec4 vertexColor;
in vec2 vertexTexCoord2;
uniform mat4 mvp;
uniform mat4 matModel;
uniform vec3 uCamPos;
out vec3 fNormal;
out vec3 fView;
out vec3 fColor;
out float fMaterial;
out vec3 fLocal;
out vec3 fLocalNormal, fWorld;
#include "ship_articulation.glsl"
void main()
{
    vec3 p=vertexPosition,n=vertexNormal;
    ship_pose(p,n,vertexTexCoord2.y);
    vec3 wp = (matModel * vec4(p, 1.0)).xyz;
    fNormal = normalize(mat3(matModel) * n);
    fView = normalize(uCamPos - wp);
    fColor = pow(vertexColor.rgb, vec3(2.2));
    fMaterial = vertexTexCoord2.x;
    fLocal = vertexPosition;
    fLocalNormal = vertexNormal;
    fWorld=wp;
    gl_Position = mvp * vec4(p, 1.0);
}
