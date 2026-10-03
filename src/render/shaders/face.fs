#version 330
in vec2 fUV;
in vec3 fColor;
in float fIntensity;
in float fDist;
in float fSeed;
in float fFacing;
out vec4 finalColor;
uniform float uDataRate;
#include "common.glsl"
void main()
{
    // Grid of small rectangles toggled by hash, slowly flickering.
    vec2 grid = vec2(7.0, 9.0);
    vec2 cell = floor(fUV * grid);
    vec2 f = fract(fUV * grid);
    float t = floor(uTime * uDataRate + hash21(cell + fSeed) * 4.0);
    float on = step(0.62, hash21(cell * 1.7 + fSeed + t * 0.37));
    float rect = step(0.12, f.x) * step(f.x, 0.88) * step(0.3, f.y) * step(f.y, 0.7);
    float fill = 0.08 + on * rect * 0.9;
    vec2 e = min(fUV, 1.0 - fUV);
    float rim = 1.0 - smoothstep(0.0, 0.06, min(e.x, e.y));
    float I = fIntensity * (fill + rim * 0.3) * (0.6 + 0.4 * (1.0 - fFacing));
    finalColor = vec4(apply_atmo(fColor * I, fDist), 1.0);
}
