#version 330
// Overhead corridor beams (env/corridor_beams.c), vertex shader tower.vs. Dark body densely
// covered in procedural cyan circuit traces: horizontal runs along the beam on a lattice of
// pitch uTrace.x, broken into hashed segments, sparser cross runs, vias at segment ends and
// data pulses. Fades to its mean where the lattice nears pixel size; fog toward the sky.
in vec3 fPos;
in vec2 fUV;                // u along the beam (world, wrapped), v across / up (world)
in vec3 fNormal;
in vec2 fInfo;              // line hash, width
in float fFace;             // 0 underside, 1 side, 2 end cap
in vec3 fTint;
out vec4 finalColor;
uniform vec3  uBody;        // linear
uniform vec3  uTraceColor;  // linear
uniform vec4  uTrace;       // pitch, intensity, run density, cross density
uniform vec2  uPulse;       // speed, amount
uniform vec2  uFogRange;
uniform vec3  uZenith, uHorizon;
uniform vec2  uHaze;
#include "common.glsl"

vec3 sky_color(vec3 dir)
{
    float e = max(dir.y, 0.0);
    float band = exp(-e / max(uHaze.x, 1e-4));
    return mix(uZenith, uHorizon, band) * mix(1.0, uHaze.y, band);
}

// Segmented runs along x on rows y = j. Returns line coverage; via: end pads; pulse: packets.
float runs(vec2 g, float seed, float density, vec2 w, out float via, out float pulse)
{
    float j = floor(g.y + 0.5);
    float hj = hash11(j * 1.71 + seed);
    float len = mix(4.0, 22.0, hash11(j * 2.37 + seed));
    float sx = g.x + hj * 37.0;
    float k = floor(sx / len);
    float on = step(1.0 - density, hash21(vec2(j, k) + seed));
    float x = sx - k * len;
    float a = 0.6 + 2.0 * hash21(vec2(k, j) + seed + 3.1);
    float b = len - 0.6 - 2.0 * hash21(vec2(k, j) + seed + 7.3);
    float inSeg = smoothstep(a - w.x, a + w.x, x) * (1.0 - smoothstep(b - w.x, b + w.x, x));
    float dy = abs(g.y - j);
    float hw = 0.15;
    float line = 1.0 - smoothstep(hw - w.y, hw + w.y, dy);
    float r = min(abs(x - a), abs(x - b));
    float pad = 1.0 - smoothstep(0.24 - max(w.x, w.y), 0.24 + max(w.x, w.y), length(vec2(r, dy)));
    via = pad * on * step(a - 0.3, x) * step(x, b + 0.3);
    float ph = fract(x / len - uTime * uPulse.x / len + hj * 5.0);
    pulse = exp(-pow((ph - 0.5) * 14.0, 2.0)) * step(0.6, hj);
    return line * inSeg * on;
}

void main()
{
    vec3 n = normalize(fNormal);
    vec3 toCam = uCamPos - fPos;
    float dist = length(toCam);
    vec3 V = toCam / dist;
    float seed = fInfo.x * 113.0 + fFace * 17.0;
    vec2 g = fUV / uTrace.x;
    vec2 w = fwidth(g);

    float viaA, pulseA, viaB, pulseB;
    float lineA = runs(g, seed, uTrace.z, w, viaA, pulseA);
    // Cross runs: the same pattern rotated, sparser (turns and junctions).
    float lineB = runs(vec2(g.y + 11.0, g.x), seed + 41.0, uTrace.w, w.yx, viaB, pulseB);
    float cov = max(max(lineA, lineB), max(viaA, viaB) * 0.9);
    float glow = cov * (1.0 + uPulse.y * pulseA * lineA);
    // Mean over the lattice once cells approach a pixel (no shimmer at distance).
    // (Scaled down: the true mean on a grazing face reads as flat teal paint.)
    float mean = (0.26 * 0.75 * uTrace.z + 0.26 * 0.75 * uTrace.w) * 0.25;
    glow = mix(glow, mean, smoothstep(0.25, 0.6, max(w.x, w.y)));
    // Keep a dark margin along the beam edges so the beam outline reads.
    float W = fFace < 0.5 ? fInfo.y : 1e3;
    float edge = smoothstep(0.25, 0.6, min(fUV.y, W - fUV.y));
    if (fFace > 1.5) edge = 0.35;

    float key = 0.4 + 0.6 * max(dot(n, normalize(vec3(0.35, -0.55, 0.75))), 0.0);
    vec3 col = uBody * key;
    vec3 hot = mix(uTraceColor, vec3(1.0), 0.12 * cov);
    col += hot * glow * uTrace.y * edge * (0.85 + 0.3 * uIntensity);
    col = mix(col, sky_color(-V), smoothstep(uFogRange.x, uFogRange.y, dist));
    finalColor = vec4(col, 1.0);
}
