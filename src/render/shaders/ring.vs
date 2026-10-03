#version 330
// Segmented ring layers. Vertices are ring-local (xy = cross-section, z = axial);
// the ring basis comes from per-chunk uniform arrays and rotation is animated here.
in vec3 vertexPosition;
in vec2 vertexTexCoord;     // u along, v across
in vec2 vertexTexCoord2;    // ring slot, angular velocity
in vec3 vertexNormal;       // across direction (ring-local)
in vec4 vertexTangent;      // halfWidth, intensity, hash, pulseFreq
in vec4 vertexColor;
uniform mat4 matModel;
uniform mat4 matView;
uniform mat4 matProjection;
uniform vec4 uRingA[16];    // origin (chunk-local), w unused
uniform vec4 uRingB[16];    // right
uniform vec4 uRingC[16];    // up
#include "common.glsl"
out vec2 fUV;
out vec3 fColor;
out float fIntensity;
out float fDist;
out vec2 fPulse;
void main()
{
    int g = int(vertexTexCoord2.x + 0.5);
    float ang = vertexTexCoord2.y * uTime;
    float c = cos(ang), s = sin(ang);
    float hw = vertexTangent.x;
    float side = vertexTexCoord.y;
    vec2 acr = vertexNormal.xy;
    vec2 cl = vertexPosition.xy - acr * hw * side;
    cl = vec2(c * cl.x - s * cl.y, s * cl.x + c * cl.y);
    acr = vec2(c * acr.x - s * acr.y, s * acr.x + c * acr.y);
    vec3 right = uRingB[g].xyz, up = uRingC[g].xyz;
    vec3 fwd = cross(up, right);
    vec3 center = uRingA[g].xyz + right * cl.x + up * cl.y + fwd * vertexPosition.z;
    vec3 across = right * acr.x + up * acr.y;
    vec3 wc = (matModel * vec4(center, 1.0)).xyz;
    float dist = length(wc - uCamPos);
    float hwEff;
    float k = min_width(hw, dist, hwEff);
    vec3 wp = (matModel * vec4(center + across * hwEff * side, 1.0)).xyz;
    fUV = vertexTexCoord;
    fColor = srgb_to_linear(vertexColor.rgb);
    fIntensity = vertexTangent.y * k;
    fDist = dist;
    fPulse = vertexTangent.zw;
    gl_Position = matProjection * matView * vec4(wp, 1.0);
}
