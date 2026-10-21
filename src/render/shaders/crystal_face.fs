#version 330
// Crystal field faces: a faint warm glassy fill, brighter at grazing angles. Additive with no
// depth write, so the back edges still show through.
in vec3 fColor;
in float fIntensity;
in float fDist;
in float fFacing;
out vec4 finalColor;
uniform float uFaceBase, uFaceFresnel;
uniform vec2  uVanishPx;
uniform vec4  uClear;
#include "common.glsl"
float centre_clear()
{
    float r = length(gl_FragCoord.xy - uVanishPx) / uClear.y;
    return 1.0 - uClear.x * (1.0 - smoothstep(0.4, 1.0, r)) * smoothstep(uClear.z, uClear.w, fDist);
}
void main()
{
    float g = 1.0 - fFacing;
    float I = fIntensity * (uFaceBase + uFaceFresnel * g * g * g);
    finalColor = vec4(apply_atmo(fColor * I * centre_clear(), fDist), 1.0);
}
