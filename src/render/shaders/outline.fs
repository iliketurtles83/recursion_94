#version 330
// Stepped pillar outlines (env/corridor_pillars.c, trace.vs packing): hot white core, pale
// cyan halo; uGain lets the reflection pass draw them dimmer.
in vec2 fUV;
in vec3 fColor;
in float fIntensity;
in float fDist;
in float fHash;
in float fPulse;
out vec4 finalColor;
uniform float uGain;
#include "common.glsl"
void main()
{
    float aa = fwidth(fUV.y) * 1.2;
    vec3 col = line_color(fColor, fUV.y, aa, 0.8);
    finalColor = vec4(apply_atmo(col * fIntensity * uGain, fDist), 1.0);
}
