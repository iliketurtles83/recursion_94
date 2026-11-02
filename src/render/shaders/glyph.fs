#version 330
// Code text: rows of dash "words" with hashed line lengths, indents and blank lines,
// pink / violet / pale cyan per row, slow upward scroll and row flicker. When rows or
// characters shrink toward a pixel the pattern fades to its average brightness.
in vec2 fUV;
in vec2 fInfo;              // panel hash, kind (0 strip, 1 block)
in vec4 fPanel;             // width, height, intensity, scroll speed (rows / s)
in float fDist;
out vec4 finalColor;
uniform vec3 uPink, uViolet, uCyan;   // linear
#include "common.glsl"
void main()
{
    bool strip = fInfo.y < 0.5;
    float seed = fInfo.x * 131.0;
    float rowH = strip ? 0.26 : 0.5;
    float charW = strip ? 0.13 : 0.3;
    float W = fPanel.x, H = fPanel.y;

    float y = fUV.y / rowH + fPanel.w * uTime;
    float row = floor(y), fy = fract(y);
    float hr = hash11(row * 1.31 + seed);
    float indent = floor(hash11(row * 2.7 + seed) * 4.0) * charW * 2.0 * step(0.45, hr);
    float len = mix(0.3, 1.0, hash11(row * 5.3 + seed)) * W;
    float blank = step(hash11(row * 9.1 + seed), 0.12);
    float x = fUV.x - indent;
    float ci = floor(x / charW);
    float gap = step(hash11(ci * 3.7 + row * 11.3 + seed), 0.18);
    float inLine = step(0.0, x) * step(x, len);

    float wy = fwidth(y), wx = fwidth(x / charW);
    float bar = smoothstep(0.22 - wy, 0.22 + wy, fy) * (1.0 - smoothstep(0.78 - wy, 0.78 + wy, fy));
    float on = bar * inLine * (1.0 - gap) * (1.0 - blank);
    float avg = 0.56 * 0.6 * 0.82 * 0.88;
    on = mix(on, avg, smoothstep(0.3, 0.7, max(wy, wx)));

    vec3 col = strip ? (hr < 0.75 ? uPink : uViolet) : (hr < 0.5 ? uPink : (hr < 0.8 ? uCyan : uViolet));
    float flick = 0.75 + 0.25 * step(0.5, hash11(row + floor(uTime * 3.0 + hr * 10.0)));
    vec2 e = min(fUV, vec2(W, H) - fUV);
    float edge = smoothstep(0.0, 0.15, min(e.x, e.y));
    vec3 c = col * fPanel.z * (on * flick + 0.04) * edge;
    finalColor = vec4(apply_atmo(c, fDist), 1.0);
}
