#version 330
// Code rain on server pillar faces (env/corridor_pillars.c): columns of short rounded dashes
// scrolling down. Per column (hash): on/off, vertical span, speed, dash period and length, a
// brighter head running down with a short trail behind it. Per dash: on/off and brightness.
// Columns or dash rows that shrink toward a couple of pixels fade to their average brightness
// (no shimmer at distance or in the reflection).
in vec2 fUV;                // u across the face (world units), v above the floor
in vec2 fInfo;              // pillar hash, face (0 lane, 1 front)
in vec4 fPanel;             // face width, pillar height, intensity, wall
in float fDist;
out vec4 finalColor;
uniform vec3  uGreen, uMint, uWhite;   // linear
uniform vec4  uRain;        // column pitch, empty-column fraction, speed min, speed max
uniform vec4  uRain2;       // dash fill, head boost, trail length, white flicker chance
#include "common.glsl"

void main()
{
    float pitch = uRain.x;
    float seed = fInfo.x * 173.0 + fInfo.y * 37.0;
    float W = fPanel.x, H = fPanel.y;
    // Whole columns only, centred on the face.
    float ncol = max(floor(W / pitch - 0.4), 0.0);
    float u = fUV.x - (W - ncol * pitch) * 0.5;
    float x = u / pitch;
    float col = floor(x), fx = fract(x);
    float inFace = step(0.0, x) * step(x, ncol);

    float hc = hash11(col * 1.73 + seed);
    float colOn = step(uRain.y, hc);
    float h2 = hash11(col * 3.11 + seed + 5.0);
    float h3 = hash11(col * 5.71 + seed + 9.0);
    float speed = mix(uRain.z, uRain.w, h2);
    float period = mix(0.32, 0.62, h3);
    float dashLen = mix(0.5, 0.82, hash11(col * 7.93 + seed));
    // Vertical span of the column on this pillar.
    float v0 = H * 0.35 * hash11(col * 2.37 + seed + 1.0) * step(0.5, hash11(col * 4.1 + seed));
    float v1 = H * (1.0 - 0.3 * hash11(col * 6.13 + seed + 2.0) * step(0.6, hash11(col * 8.3 + seed)));
    float span = smoothstep(v0, v0 + 0.3, fUV.y) * (1.0 - smoothstep(v1 - 0.3, v1, fUV.y));

    float y = fUV.y / period + uTime * speed / period;
    float row = floor(y), fy = fract(y);
    float dashOn = step(1.0 - uRain2.x, hash11(row * 0.731 + col * 13.17 + seed));
    float bright = mix(0.35, 1.0, hash11(row * 1.37 + col * 7.11 + seed));

    // Rounded dash (SDF), in world units.
    vec2 p = vec2((fx - 0.5) * pitch, (fy - 0.5) * period);
    vec2 hb = vec2(0.28 * pitch, 0.5 * dashLen * period);
    float rad = min(hb.x, hb.y) * 0.85;
    float d = length(max(abs(p) - hb + rad, 0.0)) - rad;
    float aa = max(fwidth(fUV.x), fwidth(fUV.y)) * 0.75;
    float dash = 1.0 - smoothstep(-aa, aa, d);

    // Head running down the column with a trail above it.
    float cyc = H + 8.0;
    float head = H + 2.0 - mod(uTime * speed * 1.6 + h2 * cyc * 7.0, cyc);
    float above = fUV.y - head;
    float trail = above >= 0.0 ? exp(-above / uRain2.z) : 0.0;
    float boost = 1.0 + uRain2.y * trail * step(0.45, h3);

    float on = dash * dashOn * colOn * bright * boost;
    // Distance filter: columns (x) and rows (y) below ~2 px converge to the mean.
    float wx = fwidth(x), wy = fwidth(y);
    // (The mean is halved: a fully averaged face otherwise reads as flat green paint.)
    float avg = (1.0 - uRain.y) * uRain2.x * (0.56 * mix(0.5, 0.82, 0.5) * 0.85) * 0.675 * 0.5;
    on = mix(on, avg, smoothstep(0.25, 0.6, max(wx, wy)));
    on *= inFace * span;

    vec3 tint = hash11(col * 9.7 + seed) < 0.18 ? uMint : uGreen;
    float flick = step(1.0 - uRain2.w, hash11(row * 3.3 + col * 1.9 + floor(uTime * 8.0)));
    tint = mix(tint, uWhite, flick * dash * trail);
    finalColor = vec4(apply_atmo(tint * fPanel.z * on, fDist), 1.0);
}
