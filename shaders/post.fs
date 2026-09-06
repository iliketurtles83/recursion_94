#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

out vec4 finalColor;

uniform sampler2D texture0;
uniform vec2 uResolution;
uniform float uTime;
uniform float uIntensity;
uniform float uBeatPulse;
uniform float uBossTransition;
uniform int uRunSeed;
uniform vec3 uPrimaryColor;
uniform vec3 uSecondaryColor;

float luminance(vec3 color) {
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

vec3 bloomSample(vec2 uv) {
    vec3 color = texture(texture0, clamp(uv, vec2(0.001), vec2(0.999))).rgb;
    float gate = smoothstep(0.66, 0.96, max(max(color.r, color.g), color.b));
    return color * gate;
}

// ── CRT barrel distortion ──────────────────────────────────────────
// Warps UV toward the curved glass of a CRT faceplate. Stronger at
// edges, zero at center. Brings the edges inward so the image reads
// as seen through a curved monitor rather than a flat panel.
vec2 crtBarrelDistort(vec2 uv) {
    vec2 centered = uv * 2.0 - 1.0;
    float r2 = dot(centered, centered);
    float distortion = 1.0 + 0.18 * r2 + 0.06 * r2 * r2;
    vec2 distorted = centered * distortion;
    return distorted * 0.5 + 0.5;
}

// ── CRT scanline mask ──────────────────────────────────────────────
// Horizontal sine-wave modulation simulates the electron beam
// raster sweep. Subtle — you feel it more than see it.
vec3 crtScanlines(vec2 uv, float intensity) {
    float scanline = 0.5 + 0.5 * sin(uv.y * uResolution.y * 3.14159 * 0.85);
    return vec3(scanline);
}

// ── CRT phosphor sub-pixel mask ────────────────────────────────────
// Simulates the red/green/blue phosphor stripe pattern on a CRT.
// Each column of pixels is tinted R, G, or B, creating the subtle
// color fringing that makes CRT text and lines feel alive.
vec3 crtPhosphorMask(vec2 uv) {
    int col = int(mod(uv.x * uResolution.x, 3.0));
    if (col == 0) return vec3(1.05, 0.92, 0.95);  // Red stripe
    if (col == 1) return vec3(0.95, 1.05, 0.92);  // Green stripe
    return vec3(0.92, 0.95, 1.05);                  // Blue stripe
}

// ── 8x8 Bayer ordered dithering matrix ─────────────────────────────
// Breaks up color banding after quantization by distributing error
// spatially across adjacent pixels. Produces the characteristic
// crosshatched texture of period hardware.
float bayer8x8(vec2 pos) {
    int x = int(mod(pos.x, 8.0));
    int y = int(mod(pos.y, 8.0));

    if (y == 0) return float((x < 4) ? (x * 4) : (x * 4)) / 64.0;
    // Manual 8x8 Bayer threshold matrix
    if (y == 1) return float((x < 4) ? (x * 4 + 32) : (x * 4 + 16)) / 64.0;
    if (y == 2) return float((x < 4) ? (x * 4 + 8) : (x * 4 + 40)) / 64.0;
    if (y == 3) return float((x < 4) ? (x * 4 + 48) : (x * 4 + 24)) / 64.0;
    if (y == 4) return float((x < 4) ? (x * 4 + 2) : (x * 4 + 34)) / 64.0;
    if (y == 5) return float((x < 4) ? (x * 4 + 36) : (x * 4 + 18)) / 64.0;
    if (y == 6) return float((x < 4) ? (x * 4 + 10) : (x * 4 + 42)) / 64.0;
    return float((x < 4) ? (x * 4 + 50) : (x * 4 + 26)) / 64.0;
}

// ── CRT quantization + dithering ───────────────────────────────────
// Truncates color to a VGA Mode 13h–style palette (3R:3G:2B = 256
// simultaneous colors from an 18-bit master palette). Bayer dither
// breaks up the resulting banding into a fine crosshatch texture.
vec3 crtQuantize(vec3 color, vec2 fragCoord) {
    float dither = bayer8x8(fragCoord) - 0.5;  // [-0.5, 0.5]

    // 3-bit red (0..7), 3-bit green (0..7), 2-bit blue (0..3)
    float r = floor((color.r + dither * 0.125) * 7.0 + 0.5) / 7.0;
    float g = floor((color.g + dither * 0.125) * 7.0 + 0.5) / 7.0;
    float b = floor((color.b + dither * 0.125) * 3.0 + 0.5) / 3.0;

    return vec3(r, g, b);
}

void main() {
    vec2 uv = fragTexCoord;
    vec2 pixel = 1.0 / uResolution;
    vec2 centered = uv * 2.0 - 1.0;
    float edge = dot(centered, centered);

    // ── CRT barrel distortion (applied before all sampling) ────────
    vec2 crtUV = crtBarrelDistort(uv);

    // One-shot hexagonal rift wave used only during the boss approach.
    // Unlike the removed beat rings, it expands once and clears before
    // combat starts.
    float hexDistance = max(abs(centered.y),
                            abs(centered.x) * 0.8660254 + abs(centered.y) * 0.5);
    float riftRadius = uBossTransition * 1.28;
    float riftWave = (1.0 - smoothstep(0.0, 0.055,
                                      abs(hexDistance - riftRadius))) *
                     step(0.0001, uBossTransition);
    vec2 riftDirection = normalize(centered + vec2(0.0001));
    // Rift distortion on the already-distorted UV
    crtUV += riftDirection * riftWave * (1.0 - uBossTransition) * 0.018;

    // Compact edge-aware antialiasing smooths diagonal craft silhouettes
    // and distant architecture after the 3D pass without requiring
    // another target.
    vec3 source = texture(texture0, crtUV).rgb;
    vec3 north = texture(texture0, crtUV + vec2(0.0, pixel.y)).rgb;
    vec3 south = texture(texture0, crtUV - vec2(0.0, pixel.y)).rgb;
    vec3 east = texture(texture0, crtUV + vec2(pixel.x, 0.0)).rgb;
    vec3 west = texture(texture0, crtUV - vec2(pixel.x, 0.0)).rgb;
    float lumaCenter = luminance(source);
    float lumaMin = min(lumaCenter, min(min(luminance(north), luminance(south)),
                                        min(luminance(east), luminance(west))));
    float lumaMax = max(lumaCenter, max(max(luminance(north), luminance(south)),
                                        max(luminance(east), luminance(west))));
    float edgeAA = smoothstep(0.055, 0.22, lumaMax - lumaMin) * 0.58;
    vec3 color = mix(source, (north + south + east + west) * 0.25, edgeAA);

    // CRT-enhanced chromatic aberration: stronger at edges, audio-reactive
    // on beat pulses. Adds the radial color separation that makes bright
    // edges on CRTs literally split into R/G/B fringes.
    float crtChromaStrength = 0.42 + uIntensity * 0.76 + uBeatPulse * 0.25;
    vec2 crtChromaOffset = centered * pixel * crtChromaStrength;
    vec3 crtChroma = vec3(texture(texture0, crtUV + crtChromaOffset).r,
                          texture(texture0, crtUV).g,
                          texture(texture0, crtUV - crtChromaOffset).b);
    color = mix(color, crtChroma, 0.34);

    vec2 nearTap = pixel * (1.6 + uIntensity * 1.4);
    vec2 farTap = pixel * (4.2 + uIntensity * 2.5);
    vec3 bloom = bloomSample(crtUV + vec2( nearTap.x, 0.0)) +
                 bloomSample(crtUV + vec2(-nearTap.x, 0.0)) +
                 bloomSample(crtUV + vec2(0.0,  nearTap.y)) +
                 bloomSample(crtUV + vec2(0.0, -nearTap.y));
    bloom += (bloomSample(crtUV + vec2( farTap.x,  farTap.y)) +
              bloomSample(crtUV + vec2(-farTap.x,  farTap.y)) +
              bloomSample(crtUV + vec2( farTap.x, -farTap.y)) +
              bloomSample(crtUV + vec2(-farTap.x, -farTap.y))) * 0.62;
    bloom *= 0.128;

    // Very low-energy horizontal highlight streaks give headlights, signage
    // and weapon impacts a cinematic lens response without whitening surfaces.
    vec3 anamorphic = bloomSample(crtUV + vec2(pixel.x * 9.0, 0.0)) +
                      bloomSample(crtUV - vec2(pixel.x * 9.0, 0.0)) +
                      bloomSample(crtUV + vec2(pixel.x * 19.0, 0.0)) * 0.55 +
                      bloomSample(crtUV - vec2(pixel.x * 19.0, 0.0)) * 0.55;

    // A restrained radial echo appears at higher intensity and reads as
    // acceleration while keeping the center/reticle stable.
    float speedBlur = smoothstep(0.62, 1.0, uIntensity) * 0.085;
    vec2 radialStep = centered * pixel * (2.0 + uIntensity * 5.0);
    vec3 radial = texture(texture0, crtUV - radialStep).rgb +
                  texture(texture0, crtUV - radialStep * 2.4).rgb;
    color = mix(color, radial * 0.5, speedBlur);

    color += bloom * (0.30 + uIntensity * 0.28 + uBeatPulse * 0.06);
    color += anamorphic * (0.008 + uIntensity * 0.007);
    color += mix(uPrimaryColor, uSecondaryColor, uv.x) *
             (0.006 + 0.008 * uIntensity) * (1.0 - luminance(color));
    float arrivalEnvelope = sin(uBossTransition * 3.14159265) *
                            step(0.0001, uBossTransition);
    color += mix(uSecondaryColor, uPrimaryColor, 0.46) *
             (riftWave * 0.34 + arrivalEnvelope * 0.035) *
             (1.0 - luminance(color) * 0.55);

    // Bounded raymarched tunnel, only ever visible during the ~2.6s boss
    // approach and fully cleared by the time combat starts.
    if (arrivalEnvelope > 0.001) {
        vec3 rd = normalize(vec3(centered, 1.4));
        float marched = 0.0;
        float glow = 0.0;
        for (int i = 0; i < 16; i++) {
            vec3 q = rd * marched;
            q.z += uTime * 1.6;
            float scale = 1.0;
            for (int f = 0; f < 3; f++) {
                q = abs(q) - 0.75;
                q.xy = q.x > q.y ? q.xy : q.yx;
                q *= 1.7;
                scale *= 1.7;
            }
            float d = (length(max(abs(q) - 0.6, 0.0)) - 0.08) / scale;
            d = abs(d) + 0.02;
            glow += 0.02 / (0.03 + d * d * 40.0);
            marched += max(d * 0.6, 0.03);
            if (marched > 6.0) break;
        }
        vec3 tunnelColor = mix(uPrimaryColor, uSecondaryColor,
                               sin(marched * 0.6 + uTime) * 0.5 + 0.5) * glow;
        color += tunnelColor * arrivalEnvelope * 0.5;
    }

    float screenY = gl_FragCoord.y / uResolution.y;
    float horizonHaze = exp(-pow((screenY - 0.53) * 4.1, 2.0));
    color += mix(uSecondaryColor, uPrimaryColor, 0.58) * horizonHaze *
             (0.018 + uIntensity * 0.018) * (1.0 - luminance(color));

    // ── CRT scanline + phosphor mask overlay ───────────────────────
    // Layered on top of all 3D/post effects so the entire frame gets
    // the analog texture of a CRT display.
    vec3 scanlineMask = crtScanlines(uv, uIntensity);
    vec3 phosphorMask = crtPhosphorMask(uv);
    color *= scanlineMask * phosphorMask;

    // ── CRT quantization + Bayer dithering ─────────────────────────
    // Truncates to a 256-color VGA palette (3R:3G:2B) with Bayer
    // ordered dither to break up banding into a fine crosshatch.
    color = crtQuantize(color, gl_FragCoord.xy);

    // Lower scene exposure and a filmic shoulder preserve material color
    // in neon highlights instead of collapsing ships and towers into flat
    // white.
    color *= 0.86;
    color = (color * (2.51 * color + 0.03)) /
            (color * (2.43 * color + 0.59) + 0.14);
    float gradedLuma = luminance(color);
    color = mix(vec3(gradedLuma), color, 1.08);
    color = pow(max(color, vec3(0.0)), vec3(0.965));

    float vignette = 1.0 - smoothstep(0.26, 1.38, edge) * (0.22 - uIntensity * 0.05);
    color *= vignette;

    float seedPhase = float(uint(uRunSeed) & 1023u) * 0.067;
    float grain = fract(sin(dot(gl_FragCoord.xy + vec2(uTime * 17.0 + seedPhase),
                               vec2(12.9898, 78.233))) * 43758.5453) - 0.5;
    color += grain * 0.006;

    finalColor = vec4(clamp(color, 0.0, 0.985), 1.0) * fragColor;
}
