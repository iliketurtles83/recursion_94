#version 330
in vec2 fUV;
in vec3 fColor;
in float fIntensity;
in float fDist;
in vec2 fPulse;             // hash, frequency (0 = none)
out vec4 finalColor;
uniform float uPulseAmount;
uniform float uPulseSpeed;
#include "common.glsl"
void main()
{
    float aa = fwidth(fUV.y) * 1.2;
    vec3 lc = line_color(fColor, fUV.y, aa, 0.3);
    float pulse = 0.0;
    if (fPulse.y > 0.0) {
        float ph = fract(fUV.x * fPulse.y - uTime * uPulseSpeed * (0.5 + fPulse.x) + fPulse.x * 17.0);
        pulse = smoothstep(0.82, 0.97, ph) * (1.0 - smoothstep(0.97, 1.0, ph)) * uPulseAmount;
    }
    vec3 col = lc * fIntensity * (1.0 + pulse) * (0.7 + 0.6 * uIntensity);
    finalColor = vec4(apply_atmo(col, fDist), 1.0);
}
