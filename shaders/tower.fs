#version 330

in vec3 fragPosition;
in vec3 fragNormal;
in vec3 fragLocalPos;
in vec4 fragColor;
in float fragDist;

out vec4 finalColor;

uniform vec4 uAccentColor;
uniform vec3 uBodyColor;
uniform float virtualPlayerZ;
uniform float uTime;
uniform float uIntensity;
uniform int uRunSeed;
uniform vec3 uPrimaryColor;
uniform vec3 uSecondaryColor;
uniform int uStructureKind;

void main()
{
    // Surface normal and directional light calculation
    vec3 N = normalize(fragNormal);
    vec3 lightDir = normalize(vec3(0.35, 0.85, 0.45));
    float diff = max(dot(N, lightDir), 0.0);
    float ambient = 0.30;

    // Cyberpunk accent color
    vec3 neonColor = mix(uAccentColor.rgb,
                         mix(uPrimaryColor, uSecondaryColor, 0.35), 0.28);
    float seedPhase = float(uint(uRunSeed) & 2047u) * 0.003067962;

    // View direction and smooth rim lighting
    vec3 viewDir = normalize(-fragPosition);
    float NdotV = max(dot(N, viewDir), 0.0);
    float rim = pow(1.0 - NdotV, 2.5);

    vec3 finalRGB;

    if (uStructureKind == 5) {
        // Corridor-facing data placards rendered as falling binary/matrix-style
        // data rain: independently scrolling columns with a bright leading glyph,
        // built entirely from hashes so no font or texture asset is needed.
        vec2 signUV = abs(N.x) > 0.55 ? fragLocalPos.zy + 0.5
                                      : fragLocalPos.xy + 0.5;
        float frameDistance = min(min(signUV.x, 1.0 - signUV.x),
                                  min(signUV.y, 1.0 - signUV.y));
        float frame = 1.0 - smoothstep(0.035, 0.075, frameDistance);
        float columns = 9.0;
        float colId = floor(signUV.x * columns);
        float colSpeed = 0.6 + fract(sin(colId * 91.71 + seedPhase * 13.0) * 43758.5453) * 1.6;
        float scrollY = signUV.y * 6.0 + uTime * colSpeed + seedPhase * 5.0;
        vec2 cell = vec2(colId, floor(scrollY));
        float cellCode = sin(dot(cell, vec2(12.9898, 78.233)));
        float glyph = step(0.5, fract(cellCode * 43758.5453));
        vec2 within = vec2(fract(signUV.x * columns), fract(scrollY));
        glyph *= smoothstep(0.12, 0.24, min(within.x, 1.0 - within.x)) *
                 smoothstep(0.12, 0.24, min(within.y, 1.0 - within.y));
        float leading = smoothstep(0.86, 1.0, within.y) * glyph;
        float scan = pow(max(sin(signUV.y * 44.0 - uTime * 3.2 + seedPhase), 0.0), 12.0);
        float header = smoothstep(0.76, 0.78, signUV.y) *
                       (1.0 - smoothstep(0.89, 0.91, signUV.y));
        vec3 signBase = mix(vec3(0.003, 0.008, 0.020), neonColor, 0.055);
        finalRGB = signBase + neonColor * (frame * 0.42 + glyph * 0.16 + leading * 0.55 +
                                           scan * 0.10 + header * 0.18);
    } else if (uStructureKind == 6) {
        // Classic multi-layer sine-wave plasma: several offset traveling waves
        // summed and mapped through the run palette, a direct demoscene staple.
        float t = uTime * (0.6 + uIntensity * 0.5);
        float p1 = sin(fragPosition.x * 0.6 + t);
        float p2 = sin(fragPosition.y * 0.5 - t * 1.3);
        float p3 = sin((fragPosition.x + fragPosition.y) * 0.35 + t * 0.7);
        float p4 = sin(length(fragPosition.xy) * 0.4 - t * 1.8 + seedPhase * 6.0);
        float plasma = (p1 + p2 + p3 + p4) * 0.25 * 0.5 + 0.5;
        vec3 plasmaColor = mix(uPrimaryColor, uSecondaryColor, plasma);
        finalRGB = mix(vec3(0.006, 0.012, 0.026), plasmaColor, 0.55 + 0.35 * diff) +
                   plasmaColor * rim * 0.22 + neonColor * pow(plasma, 6.0) * 0.30;
    } else if (uStructureKind == 11) {
        // Cheap raymarched fractal core: fold local space a few times and
        // accumulate glow along the interior-facing ray instead of a hard hit,
        // which stays forgiving/stable without a real camera-position uniform.
        vec3 rd = -N;
        vec3 p = fragLocalPos;
        float marched = 0.0;
        float glow = 0.0;
        for (int i = 0; i < 12; i++) {
            vec3 q = p + rd * marched;
            float scale = 1.0;
            q = abs(q) - 0.5;
            for (int f = 0; f < 3; f++) {
                q = abs(q) - (0.24 + 0.02 * sin(uTime * 0.31 + float(f)));
                q.xy = q.x > q.y ? q.xy : q.yx;
                q *= 1.9;
                scale *= 1.9;
            }
            float d = (length(max(abs(q) - 0.5, 0.0)) - 0.06) / scale;
            d = abs(d) + 0.012;
            glow += 0.018 / (0.02 + d * d * 46.0);
            marched += max(d, 0.015);
            if (marched > 1.2) break;
        }
        vec3 fractalColor = mix(vec3(0.008, 0.008, 0.018), neonColor, clamp(glow, 0.0, 1.0));
        finalRGB = fractalColor + neonColor * rim * 0.30;
    } else if (uStructureKind == 7) {
        // Recessed architectural glazing: dark glass, physical mullions and
        // a sparse set of dim occupied panes. It supplies scale without bloom.
        vec2 windowUV = fragLocalPos.zy + 0.5;
        vec2 tiled = windowUV * vec2(5.0, 3.0);
        vec2 paneUV = fract(tiled);
        vec2 paneId = floor(tiled);
        float borderDistance = min(min(paneUV.x, 1.0 - paneUV.x),
                                   min(paneUV.y, 1.0 - paneUV.y));
        float mullion = 1.0 - smoothstep(0.035, 0.085, borderDistance);
        float paneHash = fract(sin(dot(paneId + vec2(seedPhase * 19.0, seedPhase * 7.0),
                                       vec2(12.9898, 78.233))) * 43758.5453);
        float occupied = step(0.86, paneHash);
        vec3 glass = mix(vec3(0.006, 0.014, 0.022), uBodyColor, 0.22);
        finalRGB = glass * (0.70 + diff * 0.34) +
                   vec3(0.016, 0.024, 0.034) * mullion +
                   mix(uSecondaryColor, vec3(0.45, 0.52, 0.42), 0.28) * occupied *
                   (0.020 + uIntensity * 0.010);
    } else if (uStructureKind == 9) {
        // Deep ventilation louvers use alternating metal slats and shadow.
        vec2 ventUV = fragLocalPos.zy + 0.5;
        float slatPhase = fract(ventUV.y * 11.0);
        float slatFace = smoothstep(0.16, 0.28, slatPhase) *
                         (1.0 - smoothstep(0.68, 0.82, slatPhase));
        float sideFrame = 1.0 - smoothstep(0.035, 0.095,
                                           min(ventUV.x, 1.0 - ventUV.x));
        vec3 recess = mix(vec3(0.010, 0.014, 0.019), uBodyColor, 0.22);
        vec3 slatMetal = mix(vec3(0.030, 0.038, 0.046), uBodyColor, 0.40);
        finalRGB = mix(recess, slatMetal * (0.64 + diff * 0.30), slatFace) +
                   vec3(0.040, 0.048, 0.056) * sideFrame;
    } else if (uStructureKind == 10) {
        // Loading/entry aperture with a split door and heavy perimeter frame.
        vec2 doorUV = fragLocalPos.zy + 0.5;
        float edgeDistance = min(min(doorUV.x, 1.0 - doorUV.x),
                                 min(doorUV.y, 1.0 - doorUV.y));
        float frame = 1.0 - smoothstep(0.045, 0.105, edgeDistance);
        float split = 1.0 - smoothstep(0.012, 0.032, abs(doorUV.x - 0.5));
        float lintel = smoothstep(0.70, 0.73, doorUV.y) *
                       (1.0 - smoothstep(0.79, 0.82, doorUV.y));
        vec3 door = mix(vec3(0.010, 0.014, 0.020), uBodyColor, 0.26);
        finalRGB = door * (0.62 + diff * 0.26) +
                   vec3(0.046, 0.054, 0.063) * (frame * 0.78 + split * 0.30) +
                   uSecondaryColor * lintel * 0.018;
    } else if (uAccentColor.a > 1.5) {
        // =====================================================================
        // CONDUIT DATA STREAM PATH (Smooth, non-flickering luminescent neon)
        // =====================================================================
        // Steady luminous core and glowing tube base
        vec3 tubeBase = neonColor * (0.21 + 0.105 * diff);
        vec3 coreGlow = mix(neonColor, vec3(0.65, 0.84, 0.92), 0.34) * 0.050;

        // Long-wavelength, calm traveling data pulse (smooth sine harmonics)
        float streamCoord = (fragPosition.z + fragPosition.x * 0.4 + fragPosition.y * 0.3) * 0.04 -
                            (virtualPlayerZ * 0.012) - uTime * uIntensity * 0.14;
        float pulseWave1 = sin(streamCoord * 6.28318) * 0.5 + 0.5;
        float pulseWave2 = sin(streamCoord * 12.56636 + 1.2) * 0.5 + 0.5;
        float smoothPulse = pow(pulseWave1, 3.0) * 0.65 + pow(pulseWave2, 4.0) * 0.35;

        // Pulse energy shift: neon shifts toward bright cyan/white core at peak
        vec3 pulseEnergy = mix(neonColor, vec3(0.32, 0.76, 0.86), 0.46) * smoothPulse * 0.20;
        
        float boostCarrier = pow(max(sin(streamCoord * 31.4159 + seedPhase), 0.0), 9.0);
        finalRGB = tubeBase + coreGlow + pulseEnergy + neonColor * (rim * 0.24) +
                   uSecondaryColor * boostCarrier * uIntensity * 0.18;
    } else {
        // =====================================================================
        // HARDWARE MONOLITH & SLAB PATH (Obsidian body + anti-aliased neon edge)
        // =====================================================================
        vec3 absPos = abs(fragLocalPos);
        float max1 = max(max(absPos.x, absPos.y), absPos.z);
        float min1 = min(min(absPos.x, absPos.y), absPos.z);
        float max2 = absPos.x + absPos.y + absPos.z - max1 - min1;

        // Multi-tier smooth edge lines (anti-aliased, zero jitter)
        float edgeOuter = smoothstep(0.32, 0.485, max2) * 0.30;
        float edgeCore  = smoothstep(0.44, 0.490, max2) * 0.48;
        float boxEdge   = clamp(edgeOuter + edgeCore, 0.0, 1.0);

        // Cylinder Rim Profile (for capacitors/nodes)
        float cylRadius = length(fragLocalPos.xz);
        float cylOuter = smoothstep(0.32, 0.485, cylRadius) * smoothstep(0.35, 0.49, absPos.y) * 0.30;
        float cylCore  = smoothstep(0.44, 0.490, cylRadius) * smoothstep(0.44, 0.49, absPos.y) * 0.48;
        float cylEdge  = clamp(cylOuter + cylCore, 0.0, 1.0);

        float isCylinder = step(0.46, cylRadius) * (1.0 - step(0.47, max2));
        float edgeFactor = mix(boxEdge, cylEdge, isCylinder);

        // Dark obsidian chassis alloy with metallic specular sheen
        vec3 bodyTint = mix(vec3(0.018, 0.030, 0.058), neonColor, 0.105);
        vec3 baseBody = bodyTint * (0.62 + 0.48 * diff) +
                        uBodyColor * (ambient + 0.38 * diff);

        // Specular highlight on chassis top/edges
        vec3 halfDir = normalize(lightDir + viewDir);
        float spec = pow(max(dot(N, halfDir), 0.0), 20.0) * 0.32;

        // Structural secondary pieces are read by their geometry and shading,
        // so their edge treatment is deliberately much quieter than the main
        // tower chassis.
        float structuralDetail = uStructureKind == 8 ? 1.0 : 0.0;
        vec3 neonLines = neonColor * (edgeFactor * 0.54 + pow(edgeFactor, 2.0) * 0.18) *
                         mix(1.0, 0.18, structuralDetail);

        // World-scale panel seams and tiny window lanes give large surfaces a
        // material scale. Each structure family receives a different cadence.
        float kind = float(uStructureKind);
        float panelPhase = fragPosition.y * (0.20 + kind * 0.017) +
                           fragPosition.z * 0.017 + seedPhase;
        float panelSeam = 1.0 - smoothstep(0.018, 0.065,
                                           abs(fract(panelPhase) - 0.5));
        float verticalSeam = 1.0 - smoothstep(0.022, 0.075,
                                              abs(fract(fragLocalPos.x *
                                                       (3.0 + mod(kind, 3.0))) - 0.5));
        float windowLane = pow(max(sin(fragPosition.y * 1.32 +
                                       fragPosition.z * 0.11 + seedPhase), 0.0), 22.0) *
                           step(0.15, abs(N.x) + abs(N.z));


        float circuitA = sin((fragPosition.y * 1.9 + fragPosition.z * 0.21) + uTime * 3.0 + seedPhase);
        float circuitB = sin((fragPosition.x * 1.7 - fragPosition.z * 0.16) - uTime * 4.1);
        float circuitry = pow(max(circuitA * circuitB, 0.0), 12.0) * uIntensity *
                          (1.0 - structuralDetail);
        float scanBand = pow(max(sin(fragPosition.y * 3.4 - uTime * 7.0), 0.0), 18.0) *
                         uIntensity * (1.0 - structuralDetail);

        baseBody *= 1.0 - panelSeam * 0.20 - verticalSeam * 0.08;
        finalRGB = baseBody + neonLines + mix(uPrimaryColor, vec3(0.3, 0.7, 1.0), 0.45) * spec *
                   mix(0.42, 0.10, structuralDetail) +
                   neonColor * (rim * mix(0.25, 0.07, structuralDetail)) +
                   mix(uPrimaryColor, uSecondaryColor, circuitB * 0.5 + 0.5) * circuitry * 0.34 +
                   uPrimaryColor * scanBand * edgeFactor * 0.30 +
                   mix(neonColor, uSecondaryColor, 0.35) * windowLane *
                   (0.012 + uIntensity * 0.010) * (1.0 - structuralDetail);
    }

    // Atmospheric deep-indigo horizon fog
    vec3 fogColor = mix(vec3(0.008, 0.014, 0.038), uSecondaryColor, 0.025 + uIntensity * 0.018);
    float fogFactor = clamp(fragDist, 0.0, 1.0);
    finalRGB = mix(fogColor, finalRGB, fogFactor);

    finalColor = vec4(finalRGB, 1.0);
}
