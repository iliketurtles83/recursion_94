#version 330
// Code-text panels on tower faces (env/city_decor.c), chunk-local.
in vec3 vertexPosition;
in vec2 vertexTexCoord;     // u, v inside the panel (world units)
in vec2 vertexTexCoord2;    // panel hash, kind (0 strip, 1 block)
in vec3 vertexNormal;
in vec4 vertexTangent;      // width, height, intensity, scroll speed
in vec4 vertexColor;
uniform mat4 matModel;
uniform mat4 matView;
uniform mat4 matProjection;
#include "common.glsl"
out vec2 fUV;
out vec2 fInfo;
out vec4 fPanel;
out float fDist;
void main()
{
    vec3 wp = (matModel * vec4(vertexPosition, 1.0)).xyz;
    fUV = vertexTexCoord;
    fInfo = vertexTexCoord2;
    fPanel = vertexTangent;
    fDist = length(uCamPos - wp);
    gl_Position = matProjection * matView * vec4(wp, 1.0);
}
