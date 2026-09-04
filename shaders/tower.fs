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

int getGlyphBitmap(int idx) {
    idx = idx & 15;
    if (idx == 0) return 31599; // 0
    if (idx == 1) return 11415; // 1
    if (idx == 2) return 29671; // 2
    if (idx == 3) return 29647; // 3
    if (idx == 4) return 23497; // 4
    if (idx == 5) return 31183; // 5
    if (idx == 6) return 31215; // 6
    if (idx == 7) return 29330; // 7
    if (idx == 8) return 31727; // 8
    if (idx == 9) return 31695; // 9
    if (idx == 10) return 11245; // A
    if (idx == 11) return 27566; // B
    if (idx == 12) return 31015; // C
    if (idx == 13) return 27502; // D
    if (idx == 14) return 31207; // E
    return 31204; // 15: F
}

float evaluateGlyph(int charIdx, vec2 cellUV) {
    if (cellUV.x < 0.08 || cellUV.x > 0.92 || cellUV.y < 0.06 || cellUV.y > 0.94) return 0.0;
    int px = clamp(int((cellUV.x - 0.08) / 0.84 * 3.0), 0, 2);
    int py = clamp(int((cellUV.y - 0.06) / 0.88 * 5.0), 0, 4);
    int bitPos = 14 - (py * 3 + px);
    int bitmap = getGlyphBitmap(charIdx);
    return float((bitmap >> bitPos) & 1);
}

vec3 renderMatrixRain(vec2 uv, float widthMeters, float heightMeters, float time, float seedPhase, float phaseOffset, vec3 textColor) {
    // Glowing edge frame bordering the active terminal screen
    float borderDist = min(min(uv.x, 1.0 - uv.x), min(uv.y, 1.0 - uv.y));
    float frameLine = smoothstep(0.008, 0.022, borderDist) * (1.0 - smoothstep(0.022, 0.040, borderDist));
    float contentMask = smoothstep(0.025, 0.050, borderDist);

    // Bold column and row dimensions: ~0.45m to 0.55m per character for clear distance visibility
    float cols = clamp(floor(max(widthMeters, 1.5) * 1.8), 4.0, 12.0);
    float colId = floor(uv.x * cols);
    float colU = fract(uv.x * cols);

    float rows = clamp(floor(max(heightMeters, 1.2) * 1.7), 3.0, 8.0);
    float colHash = fract(sin(colId * 91.71 + seedPhase * 13.0 + phaseOffset * 7.0) * 43758.5453);
    float colSpeed = 1.2 + colHash * 1.5;

    // Fast, continuous cascade of code
    float scrollY = (1.0 - uv.y) * rows + time * colSpeed + phaseOffset * 6.0 + colHash * 9.0;
    float rowId = floor(scrollY);
    float rowV = fract(scrollY);

    float cellSeed = fract(sin(colId * 12.9898 + rowId * 78.233 + seedPhase * 5.0) * 43758.5453);
    float mutate = floor(time * 6.0 + cellSeed * 7.0);
    int charIdx = int(abs(sin(cellSeed * 43.0 + mutate * 0.23) * 16.0)) % 16;

    float glyph = evaluateGlyph(charIdx, vec2(colU, rowV));

    // Stream drop mechanics: compact 5-8 char cycle with brilliant white head and long luminous tail
    float streamLen = 5.0 + colHash * 4.0;
    float dropPos = fract(scrollY / streamLen); // 0.0 at tail end, 1.0 at head
    float head = smoothstep(0.80, 1.0, dropPos);
    float tail = pow(dropPos, 1.5);
    float streamIntensity = tail * 1.5 + head * 3.0;

    vec3 brightHead = vec3(1.0, 1.0, 1.0);
    vec3 matrixGreen = mix(textColor, vec3(0.15, 1.0, 0.55), 0.75);
    vec3 glyphColor = mix(matrixGreen, brightHead, head * 0.95);

    // Lit character + ambient phosphor trace so empty cells aren't pitch black
    float charLum = glyph * (streamIntensity + 0.35);
    vec3 charLit = glyphColor * charLum;
    vec3 trailGlow = matrixGreen * (tail * 0.22);
    vec3 screenBackdrop = vec3(0.004, 0.020, 0.014); // Cyber-terminal dark phosphor glass
    vec3 frameColor = matrixGreen * (frameLine * 1.6);

    return (screenBackdrop + charLit + trailGlow) * contentMask + frameColor;
}

