#version 330
// Static planar strips (circuit traces, vias) in chunk-local space. Packing: linegeo.h
// "planar strip". Strips narrower than uMinPx are widened along `across` with their
// intensity scaled down (energy kept), like the camera-facing lines.
in vec3 vertexPosition;     // already offset by +-across * halfWidth
in vec2 vertexTexCoord;     // u = arc length, v = -1..1 across
in vec2 vertexTexCoord2;
in vec3 vertexNormal;       // across (unit)
in vec4 vertexTangent;      // halfWidth, intensity, hash, pulse frequency
in vec4 vertexColor;
uniform mat4 matModel;
uniform mat4 matView;
uniform mat4 matProjection;
#include "common.glsl"
out vec2 fUV;
out vec3 fColor;
out float fIntensity;
out float fDist;
out float fHash;
out float fPulse;
void main()
{
    vec3 wp = (matModel * vec4(vertexPosition, 1.0)).xyz;
    vec3 across = mat3(matModel) * vertexNormal;
    float hw = vertexTangent.x;
    float dist = length(uCamPos - wp);
    float hwEff;
    float k = min_width(hw, dist, hwEff);
    wp += across * (hwEff - hw) * vertexTexCoord.y;
    fUV = vertexTexCoord;
    fColor = srgb_to_linear(vertexColor.rgb);
    fIntensity = vertexTangent.y * k;
    fDist = dist;
    fHash = vertexTangent.z;
    fPulse = vertexTangent.w;
    gl_Position = matProjection * matView * vec4(wp, 1.0);
}
