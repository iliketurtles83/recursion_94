#version 330
// Circuit trace: neon line profile plus occasional data pulses running along it.
in vec2 fUV;
in vec3 fColor;
in float fIntensity;
in float fDist;
in float fHash;
in float fPulse;
out vec4 finalColor;
uniform float uPulseSpeed;
uniform float uPulseAmount;
#include "common.glsl"
void main()
{
    float aa = fwidth(fUV.y) * 1.2;
    vec3 col = line_color(fColor, fUV.y, aa, 0.45);
    float pulse = 0.0;
    if (fPulse > 0.0) {
        // A short bright packet every 1/fPulse units, travelling upward; only some traces carry them.
        float x = fract(fUV.x * fPulse - uTime * uPulseSpeed * fPulse + fHash * 7.0);
        pulse = exp(-pow((x - 0.5) * 9.0, 2.0)) * step(0.55, fHash);
    }
    finalColor = vec4(apply_atmo(col * fIntensity * (1.0 + uPulseAmount * pulse), fDist), 1.0);
}
