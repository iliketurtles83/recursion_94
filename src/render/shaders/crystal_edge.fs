#version 330
// Crystal field edges: saturated orange lines (a weak white core, unlike line_facing.fs),
// thinned out around the vanishing point at distance so the lane ahead stays clear.
in vec2 fUV;
in vec3 fColor;
in float fIntensity;
in float fDist;
in float fHash;
out vec4 finalColor;
uniform float uWhiteCore;
uniform vec2  uVanishPx;    // vanishing point in pixels
uniform vec4  uClear;       // x: strength, y: screen radius (px), z: distance start, w: distance end
#include "common.glsl"
float centre_clear()
{
    float r = length(gl_FragCoord.xy - uVanishPx) / uClear.y;
    return 1.0 - uClear.x * (1.0 - smoothstep(0.4, 1.0, r)) * smoothstep(uClear.z, uClear.w, fDist);
}
void main()
{
    float aa = fwidth(fUV.y) * 1.2;
    vec3 col = line_color(fColor, fUV.y, aa, uWhiteCore);
    finalColor = vec4(apply_atmo(col * fIntensity * centre_clear(), fDist), 1.0);
}
