#version 330
// Data trail (trailData): an engine wake made of light, not fire. fHash carries the
// age of the trail point in seconds (interpolated along the ribbon), fColor.r the
// engine index. White-hot at the nozzle, cyan, then deep blue; packets race back
// along it; the tail dissolves in world-fixed blocks so the pattern never flickers.
in vec2 fUV;
in vec3 fColor;
in float fIntensity;
in float fDist;
in float fHash;
out vec4 finalColor;
uniform float uTrailAge;     // seconds covered by the whole trail
uniform float uPackets;      // packet brightness (0 = none)
uniform float uDissolve;     // tail block dissolve (0 = smooth fade)
#include "common.glsl"
void main()
{
    float age = max(fHash, 0.0);
    float t = clamp(age / uTrailAge, 0.0, 1.0);
    float engine = fColor.r > 0.5 ? 1.0 : 0.0;
    // Blocks keyed by emission time: fixed to the world point, not to the ship.
    float born = uTime - age;
    float block = floor(born * 45.0);
    float r = hash11(block * 1.731 + engine * 57.0);
    float keep = mix(1.0, step(r, 1.0 - t * 1.15), uDissolve * smoothstep(0.15, 0.45, t));
    // Packets: their age grows twice as fast as a world point's, so they shoot backward.
    float ph = fract(age * 7.0 - uTime * 14.0 + engine * 0.5);
    float packet = smoothstep(0.78, 0.97, ph) * (1.0 - smoothstep(0.97, 1.0, ph));
    vec3 hot = vec3(1.0), cyan = vec3(0.10, 0.80, 1.0), deep = vec3(0.10, 0.18, 1.0);
    vec3 tint = mix(mix(hot, cyan, smoothstep(0.0, 0.12, t)), deep, smoothstep(0.25, 0.9, t));
    float aa = fwidth(fUV.y) * 1.2;
    vec3 col = line_color(tint, fUV.y, aa, 0.6 * (1.0 - smoothstep(0.0, 0.08, t)));
    float energy = fIntensity * keep * (1.0 + uPackets * packet * (1.0 - t) * 3.0);
    finalColor = vec4(apply_atmo(col * energy, fDist), 1.0);
}
