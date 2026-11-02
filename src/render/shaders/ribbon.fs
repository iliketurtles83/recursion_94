#version 330
in vec2 fUV;
in vec3 fColor;
in float fIntensity;
in float fDist;
in float fSeed;
out vec4 finalColor;
uniform float uPeriod;      // u wrap period (lattice repeats so wrapped u has no seam)
uniform vec3 uColorA;       // purple (linear)
uniform vec3 uColorB;       // blue (linear)
uniform float uScroll;
#include "common.glsl"

// Value noise periodic in x with period px (in lattice cells).
float pnoise(vec2 x, float px)
{
    vec2 i = floor(x);
    vec2 f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    float i0 = mod(i.x, px), i1 = mod(i.x + 1.0, px);
    float a = hash21(vec2(i0, i.y)), b = hash21(vec2(i1, i.y));
    float c = hash21(vec2(i0, i.y + 1.0)), d = hash21(vec2(i1, i.y + 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float pfbm(vec2 x, float px)
{
    float s = 0.0, a = 0.5;
    for (int o = 0; o < 4; o++) { s += a * pnoise(x, px); x *= 2.0; px *= 2.0; a *= 0.5; }
    return s / 0.9375;
}

void main()
{
    float v = fUV.y;
    float u = fUV.x;
    float scroll = uTime * uScroll;
    // Broad soft bands with long wisps along u.
    float n = pfbm(vec2(u / 20.0 - scroll, v * 0.9 + fSeed * 13.0), uPeriod / 20.0);
    float wisps = 0.25 + 0.95 * smoothstep(0.3, 0.75, n);
    // Fine streaks: low frequency along u, high across.
    float st = pnoise(vec2(u / 6.0 - scroll * 2.0, v * 7.0 + fSeed * 5.0), uPeriod / 6.0);
    float streaks = 0.6 + 0.7 * smoothstep(0.35, 0.9, st);
    float edge = smoothstep(1.0, 0.2, abs(v));
    edge *= edge;
    float tone = pnoise(vec2(u / 48.0, fSeed * 3.0), uPeriod / 48.0);
    vec3 col = mix(uColorA, uColorB, smoothstep(0.3, 0.7, tone));
    float I = fIntensity * wisps * streaks * edge * (0.8 + 0.4 * uIntensity);
    finalColor = vec4(apply_atmo(col * I, fDist), 1.0);
}