vec3 renderBinaryFlow(vec2 uv, float widthMeters, float heightMeters, float time, float seedPhase, float phaseOffset, vec3 color) {
    // Edge guide borders for the vertical data waterfall
    float borderDist = min(uv.x, 1.0 - uv.x);
    float railLine = smoothstep(0.010, 0.030, borderDist) * (1.0 - smoothstep(0.030, 0.060, borderDist));
    float contentMask = smoothstep(0.025, 0.060, borderDist);

    // 2 to 3 bold, large binary columns down the tower face
    float cols = clamp(floor(max(widthMeters, 1.2) * 0.75), 2.0, 3.0);
    float colId = floor(uv.x * cols);
    float colU = fract(uv.x * cols);

    // Large, prominent rows: 6 to 12 characters tall across tower height
    float rows = clamp(floor(max(heightMeters, 2.0) * 0.60), 6.0, 12.0);
    float colHash = fract(sin(colId * 83.17 + seedPhase * 11.0 + phaseOffset * 5.0) * 43758.5453);
    float colSpeed = 1.3 + colHash * 1.4;

    // High-speed vertical cascade
    float scrollY = (1.0 - uv.y) * rows + time * colSpeed + phaseOffset * 7.0 + colHash * 13.0;
    float rowId = floor(scrollY);
    float rowV = fract(scrollY);

    // Pure binary: strictly 0 or 1!
    float bitSeed = fract(sin(colId * 17.13 + rowId * 91.41 + seedPhase * 3.0) * 43758.5453);
    float bitFlip = floor(time * 6.0 + bitSeed * 9.0);
    int isOne = int(floor(bitSeed * 2.0 + bitFlip * 0.5)) & 1;

    float glyph = evaluateGlyph(isOne, vec2(colU, rowV));

    // Stream drops cascading down the tall tower
    float streamLen = 4.0 + colHash * 3.0;
    float dropPos = fract(scrollY / streamLen);
    float head = smoothstep(0.72, 1.0, dropPos);
    float tail = pow(dropPos, 1.3);
    float streamIntensity = tail * 1.8 + head * 3.5;

    vec3 cyanColor = mix(color, vec3(0.18, 0.88, 1.0), 0.70);
    vec3 whiteHead = vec3(1.0, 1.0, 1.0);
    vec3 glyphColor = mix(cyanColor, whiteHead, head * 0.95);

    float charLum = glyph * (streamIntensity + 0.35);
    vec3 charLit = glyphColor * charLum;
    vec3 trailGlow = cyanColor * (tail * 0.22);
    vec3 rail = cyanColor * (railLine * 1.8);
    vec3 backdrop = vec3(0.003, 0.012, 0.024);

    return (backdrop + charLit + trailGlow) * contentMask + rail;
}

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
        // Corridor-facing data placards rendered with 3x5 font Matrix data-rain
        vec2 signUV = abs(N.x) > 0.55 ? fragLocalPos.zy + 0.5
                                      : fragLocalPos.xy + 0.5;
        vec3 rain = renderMatrixRain(signUV, 2.6, 2.0, uTime, seedPhase, 0.0, neonColor);
        vec3 signBase = mix(vec3(0.003, 0.008, 0.020), neonColor, 0.055);
        finalRGB = signBase + rain;
    } else if (uStructureKind == 2) {
        // =====================================================================
        // MEMORY SLAB (Selective data-rain on corridor-facing or front faces)
        // =====================================================================
        vec3 absPos = abs(fragLocalPos);
        float max1 = max(max(absPos.x, absPos.y), absPos.z);
        float min1 = min(min(absPos.x, absPos.y), absPos.z);
        float max2 = absPos.x + absPos.y + absPos.z - max1 - min1;

        float edgeOuter = smoothstep(0.32, 0.485, max2) * 0.30;
        float edgeCore  = smoothstep(0.44, 0.490, max2) * 0.48;
        float boxEdge   = clamp(edgeOuter + edgeCore, 0.0, 1.0);

        vec3 baseBody = mix(vec3(0.002, 0.005, 0.010), neonColor, 0.05);

        vec3 halfDir = normalize(lightDir + viewDir);
        float spec = pow(max(dot(N, halfDir), 0.0), 32.0) * 0.8;

        float edgePulse = sin(fragPosition.y * 2.0 - uTime * 5.0 + seedPhase) * 0.5 + 0.5;
        vec3 neonLines = neonColor * (boxEdge * 1.5 + pow(boxEdge, 2.0) * 2.0) * (0.8 + edgePulse * 0.4);

        // Memory slabs display falling matrix code exclusively on designated faces:
        // Corridor-facing inner side (faces toward the player corridor X=0)
        bool isCorridorFace = (fragPosition.x < 0.0 ? N.x > 0.45 : N.x < -0.45);
        // Oncoming front face (faces oncoming traffic toward +Z)
        bool isFrontFace = (N.z > 0.45);

        // Deterministic per-slab variation: stable across treadmill movement
        float slabSeed = fract(sin(floor(fragPosition.x * 0.25) * 127.1 +
                                   floor((fragPosition.z + virtualPlayerZ) * 0.0625) * 311.7 +
                                   float(uRunSeed) * 0.017) * 43758.5453);
        int faceMode = int(fragColor.x + 0.2);
        if (faceMode == 0) {
            // Robust fallback if instance channel wasn't populated: ~65% of slabs active
            if (slabSeed < 0.65) {
                float faceRoll = fract(slabSeed * 19.3);
                faceMode = (faceRoll < 0.38) ? 1 : ((faceRoll < 0.72) ? 2 : 3);
            }
        }

        bool activeFace = (faceMode == 1 && isCorridorFace) ||
                          (faceMode == 2 && isFrontFace) ||
                          (faceMode == 3 && (isCorridorFace || isFrontFace));

        if (activeFace) {
            float height01 = clamp(fragLocalPos.y + 0.5, 0.0, 1.0);
            float taper = 0.92 + 0.08 * cos(height01 * 3.14159265);
            vec2 faceUV;
            float faceWidth = fragColor.w > 0.5 ? fragColor.w : 4.0;
            float faceHeight = fragColor.z > 0.5 ? fragColor.z : 2.5;

            if (isCorridorFace) {
                float localZ = fragLocalPos.z / max(taper, 0.1);
                float u = (fragPosition.x < 0.0) ? (localZ + 0.5) : (0.5 - localZ);
                faceUV = vec2(clamp(u, 0.0, 1.0), height01);
            } else {
                float localX = fragLocalPos.x / max(taper, 0.1);
                float u = localX + 0.5;
                faceUV = vec2(clamp(u, 0.0, 1.0), height01);
            }

            vec3 slabNeon = mix(neonColor, vec3(0.15, 1.0, 0.55), 0.75);
            vec3 rain = renderMatrixRain(faceUV, faceWidth, faceHeight, uTime, seedPhase, fragColor.y + slabSeed, slabNeon);
            finalRGB = baseBody + neonLines * 0.35 + rain + spec * 0.20;
        } else {
            finalRGB = baseBody + neonLines + mix(uPrimaryColor, vec3(0.5, 0.8, 1.0), 0.6) * spec * 0.6 +
                       neonColor * (rim * 0.4);
        }
    } else if (uStructureKind == 1) {
        // =====================================================================
        // CACHE TOWER (Vertical Binary Code Waterfall down corridor/front faces)
        // =====================================================================
        float cylRadius = length(fragLocalPos.xz);
        float edgeOuter = smoothstep(0.32, 0.485, cylRadius) * 0.30;
        float edgeCore  = smoothstep(0.44, 0.490, cylRadius) * 0.48;
        float prismEdge = clamp(edgeOuter + edgeCore, 0.0, 1.0);

        vec3 baseBody = mix(vec3(0.002, 0.005, 0.010), neonColor, 0.05);
        vec3 halfDir = normalize(lightDir + viewDir);
        float spec = pow(max(dot(N, halfDir), 0.0), 32.0) * 0.8;

        float edgePulse = sin(fragPosition.y * 2.0 - uTime * 5.0 + seedPhase) * 0.5 + 0.5;
        vec3 neonLines = neonColor * (prismEdge * 1.5 + pow(prismEdge, 2.0) * 2.0) * (0.8 + edgePulse * 0.4);

        // Corridor-facing side and oncoming front face
        bool isCorridorFace = (fragPosition.x < 0.0 ? N.x > 0.35 : N.x < -0.35);
        bool isFrontFace = (N.z > 0.35);

        // Deterministic per-tower activation & face selection
        float towerSeed = fract(sin(floor(fragPosition.x * 0.25) * 113.5 +
                                   floor((fragPosition.z + virtualPlayerZ) * 0.0625) * 271.9 +
                                   float(uRunSeed) * 0.031) * 43758.5453);
        int faceMode = 0;
        if (towerSeed < 0.70) {
            float roll = fract(towerSeed * 23.7);
            faceMode = (roll < 0.40) ? 1 : ((roll < 0.75) ? 2 : 3);
        }

        bool activeFace = (faceMode == 1 && isCorridorFace) ||
                          (faceMode == 2 && isFrontFace) ||
                          (faceMode == 3 && (isCorridorFace || isFrontFace));

        if (activeFace) {
            float height01 = clamp(fragLocalPos.y + 0.5, 0.0, 1.0);
            float u = isCorridorFace ? ((fragPosition.x < 0.0 ? fragLocalPos.z : -fragLocalPos.z) + 0.5)
                                     : (fragLocalPos.x + 0.5);
            vec2 towerUV = vec2(clamp(u, 0.0, 1.0), height01);
            vec3 binColor = mix(uPrimaryColor, vec3(0.18, 0.88, 1.0), 0.70);
            vec3 binaryFlow = renderBinaryFlow(towerUV, 3.2, 18.0, uTime, seedPhase, towerSeed * 6.28, binColor);
            finalRGB = baseBody + neonLines * 0.35 + binaryFlow + spec * 0.20;
        } else {
            finalRGB = baseBody + neonLines + mix(uPrimaryColor, vec3(0.5, 0.8, 1.0), 0.6) * spec * 0.6 +
                       neonColor * (rim * 0.4);
        }
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
        // Floating holographic data rings instead of glazing
        vec2 uv = fragLocalPos.zy;
        float r = length(uv);
        float ring = smoothstep(0.4, 0.42, r) * (1.0 - smoothstep(0.42, 0.44, r));
        float ring2 = smoothstep(0.2, 0.22, r) * (1.0 - smoothstep(0.22, 0.24, r));
        float spin = atan(uv.y, uv.x) + uTime * 2.0;
        float dash = step(0.5, fract(spin * 4.0 / 6.28318));
        
        vec3 holoColor = mix(uPrimaryColor, uSecondaryColor, 0.5);
        finalRGB = holoColor * (ring * dash + ring2 * (1.0 - dash)) * (0.8 + uIntensity * 1.5) + neonColor * rim * 0.2;
    } else if (uStructureKind == 9) {
        // Vertical memory banks with steady luminous slot racks
        vec2 uv = fragLocalPos.zy + 0.5;
        float bankX = step(0.25, fract(uv.x * 5.0));
        float bankY = step(0.20, fract(uv.y * 8.0));
        float slot = bankX * bankY * step(0.08, uv.y) * step(uv.y, 0.92);
        vec3 base = mix(vec3(0.002, 0.004, 0.01), neonColor, 0.05);
        finalRGB = base + mix(uPrimaryColor, neonColor, 0.5) * slot * 0.35;
    } else if (uStructureKind == 10) {
        // Glowing data port instead of loading door
        vec2 uv = fragLocalPos.zy + 0.5;
        float port = smoothstep(0.2, 0.22, uv.x) * (1.0 - smoothstep(0.78, 0.8, uv.x));
        port *= smoothstep(0.1, 0.12, uv.y) * (1.0 - smoothstep(0.6, 0.62, uv.y));
        
        float scanline = step(0.9, fract(uv.y * 10.0 - uTime * 2.0));
        vec3 portGlow = uPrimaryColor * port * (0.5 + scanline * 0.5) * (1.0 + uIntensity);
        
        vec3 base = mix(vec3(0.002, 0.004, 0.01), neonColor, 0.05);
        finalRGB = base + portGlow + neonColor * rim * 0.2;
    } else if (uAccentColor.a > 1.5) {
        // =====================================================================
        // CONDUIT DATA STREAM PATH (Smooth, non-flickering luminescent neon)
        // =====================================================================
        // Steady luminous core and glowing tube base
        vec3 tubeBase = neonColor * (0.50 + 0.25 * diff);
        vec3 coreGlow = mix(neonColor, vec3(0.85, 0.95, 1.0), 0.50) * 0.35;

        // Direction vector (scaled by segment length) and phase offset passed per-instance via fragColor
        vec3 flowScaled = fragColor.xyz;
        float phaseOffset = fragColor.w;

        // Position along the conduit flow axis in local meters
        float localMeters = dot(fragLocalPos, flowScaled);

        // Long-wavelength, calm traveling data pulse directed along local conduit orientation
        float streamCoord = localMeters * 0.08 - uTime * (0.65 + uIntensity * 0.35) + phaseOffset;
        float pulseWave1 = sin(streamCoord * 6.28318) * 0.5 + 0.5;
        float pulseWave2 = sin(streamCoord * 12.56636 + 1.2) * 0.5 + 0.5;
        float smoothPulse = pow(pulseWave1, 2.5) * 0.65 + pow(pulseWave2, 3.5) * 0.35;

        // Pulse energy shift: neon shifts toward bright cyan/white core at peak
        vec3 pulseEnergy = mix(neonColor, vec3(0.90, 0.98, 1.0), 0.75) * smoothPulse * 0.70;

        // Reuse streamCoord as the scroll axis for hash-based glyphs; cross-coordinate aligns
        // with the conduit cross-section across X, Y, or Z spans
        float crossCoord = abs(flowScaled.x) > 0.5 ? fragLocalPos.z : fragLocalPos.x;
        float glyphCol = floor((crossCoord + 0.5) * 5.0);
        float glyphRow = floor(streamCoord * 18.0);
        float glyphCode = sin(dot(vec2(glyphCol, glyphRow), vec2(12.9898, 78.233)));
        int charIdx = int(abs(glyphCode * 16.0)) % 16;
        vec2 glyphWithin = vec2(fract((crossCoord + 0.5) * 5.0), fract(streamCoord * 18.0));
        float glyph = evaluateGlyph(charIdx, glyphWithin);
        vec3 glyphColor = mix(neonColor, vec3(0.75, 0.95, 1.0), 0.6) * glyph * smoothPulse * 0.35;

        float boostCarrier = pow(max(sin(streamCoord * 31.4159 + seedPhase), 0.0), 9.0);
        finalRGB = tubeBase + coreGlow + pulseEnergy + glyphColor + neonColor * (rim * 0.40) +
                   uSecondaryColor * boostCarrier * uIntensity * 0.25;
    } else {
        // =====================================================================
        // DIGITAL GLASS MONOLITH (Translucent dark glass + intense neon grid)
        // =====================================================================
        vec3 absPos = abs(fragLocalPos);
        float max1 = max(max(absPos.x, absPos.y), absPos.z);
        float min1 = min(min(absPos.x, absPos.y), absPos.z);
        float max2 = absPos.x + absPos.y + absPos.z - max1 - min1;

        // Wireframe edges
        float edgeOuter = smoothstep(0.32, 0.485, max2) * 0.30;
        float edgeCore  = smoothstep(0.44, 0.490, max2) * 0.48;
        float boxEdge   = clamp(edgeOuter + edgeCore, 0.0, 1.0);

        float cylRadius = length(fragLocalPos.xz);
        float cylOuter = smoothstep(0.32, 0.485, cylRadius) * smoothstep(0.35, 0.49, absPos.y) * 0.30;
        float cylCore  = smoothstep(0.44, 0.490, cylRadius) * smoothstep(0.44, 0.49, absPos.y) * 0.48;
        float cylEdge  = clamp(cylOuter + cylCore, 0.0, 1.0);

        float isCylinder = step(0.46, cylRadius) * (1.0 - step(0.47, max2));
        float edgeFactor = mix(boxEdge, cylEdge, isCylinder);

        // Dark reflective glass body
        vec3 baseBody = mix(vec3(0.002, 0.005, 0.010), neonColor, 0.05);

        // Intense specular highlight for glass
        vec3 halfDir = normalize(lightDir + viewDir);
        float spec = pow(max(dot(N, halfDir), 0.0), 32.0) * 0.8;

        float structuralDetail = uStructureKind == 8 ? 1.0 : 0.0;
        
        // Intense pulsing neon edges
        float edgePulse = sin(fragPosition.y * 2.0 - uTime * 5.0 + seedPhase) * 0.5 + 0.5;
        vec3 neonLines = neonColor * (edgeFactor * 1.5 + pow(edgeFactor, 2.0) * 2.0) * (0.8 + edgePulse * 0.4) * mix(1.0, 0.3, structuralDetail);

        finalRGB = baseBody + neonLines + mix(uPrimaryColor, vec3(0.5, 0.8, 1.0), 0.6) * spec * mix(0.8, 0.2, structuralDetail) +
                   neonColor * (rim * mix(0.6, 0.2, structuralDetail));
    }

    // Atmospheric deep-indigo horizon fog
    vec3 fogColor = mix(vec3(0.008, 0.014, 0.038), uSecondaryColor, 0.025 + uIntensity * 0.018);
    float fogFactor = clamp(fragDist, 0.0, 1.0);
    finalRGB = mix(fogColor, finalRGB, fogFactor);

    finalColor = vec4(finalRGB, 1.0);
}
