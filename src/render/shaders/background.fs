#version 330
// Dark navy radial gradient around the vanishing point plus sparse static dust.
in vec2 fUV;
out vec4 finalColor;
uniform vec2  uVanish;
uniform float uAspect;
uniform vec3  uCamRight, uCamUp, uCamFwd;
uniform float uTanHalf;
uniform vec3  uBgColor;     // linear
uniform vec3  uGlowColor;   // linear
uniform float uGlow;
uniform float uDust;
uniform float uGlowFocus;  // 0 = wide glow with a floor everywhere (vortex), 1 = a tight halo around the vanishing point
#include "common.glsl"
void main()
{
    vec2 ndc = fUV * 2.0 - 1.0;
    vec3 dir = normalize(uCamFwd + uCamRight * ndc.x * uTanHalf * uAspect + uCamUp * ndc.y * uTanHalf);
    float d = length((fUV - uVanish) * vec2(uAspect, 1.0));
    float wide = exp(-d * 1.4) + 0.35;
    float tight = exp(-d * 4.0) + 1.5 * exp(-d * 28.0);
    vec3 col = uBgColor + uGlowColor * uGlow * mix(wide, tight, uGlowFocus);
    vec3 q = dir * 90.0;
    vec3 c = floor(q);
    vec3 f = fract(q) - 0.5 - (hash33(c) - 0.5) * 0.7;
    float h = hash31(c);
    float star = step(1.0 - uDust, h) * exp(-dot(f, f) * 160.0) * (0.3 + 1.2 * hash31(c + 7.0));
    col += vec3(0.6, 0.85, 1.0) * star;
    finalColor = vec4(col, 1.0);
}
