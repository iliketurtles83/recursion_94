#version 330

in vec3 fragWorldPos;
in vec2 fragTexCoord;
in vec3 fragNormal;

out vec4 finalColor;

uniform float virtualPlayerZ;
uniform float uTime;
uniform float uIntensity;
uniform int uRunSeed;
uniform vec3 uPrimaryColor;
uniform vec3 uSecondaryColor;

// Integer hash shared with environment.c. Matching the CPU implementation lets
// submerged structures target cells that this shader actually discards.
uint hashUint(uint x) {
    x = ((x >> 16u) ^ x) * 0x045d9f3bu;
    x = ((x >> 16u) ^ x) * 0x045d9f3bu;
    return (x >> 16u) ^ x;
}

float hashCell(ivec2 cell) {
    uint key = uint(cell.x) * 0x8da6b343u ^
               uint(cell.y) * 0xd8163841u ^
               0xcb1ab31fu ^
               hashUint(uint(uRunSeed) ^ 0x6d2b79f5u);
    return float(hashUint(key) & 0x00ffffffu) / 16777215.0;
}

void main()
{
    // Continuous coordinates aligned with world momentum
    vec2 pos = vec2(fragWorldPos.x, fragWorldPos.z - virtualPlayerZ);

    // -------------------------------------------------------------------------
    // Gap Mask / Sunken Sub-Floor Grate Discard
    // -------------------------------------------------------------------------
    float cellSize = 16.0;
    float absoluteZ = virtualPlayerZ - fragWorldPos.z;
    vec2 cellPos = vec2(fragWorldPos.x, absoluteZ) / cellSize;
    ivec2 cellId = ivec2(floor(cellPos));
    vec2 cellUV = fract(cellPos);

    float cellH = hashCell(cellId);
    float corridorGuard = smoothstep(16.0, 18.5, abs(fragWorldPos.x));
    float rimGlow = 0.0;

    if (cellH < 0.13 && corridorGuard > 0.01) {
        vec2 d = abs(cellUV - vec2(0.5));
        float boxDist = max(d.x, d.y);
        float holeSize = 0.42 * corridorGuard;

        if (boxDist < holeSize) {
            discard;
        }

        rimGlow = smoothstep(holeSize + 0.07, holeSize, boxDist) * corridorGuard;
    }

    // 1. Base Dark Obsidian-Silicon PCB Substrate
    vec3 baseColor = vec3(0.016, 0.024, 0.045);

    // 2. Primary PCB Bus Traces (Electric Cyan-Teal)
    vec2 f6 = fract(pos / 12.0);
    float busX = smoothstep(0.045, 0.012, abs(f6.x - 0.5));
    float busZ = smoothstep(0.045, 0.012, abs(f6.y - 0.5));
    float primaryTraces = clamp(busX + busZ, 0.0, 1.0);

    // Flight-corridor language: broken lane guides, shoulder ticks and angled
    // data chevrons make the ground read as an engineered transit surface.
    float laneGuide = 1.0 - smoothstep(0.10, 0.24, abs(abs(pos.x) - 5.2));
    float laneDash = smoothstep(0.15, 0.24, fract(-pos.y / 7.0)) *
                     (1.0 - smoothstep(0.70, 0.82, fract(-pos.y / 7.0)));
    float shoulderGuide = 1.0 - smoothstep(0.10, 0.23, abs(abs(pos.x) - 12.4));
    float shoulderTick = step(0.58, fract(-pos.y / 3.8));
    float corridorMask = 1.0 - smoothstep(8.0, 11.0, abs(pos.x));
    float chevronPhase = abs(fract(abs(pos.x) * 0.074 - pos.y * 0.031) - 0.5);
    float chevrons = (1.0 - smoothstep(0.045, 0.082, chevronPhase)) *
                     corridorMask * step(0.76, fract(-pos.y / 18.0));

    // 3. Secondary PCB Micro Sub-Traces (Deep Azure)
    vec2 f15 = fract(pos / 3.0);
    float subX = smoothstep(0.040, 0.012, abs(f15.x - 0.5));
    float subZ = smoothstep(0.040, 0.012, abs(f15.y - 0.5));
    float subTraces = clamp(subX + subZ, 0.0, 1.0) * 0.25;

    // 4. Hexagonal Micro-Mesh Pattern (Subtle Midnight Indigo)
    vec2 hexP = pos * 0.25;
    const vec2 hS = vec2(1.0, 1.7320508);
    vec4 hC = floor(vec4(hexP, hexP - vec2(0.5, 1.0)) / hS.xyxy);
    vec4 h = hexP.xyxy - vec4(hC.xy + 0.5, hC.zw + 1.0) * hS.xyxy;
    vec2 hA = mix(h.xy, h.zw, step(dot(h.xy, h.xy), dot(h.zw, h.zw)));
    float hexDist = max(abs(hA.x), abs(hA.x) * 0.5 + abs(hA.y) * 0.8660254);
    float hexGrid = smoothstep(0.40, 0.48, hexDist) * (1.0 - smoothstep(0.48, 0.54, hexDist));

    // 5. IC Junction Vias / Nodes at Grid Intersections (Electric Blue & Ice Cyan)
    float nodeDist = length(f6 - vec2(0.5));
    float nodeRing = smoothstep(0.135, 0.105, nodeDist) * smoothstep(0.048, 0.072, nodeDist);
    float nodeCore = smoothstep(0.046, 0.015, nodeDist);

    // Seed-selected palette keeps every run visually related to its sound.
    vec3 traceColor = uPrimaryColor * 0.42;
    vec3 subTraceColor = mix(vec3(0.015, 0.035, 0.08), uSecondaryColor, 0.24);
    vec3 nodeRingColor = mix(uPrimaryColor, uSecondaryColor, 0.34) * 0.64;
    vec3 nodeCoreColor = mix(vec3(0.38, 0.66, 0.80), uPrimaryColor, 0.52);
    vec3 hexColor = mix(vec3(0.012, 0.025, 0.065), uSecondaryColor, 0.10);

    // Composite surface pattern
    vec3 patternColor = traceColor * primaryTraces +
                        subTraceColor * subTraces +
                        hexColor * hexGrid * 0.40 +
                        nodeRingColor * nodeRing * 0.62 +
                        nodeCoreColor * nodeCore * 0.68 +
                        mix(uPrimaryColor, uSecondaryColor, 0.22) *
                        (laneGuide * laneDash * 0.13 +
                         shoulderGuide * shoulderTick * 0.055 + chevrons * 0.018);

    // 6. Lighting & Specular Reflection
    vec3 N = normalize(fragNormal);
    vec3 lightDir = normalize(vec3(0.35, 0.85, 0.45));
    float diff = max(dot(N, lightDir), 0.35);

    vec3 viewDir = normalize(vec3(0.0, 9.0, 15.0) - fragWorldPos);
    vec3 halfDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(N, halfDir), 0.0), 16.0);
    float fresnel = pow(1.0 - max(dot(N, viewDir), 0.0), 2.5);

    // 7. Smooth, non-flickering Ground Data Pulses
    float pulseCoord = mod(pos.y * 0.12 + pos.x * 0.04 - (virtualPlayerZ * 0.015), 28.0);
    float pulse1 = smoothstep(0.0, 3.5, pulseCoord) * (1.0 - smoothstep(3.5, 9.0, pulseCoord));
    vec3 dataPulse = mix(uPrimaryColor, uSecondaryColor, 0.28) *
                     pulse1 * (primaryTraces + nodeRing + 0.3) *
                     (0.42 + uIntensity * 0.32);

    // Moire and traveling raster energy: classic demoscene interference kept
    // subtle at cruise and revealed by intensity/boost.
    float seedPhase = float(uint(uRunSeed) & 2047u) * 0.003067962;
    float radial = length(pos * vec2(0.085, 0.032));
    float moireA = sin(radial * 7.0 - uTime * 3.1 + seedPhase);
    float moireB = sin(pos.x * 0.22 - pos.y * 0.075 + uTime * 4.7);
    float interference = pow(max(moireA * moireB, 0.0), 3.0);
    float raster = pow(max(sin(pos.y * 0.42 - uTime * 8.0), 0.0), 10.0);
    vec3 demosceneEnergy = mix(uPrimaryColor, uSecondaryColor,
                               0.5 + 0.5 * moireB) *
                           (interference * (0.025 + uIntensity * 0.16) +
                            raster * primaryTraces * uIntensity * 0.13);

    // Hole rim emissive highlight
    vec3 rimEmissive = (traceColor * 1.12 + vec3(0.05, 0.16, 0.25)) * rimGlow;

    // Final color assembly
    vec3 finalRGB = (baseColor * diff) + patternColor * (0.58 + 0.34 * fresnel) +
                    mix(uPrimaryColor, vec3(0.4, 0.7, 1.0), 0.45) * spec * 0.30 +
                    dataPulse + demosceneEnergy + rimEmissive;

    // 8. Atmospheric deep-indigo horizon fog
    vec3 fogColor = mix(vec3(0.008, 0.014, 0.038), uSecondaryColor, 0.025 + uIntensity * 0.018);
    float fadeIn = smoothstep(-375.0, -180.0, fragWorldPos.z);
    float fadeOut = 1.0 - smoothstep(10.0, 25.0, fragWorldPos.z);
    float fogFactor = clamp(fadeIn * fadeOut, 0.0, 1.0);

    finalRGB = mix(fogColor, finalRGB, fogFactor);

    finalColor = vec4(finalRGB, 1.0);
}
