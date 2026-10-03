#version 330
// "Matrix" digital rain on server pillar faces (env/corridor_pillars.c). Glyphs sit on a fixed
// character grid; each glyph is a hashed set of strokes on a 3x3 node lattice (reads as
// katakana-like symbols) and changes over time, faster near the stream head. Per column
// (hash): on/off, speed, trail length, phase. A stream falls down each column: white-hot head,
// green trail fading and darkening behind it, faint residual glyphs elsewhere.
// Distance filter: below ~4 px per character the strokes converge to their mean coverage; below
// ~2 px per row the stream converges to its mean brightness (no shimmer at distance).
in vec2 fUV;                // u across the face (world units), v above the floor
in vec2 fInfo;              // pillar hash, face (0 lane, 1 front)
in vec4 fPanel;             // face width, pillar height, intensity, wall
in float fDist;
out vec4 finalColor;
uniform vec3  uGreen, uMint, uWhite;   // linear
uniform vec4  uRain;        // character width, empty-column fraction, speed min, speed max
uniform vec4  uRain2;       // residual glyph brightness, head boost, trail length, glyph change rate
#include "common.glsl"

float sd_seg(vec2 p, vec2 a, vec2 b)
{
    vec2 pa = p - a, ba = b - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h);
}

// Distance to the strokes of glyph `id` at cell-local p in [0,1]^2.
float glyph(vec2 p, float id)
{
    uint bits = uint(hash11(id * 0.917 + 3.1) * 65535.0) | 0x0101u;   // never empty
    const float L = 0.22, C = 0.5, R = 0.78, B = 0.14, M = 0.5, T = 0.86;
    vec2 a[16] = vec2[16](vec2(L, T), vec2(C, T), vec2(L, M), vec2(C, M), vec2(L, B), vec2(C, B),
                          vec2(L, M), vec2(L, B), vec2(C, M), vec2(C, B), vec2(R, M), vec2(R, B),
                          vec2(C, M), vec2(C, M), vec2(L, T), vec2(C, M));
    vec2 b[16] = vec2[16](vec2(C, T), vec2(R, T), vec2(C, M), vec2(R, M), vec2(C, B), vec2(R, B),
                          vec2(L, T), vec2(L, M), vec2(C, T), vec2(C, M), vec2(R, T), vec2(R, M),
                          vec2(R, T), vec2(L, B), vec2(C, M), vec2(R, B));
    float d = 1e3;
    for (int i = 0; i < 16; i++)
        if (((bits >> uint(i)) & 1u) != 0u) d = min(d, sd_seg(p, a[i], b[i]));
    return d;
}

void main()
{
    float cw = uRain.x, ch = uRain.x * 1.35;
    float seed = fInfo.x * 173.0 + fInfo.y * 37.0;
    float W = fPanel.x, H = fPanel.y;
    // Whole columns only, centred on the face.
    float ncol = max(floor(W / cw - 0.4), 0.0);
    float u = fUV.x - (W - ncol * cw) * 0.5;
    vec2 g = vec2(u / cw, fUV.y / ch);
    vec2 cell = floor(g), f = fract(g);
    float col = cell.x, row = cell.y;
    float inFace = step(0.0, g.x) * step(g.x, ncol) * step(0.5, fUV.y);

    float hc = hash11(col * 1.73 + seed);
    float colOn = step(uRain.y, hc);
    float h2 = hash11(col * 3.11 + seed + 5.0);
    float h3 = hash11(col * 5.71 + seed + 9.0);
    float speed = mix(uRain.z, uRain.w, h2);
    float trailLen = uRain2.z * mix(0.5, 1.6, h3);
    // Stream: the head falls from above the top to below the floor, then waits a random gap.
    float cyc = H + trailLen + 4.0 + 10.0 * hash11(col * 8.3 + seed);
    float head = H + 1.0 - mod(uTime * speed + h2 * 97.0, cyc);
    float vr = (row + 0.5) * ch;                 // this character's centre height
    float behind = vr - head;                    // > 0: in the trail above the head
    float trail = behind >= 0.0 ? exp(-behind / trailLen) * step(behind, trailLen * 2.5) : 0.0;
    float isHead = step(0.0, behind) * step(behind, ch);
    float bright = max(trail, uRain2.x * step(0.35, hash11(row * 0.71 + col * 13.1 + seed)));

    // Glyph: changes now and then, quickly at the head.
    float rate = uRain2.w * (0.3 + 4.0 * isHead) * mix(0.5, 1.5, hash11(row * 2.9 + col + seed));
    float id = hash21(cell + seed) * 911.0 + floor(uTime * rate + hash21(cell * 1.7 + seed) * 7.0);
    vec2 gw = fwidth(g);
    float aa = max(gw.x, gw.y) * 1.2;
    float d = glyph(f, id);
    float strokes = 1.0 - smoothstep(0.065 - aa, 0.065 + aa, d);
    // Far: strokes -> mean coverage (characters under ~4 px), stream -> its mean (rows under ~2 px).
    strokes = mix(strokes, 0.3, smoothstep(0.15, 0.35, max(gw.x, gw.y)));
    float meanStream = trailLen / cyc + uRain2.x * 0.65;
    bright = mix(bright, meanStream * 0.6, smoothstep(0.3, 0.6, gw.y));

    vec3 deep = uGreen * vec3(0.25, 0.55, 0.3);
    vec3 tint = mix(deep, hash11(col * 9.7 + seed) < 0.15 ? uMint : uGreen, clamp(trail * 1.4, 0.0, 1.0));
    tint = mix(tint, uWhite, isHead * colOn);
    float boost = 1.0 + uRain2.y * isHead;
    float on = strokes * bright * boost * colOn * inFace;
    finalColor = vec4(apply_atmo(tint * fPanel.z * on, fDist), 1.0);
}
