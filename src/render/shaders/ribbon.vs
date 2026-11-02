#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;     // u = wrapped global s, v across
in vec2 vertexTexCoord2;    // ribbon id, unused
in vec4 vertexTangent;      // halfWidth, intensity, hash, unused
in vec4 vertexColor;
uniform mat4 matModel;
uniform mat4 matView;
uniform mat4 matProjection;
#include "common.glsl"
out vec2 fUV;
out vec3 fColor;
out float fIntensity;
out float fDist;
out float fSeed;
void main()
{
    vec3 wp = (matModel * vec4(vertexPosition, 1.0)).xyz;
    fUV = vertexTexCoord;
    fColor = srgb_to_linear(vertexColor.rgb);
    fIntensity = vertexTangent.y;
    fDist = length(wp - uCamPos);
    fSeed = vertexTexCoord2.x;
    gl_Position = matProjection * matView * vec4(wp, 1.0);
}
