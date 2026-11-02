#version 330
in vec2 fUV;
in vec3 fColor;
in float fIntensity;
in float fDist;
in float fHash;
out vec4 finalColor;
uniform vec3 uEdgeColor;    // linear; mixed toward the strip edges
uniform float uEdgeMix;
uniform float uFlicker;
#include "common.glsl"
void main()
{
    float aa = fwidth(fUV.y) * 1.2;
    vec3 tint = mix(fColor, uEdgeColor, smoothstep(0.1, 0.75, abs(fUV.y)) * uEdgeMix);
    vec3 col = line_color(tint, fUV.y, aa, 0.5);
    float fl = 1.0 - uFlicker * step(0.93, hash11(floor(uTime * 12.0) + fHash * 91.0));
    finalColor = vec4(apply_atmo(col * fIntensity * fl, fDist), 1.0);
}
