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

void main() {
    vec2 uv = fragTexCoord;
    vec2 pixel = 1.0 / uResolution;
    vec2 centered = uv * 2.0 - 1.0;
    float edge = dot(centered, centered);

    // One-shot hexagonal rift wave used only during the boss approach. Unlike
    // the removed beat rings, it expands once and clears before combat starts.
    float hexDistance = max(abs(centered.y),
                            abs(centered.x) * 0.8660254 + abs(centered.y) * 0.5);
    float riftRadius = uBossTransition * 1.28;
    float riftWave = (1.0 - smoothstep(0.0, 0.055,
                                      abs(hexDistance - riftRadius))) *
                     step(0.0001, uBossTransition);
    vec2 riftDirection = normalize(centered + vec2(0.0001));
    uv += riftDirection * riftWave * (1.0 - uBossTransition) * 0.018;

    // Compact edge-aware antialiasing smooths diagonal craft silhouettes and
    // distant architecture after the 3D pass without requiring another target.
    vec3 source = texture(texture0, uv).rgb;
    vec3 north = texture(texture0, uv + vec2(0.0, pixel.y)).rgb;
    vec3 south = texture(texture0, uv - vec2(0.0, pixel.y)).rgb;
    vec3 east = texture(texture0, uv + vec2(pixel.x, 0.0)).rgb;
    vec3 west = texture(texture0, uv - vec2(pixel.x, 0.0)).rgb;
    float lumaCenter = luminance(source);
    float lumaMin = min(lumaCenter, min(min(luminance(north), luminance(south)),
                                        min(luminance(east), luminance(west))));
    float lumaMax = max(lumaCenter, max(max(luminance(north), luminance(south)),
                                        max(luminance(east), luminance(west))));
    float edgeAA = smoothstep(0.055, 0.22, lumaMax - lumaMin) * 0.58;
    vec3 color = mix(source, (north + south + east + west) * 0.25, edgeAA);

    // Restrained sub-pixel color separation gives luminous edges a cinematic
    // fringe while leaving dark body panels and textural detail intact.
    vec2 chromaOffset = centered * pixel * (0.42 + uIntensity * 0.76);
    vec3 chroma = vec3(texture(texture0, uv + chromaOffset).r,
                       texture(texture0, uv).g,
                       texture(texture0, uv - chromaOffset).b);
    color = mix(color, chroma, 0.34);

    vec2 nearTap = pixel * (1.6 + uIntensity * 1.4);
    vec2 farTap = pixel * (4.2 + uIntensity * 2.5);
    vec3 bloom = bloomSample(uv + vec2( nearTap.x, 0.0)) +
                 bloomSample(uv + vec2(-nearTap.x, 0.0)) +
                 bloomSample(uv + vec2(0.0,  nearTap.y)) +
                 bloomSample(uv + vec2(0.0, -nearTap.y));
    bloom += (bloomSample(uv + vec2( farTap.x,  farTap.y)) +
              bloomSample(uv + vec2(-farTap.x,  farTap.y)) +
              bloomSample(uv + vec2( farTap.x, -farTap.y)) +
              bloomSample(uv + vec2(-farTap.x, -farTap.y))) * 0.62;
    bloom *= 0.128;

    // Very low-energy horizontal highlight streaks give headlights, signage
    // and weapon impacts a cinematic lens response without whitening surfaces.
    vec3 anamorphic = bloomSample(uv + vec2(pixel.x * 9.0, 0.0)) +
                      bloomSample(uv - vec2(pixel.x * 9.0, 0.0)) +
                      bloomSample(uv + vec2(pixel.x * 19.0, 0.0)) * 0.55 +
                      bloomSample(uv - vec2(pixel.x * 19.0, 0.0)) * 0.55;

    // A restrained radial echo appears at higher intensity and reads as
    // acceleration while keeping the center/reticle stable.
    float speedBlur = smoothstep(0.62, 1.0, uIntensity) * 0.085;
    vec2 radialStep = centered * pixel * (2.0 + uIntensity * 5.0);
    vec3 radial = texture(texture0, uv - radialStep).rgb +
                  texture(texture0, uv - radialStep * 2.4).rgb;
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

    float screenY = gl_FragCoord.y / uResolution.y;
    float horizonHaze = exp(-pow((screenY - 0.53) * 4.1, 2.0));
    color += mix(uSecondaryColor, uPrimaryColor, 0.58) * horizonHaze *
             (0.018 + uIntensity * 0.018) * (1.0 - luminance(color));

    // Lower scene exposure and a filmic shoulder preserve material color in
    // neon highlights instead of collapsing ships and towers into flat white.
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
