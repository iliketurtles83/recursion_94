#version 330
in vec2 fUV;
in vec3 fColor;
in float fIntensity;
in float fComet;
out vec4 finalColor;
#include "common.glsl"
void main()
{
    float aa = fwidth(fUV.y) * 1.2;
    float I = line_profile(fUV.y, aa);
    float along = smoothstep(0.0, 0.08, fUV.x) * (1.0 - smoothstep(0.6, 1.0, fUV.x));
    if (fComet > 0.5) along = (smoothstep(0.0, 0.03, fUV.x) * (0.25 + 3.0 * exp(-fUV.x * 14.0))) * (1.0 - smoothstep(0.5, 1.0, fUV.x));
    finalColor = vec4(fColor * fIntensity * I * along, 1.0);
}
