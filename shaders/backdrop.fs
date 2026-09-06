#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

out vec4 finalColor;

uniform vec2 uResolution;
uniform float uTime;
uniform float uIntensity;
uniform int uRunSeed;
uniform vec3 uPrimaryColor;
uniform vec3 uSecondaryColor;
uniform vec3 uSkyZenith;
uniform vec3 uSkyHorizon;
uniform vec3 uBodyColor;

uint hashUint(uint value) {
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

float hash01(uint value) {
    return float(hashUint(value) & 0x00ffffffu) / 16777215.0;
}

float sdBox(vec2 point, vec2 halfSize) {
    vec2 offset = abs(point) - halfSize;
    return length(max(offset, 0.0)) + min(max(offset.x, offset.y), 0.0);
}

float marchCage(vec3 rayDirection, vec3 origin, float foldOffset,
                float foldScale, int marchSteps, int foldSteps) {
    float marched = 0.55;
    float glow = 0.0;
    for (int i = 0; i < 13; i++) {
        if (i >= marchSteps) break;
        vec3 q = origin + rayDirection * marched;
        float scale = 1.0;
        q = abs(q) - 1.05;
        for (int fold = 0; fold < 4; fold++) {
            if (fold >= foldSteps) break;
            q = abs(q) - (foldOffset + 0.045 *
                sin(uTime * 0.23 + float(fold) + foldOffset * 9.0));
            q.xy = q.x > q.y ? q.xy : q.yx;
            q.yz = q.y > q.z ? q.yz : q.zy;
            q *= foldScale;
            scale *= foldScale;
        }
        float distanceToCage =
            (length(max(abs(q) - 0.9, 0.0)) - 0.14) / scale;
        distanceToCage = abs(distanceToCage) + 0.02;
        glow += 0.020 / (0.05 + distanceToCage * distanceToCage * 34.0);
        marched += max(distanceToCage * 0.68, 0.05);
        if (marched > 22.0) break;
    }
    return clamp(glow, 0.0, 1.6);
}

// ── Polar coordinate tunnel ────────────────────────────────────────
// The iconic 1990s demoscene infinite tunnel. Transforms screen-space
// Cartesian UVs into polar coordinates (angle + inverse radius) so
// that advancing 'time' along the radial axis creates continuous
// forward motion through an infinite cylindrical tunnel.
vec3 renderPolarTunnel(vec2 uv, float time, float intensity) {
    // Organic center drift — the tunnel curves slightly as you fly
    vec2 center = vec2(sin(time * 0.18) * 0.08,
                       cos(time * 0.13) * 0.06);
    vec2 d = uv - center;

    // Polar coordinates: angle in [-1,1], radius = 1/distance from center
    float angle = atan(d.y, d.x) / 3.14159265;
    float dist = 0.5 / (length(d) + 0.001);

    // Tunnel UV: angle creates the circular wrapping, dist+time creates
    // the forward-scrolling perspective. Add sinusoidal warp for organic
    // tunnel curvature.
    float tunnelAngle = angle * 3.0 + sin(dist * 0.5 + time * 0.3) * 0.15;
    float tunnelDist = dist + time * 1.2;

    // Grid pattern on the tunnel walls — classic demoscene checkerboard
    vec2 grid = step(0.5, fract(vec2(tunnelAngle, tunnelDist) * 4.0));
    float checker = mod(grid.x + grid.y, 2.0);

    // Depth fade: closer to center = deeper = darker
    float depthFade = clamp(length(d) * 2.0, 0.0, 1.0);
    vec3 baseColor = mix(vec3(0.05, 0.0, 0.15), vec3(0.0, 0.8, 0.9), checker);

    return baseColor * depthFade;
}

// ── Multi-frequency sine plasma ────────────────────────────────────
// The earliest algorithmic mainstay of the demoscene. Sum of multiple
// sinusoidal functions across space and time creates shifting atmospheric
// visual bands. When mapped through the run palette, produces the
// characteristic liquid color waves of 1980s/90s intros.
vec3 renderPlasma(vec2 uv, float time, float intensity) {
    float t = time * 0.4;

    // Five sine waves at different frequencies and orientations
    float p1 = sin(uv.x * 3.0 + t);
    float p2 = sin(uv.y * 2.5 - t * 1.2);
    float p3 = sin((uv.x + uv.y) * 2.0 + t * 0.7);
    float p4 = sin(length(uv) * 2.0 - t * 1.5);
    float p5 = sin((uv.x - uv.y) * 1.5 + t * 0.9);

    float plasma = (p1 + p2 + p3 + p4 + p5) * 0.20;

    // Map through the run palette
    float phase = plasma * 0.5 + 0.5;
    vec3 plasmaColor = mix(uPrimaryColor, uSecondaryColor, phase);

    // Scale with intensity — plasma is subtle at low intensity, dominant at high
    float plasmaStrength = 0.08 + intensity * 0.15;
    return plasmaColor * plasmaStrength;
}

vec2 citySilhouette(vec2 screenPoint, uint seed) {
    const float ground = 0.43;
    float cityDistance = sdBox(screenPoint - vec2(0.0, ground * 0.5 - 0.18),
                               vec2(1.4, ground * 0.5 + 0.18));
    float glints = 0.0;
    float density = smoothstep(0.12, 0.58, uIntensity);

    for (int building = 0; building < 21; building++) {
        uint buildingHash = hashUint(seed ^ (uint(building) + 1u) * 0x9e3779b9u);
        float center = -1.10 + float(building) * 0.11;
        center += (hash01(buildingHash) - 0.5) * 0.025;
        float centrality = 1.0 - smoothstep(0.08, 1.08, abs(center));
        float height = 0.07 + centrality * 0.16 +
                       hash01(buildingHash ^ 0x51c3u) * 0.09;
        float halfWidth = 0.035 + hash01(buildingHash ^ 0xa711u) * 0.024;
        float reveal = step(hash01(buildingHash ^ 0xc17au), 0.52 + density * 0.48);
        float buildingDistance = sdBox(screenPoint - vec2(center, ground + height * 0.5),
                                       vec2(halfWidth, height * 0.5));
        cityDistance = min(cityDistance, mix(1.0, buildingDistance, reveal));

        vec2 windowCell = fract((screenPoint - vec2(center, ground)) /
                                vec2(0.026, 0.032)) - 0.5;
        float inBuilding = step(buildingDistance, -0.008) * reveal;
        uint windowRow = uint(max(floor((screenPoint.y - ground) * 150.0) + 37.0, 0.0));
        float litWindow = step(0.38, hash01(buildingHash ^ windowRow));
        glints += inBuilding * litWindow * density *
                  (1.0 - smoothstep(0.22, 0.38, length(windowCell)));
    }

    float towerX = (hash01(seed ^ 0x746f7765u) - 0.5) * 0.10;
    float towerHeight = 0.43 + hash01(seed ^ 0x94d31a7bu) * 0.06;
    float towerBody = sdBox(screenPoint - vec2(towerX, ground + towerHeight * 0.42),
                            vec2(0.072, towerHeight * 0.42));
    float towerCrown = sdBox(screenPoint - vec2(towerX, ground + towerHeight * 0.89),
                             vec2(0.041, towerHeight * 0.09));
    cityDistance = min(cityDistance, min(towerBody, towerCrown));
    return vec2(cityDistance, clamp(glints, 0.0, 1.0));
}

vec3 cyclePalette(float phase, float height) {
    // Cyberspace atmospheric dome: deep void at zenith, subtle glowing horizon
    vec3 zenith = uSkyZenith;
    vec3 horizon = uSkyHorizon;

    // Subtle atmospheric wave over time (breathing twilight horizon)
    float wave = sin(phase * 6.2831853) * 0.5 + 0.5;
    vec3 low = mix(horizon, horizon * 1.25 + uPrimaryColor * 0.05, wave);
    vec3 high = mix(zenith, zenith * 1.1 + uSecondaryColor * 0.02, wave);

    return mix(low, high, smoothstep(0.02, 0.92, height));
}

void main() {
    vec2 coord = (uResolution.x > 1.0 && uResolution.y > 1.0)
        ? vec2(gl_FragCoord.x / uResolution.x, 1.0 - gl_FragCoord.y / uResolution.y)
        : fragTexCoord;
    vec2 uv = coord * 2.0 - 1.0;
    uv.x *= uResolution.x / max(uResolution.y, 1.0);

    uint seed = uint(uRunSeed);
    float seedPhase = float(seed & 2047u) * 0.003067962;
    float foldOffset = mix(0.56, 0.68, hash01(seed ^ 0x464f4c44u));
    float foldScale = mix(1.74, 1.92, hash01(seed ^ 0x5343414cu));
    float spinSpeed = mix(0.036, 0.064, hash01(seed ^ 0x5350494eu));
    float spin = uTime * spinSpeed + seedPhase * 6.2831853;
    float cs = cos(spin);
    float sn = sin(spin);

    vec3 rd = normalize(vec3(uv, 1.35));
    rd.xz = mat2(cs, -sn, sn, cs) * rd.xz;

    float glow = marchCage(rd, vec3(0.0, 0.0, uTime * 0.12),
                           foldOffset, foldScale, 13, 4);

    float farSpin = -uTime * spinSpeed * 0.34 + seedPhase * 3.1;
    float farCs = cos(farSpin);
    float farSn = sin(farSpin);
    vec3 farDirection = normalize(vec3(uv * 0.82 + vec2(0.12, -0.04), 1.62));
    farDirection.xy = mat2(farCs, -farSn, farSn, farCs) * farDirection.xy;
    float farGlow = marchCage(farDirection, vec3(0.35, -0.18, uTime * 0.035),
                              foldOffset * 0.93, foldScale * 0.96, 8, 3);

    float cycle = fract(uTime / 96.0 + seedPhase * 0.08);
    float twilight = pow(abs(sin(cycle * 6.2831853)), 8.0);
    vec3 fractalColor = mix(uPrimaryColor, uSecondaryColor,
                            sin(glow * 2.8 + uTime * 0.4) * 0.5 + 0.5) * glow;
    vec3 farColor = mix(uSecondaryColor, uPrimaryColor, 0.22) * farGlow * 0.20;

    // Horizon glow mask centered at horizon
    float horizonMask = exp(-pow((coord.y - 0.48) * 2.4, 2.0));
    float skyHeight = clamp(1.0 - coord.y * 1.4, 0.0, 1.0);
    vec3 base = cyclePalette(cycle, skyHeight);
    float digitalBands = step(0.72, fract(sin(floor(coord.y * 72.0) +
                              seedPhase * 31.0) * 43758.5453));
    vec3 sunsetSignal = mix(uSecondaryColor, uPrimaryColor,
                            step(0.5, fract(coord.y * 31.0 + seedPhase)));
    base += sunsetSignal * digitalBands * twilight * horizonMask * 0.045;

    vec3 sky = base + (fractalColor * 1.3 + farColor) * horizonMask *
                      (0.60 + twilight * 0.40);
    vec2 city = citySilhouette(vec2(coord.x * 2.0 - 1.0,
                                    1.0 - coord.y), seed);
    float silhouette = 1.0 - smoothstep(-0.002, 0.003, city.x);
    vec3 silhouetteColor = mix(uBodyColor * 0.5,
                               uSecondaryColor * 0.035, uIntensity);
    sky = mix(sky, silhouetteColor, silhouette * 0.70);
    sky += mix(uPrimaryColor, uSecondaryColor, 0.45) * city.y * silhouette *
           (0.025 + uIntensity * 0.085);

    // ── Polar tunnel overlay (upper sky region) ────────────────────
    // Visible above the horizon line, blending into the sky. Creates
    // the infinite "sucked in" tunnel feeling that defines 1990s demos.
    vec2 tunnelUV = (coord * 2.0 - 1.0);
    tunnelUV.x *= uResolution.x / max(uResolution.y, 1.0);
    vec3 tunnel = renderPolarTunnel(tunnelUV, uTime, uIntensity);
    // Only show in upper portion, fade out near horizon
    float tunnelMask = smoothstep(0.55, 0.95, coord.y) * (0.15 + uIntensity * 0.25);
    sky = mix(sky, sky + tunnel * 0.6, tunnelMask);

    // ── Multi-frequency plasma overlay ─────────────────────────────
    // Subtle atmospheric color waves across the entire backdrop.
    // Most visible in the sky region, adds liquid color movement.
    vec2 plasmaUV = (coord * 2.0 - 1.0);
    plasmaUV.x *= uResolution.x / max(uResolution.y, 1.0);
    vec3 plasma = renderPlasma(plasmaUV, uTime, uIntensity);
    float plasmaMask = smoothstep(0.45, 0.95, coord.y) * (0.3 + uIntensity * 0.4);
    sky = mix(sky, plasma, plasmaMask);

    finalColor = vec4(sky, 1.0) * fragColor;
}
