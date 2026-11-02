#version 330
// Translucent faces of tumbling debris.
in vec3 vertexPosition;
in vec2 vertexTexCoord;     // face uv
in vec2 vertexTexCoord2;    // object slot, face id
in vec3 vertexNormal;
in vec4 vertexTangent;      // 0, intensity, hash, 0
in vec4 vertexColor;
uniform mat4 matModel;
uniform mat4 matView;
uniform mat4 matProjection;
uniform vec4 uObjA[16];
uniform vec4 uObjB[16];
uniform vec4 uObjC[16];
#include "common.glsl"
out vec2 fUV;
out vec3 fColor;
out float fIntensity;
out float fDist;
out float fSeed;
out float fFacing;
void main()
{
    int g = int(vertexTexCoord2.x + 0.5);
    float ang = uObjC[g].x + uObjB[g].w * uTime;
    vec3 axis = uObjB[g].xyz;
    vec3 p = uObjA[g].xyz + rotate_axis(vertexPosition, axis, ang);
    vec3 n = rotate_axis(vertexNormal, axis, ang);
    vec3 wp = (matModel * vec4(p, 1.0)).xyz;
    fUV = vertexTexCoord;
    fColor = srgb_to_linear(vertexColor.rgb);
    fIntensity = vertexTangent.y;
    fDist = length(wp - uCamPos);
    fSeed = vertexTangent.z * 97.0 + vertexTexCoord2.y * 13.0;
    fFacing = abs(dot(normalize(mat3(matModel) * n), normalize(uCamPos - wp)));
    gl_Position = matProjection * matView * vec4(wp, 1.0);
}
