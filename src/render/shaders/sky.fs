#version 330
// Open-sky background: zenith-to-horizon gradient, a haze band on the horizon and sparse
// static dust above it. At the horizon it equals uHorizon * uHaze.y, the colour the grid
// floor fogs into, so floor and sky meet without a seam.
in vec2 fUV;
out vec4 finalColor;
uniform float uAspect, uTanHalf;
uniform vec3  uCamRight, uCamUp, uCamFwd;
uniform vec3  uZenith, uHorizon;   // linear
uniform vec2  uHaze;               // band width (in dir.y), horizon multiplier
uniform float uDust;
#include "common.glsl"
void main()
{
    vec2 ndc = fUV * 2.0 - 1.0;
    vec3 dir = normalize(uCamFwd + uCamRight * ndc.x * uTanHalf * uAspect + uCamUp * ndc.y * uTanHalf);
    float e = max(dir.y, 0.0);
    float band = exp(-e / max(uHaze.x, 1e-4));
    vec3 col = mix(uZenith, uHorizon, band) * mix(1.0, uHaze.y, band);
    vec3 q = dir * 90.0;
    vec3 c = floor(q);
    vec3 f = fract(q) - 0.5 - (hash33(c) - 0.5) * 0.7;
    float h = hash31(c);
    float star = step(1.0 - uDust, h) * exp(-dot(f, f) * 160.0) * (0.3 + 1.2 * hash31(c + 7.0));
    col += vec3(0.6, 0.85, 1.0) * star * smoothstep(0.02, 0.12, e);
    finalColor = vec4(col, 1.0);
}
