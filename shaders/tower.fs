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
uniform vec3 uHotColor;
uniform vec3 uSkyHorizon;
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

vec3 renderMatrixRain(vec2 uv, float widthMeters, float heightMeters, float time, float seedPhase, float phaseOffset, vec3 textColor, vec3 headColor) {
    // Terminal padding: generous margins framing the digital data stream
    float padX = 0.12;
    float padY = 0.08;
    vec2 innerUV = vec2((uv.x - padX) / (1.0 - 2.0 * padX),
                        (uv.y - padY) / (1.0 - 2.0 * padY));
    float inBounds = step(0.0, innerUV.x) * step(innerUV.x, 1.0) *
                     step(0.0, innerUV.y) * step(innerUV.y, 1.0);

    // Sleek, razor-thin glowing frame line around the terminal screen
    float borderDist = min(min(innerUV.x, 1.0 - innerUV.x), min(innerUV.y, 1.0 - innerUV.y));
    float fwBorder = fwidth(borderDist);
    float frameLine = (1.0 - smoothstep(0.002, 0.002 + max(fwBorder * 1.5, 0.006), borderDist)) * inBounds;
    float contentMask = smoothstep(0.006, 0.018, borderDist) * inBounds;

    // Distinct stream columns: 7 to 12 streams with clear dark spacing
    float cols = clamp(floor(max(widthMeters, 1.5) * 2.2), 7.0, 12.0);
    float colId = floor(innerUV.x * cols);
    float colU_raw = fract(innerUV.x * cols);

    // 22% padding on each side of column -> 44% clear dark space between streams
    float charMarginX = 0.22;
    float colU = (colU_raw - charMarginX) / (1.0 - 2.0 * charMarginX);
    bool inCharX = (colU >= 0.0 && colU <= 1.0);

    // Sleek rows of characters across the slab face
    float rows = clamp(floor(max(heightMeters, 1.2) * 2.5), 8.0, 16.0);
    float colHash = fract(sin(colId * 91.71 + seedPhase * 13.0 + phaseOffset * 7.0) * 43758.5453);
    float colSpeed = 1.3 + colHash * 1.6;

    // Fast, continuous cascade of code
    float scrollY = (1.0 - innerUV.y) * rows + time * colSpeed + phaseOffset * 6.0 + colHash * 9.0;
    float rowId = floor(scrollY);
    float rowV_raw = fract(scrollY);

    float charMarginY = 0.14;
    float rowV = (rowV_raw - charMarginY) / (1.0 - 2.0 * charMarginY);
    bool inCharY = (rowV >= 0.0 && rowV <= 1.0);

    // Stream drop mechanics: discrete packet cycles
    float streamLen = 5.0 + colHash * 4.0;
    float dropPos = fract(scrollY / streamLen); // 0.0 at tail end, 1.0 at head
    float head = smoothstep(0.80, 1.0, dropPos);
    float tail = pow(dropPos, 1.5);
    float streamIntensity = tail * 2.0 + head * 4.0;

    float glyph = 0.0;
    if (inCharX && inCharY) {
        float cellSeed = fract(sin(colId * 12.9898 + rowId * 78.233 + seedPhase * 5.0) * 43758.5453);
        float mutate = floor(time * 6.0 + cellSeed * 7.0);
        int charIdx = int(abs(sin(cellSeed * 43.0 + mutate * 0.23) * 16.0)) % 16;
        glyph = evaluateGlyph(charIdx, vec2(colU, rowV));
    }

    // High-contrast glowing glyphs with contrasting incandescent head
    vec3 glyphColor = mix(textColor, headColor, head * 0.95);
    float charLum = glyph * (streamIntensity + 0.40);
    vec3 charLit = glyphColor * charLum;
    vec3 screenBackdrop = uBodyColor * 0.14; // Pitch-dark charcoal terminal glass
    vec3 frameColor = textColor * (frameLine * 1.5);

    return (screenBackdrop + charLit) * contentMask + frameColor;
}

vec3 renderBinaryFlow(vec2 uv, float widthMeters, float heightMeters, float time, float seedPhase, float phaseOffset, vec3 dataColor, vec3 headColor) {
    // Terminal padding: generous 14% margins framing the vertical binary data streams
    float padX = 0.14;
    float padY = 0.05;
    vec2 innerUV = vec2((uv.x - padX) / (1.0 - 2.0 * padX),
                        (uv.y - padY) / (1.0 - 2.0 * padY));
    float inBounds = step(0.0, innerUV.x) * step(innerUV.x, 1.0) *
                     step(0.0, innerUV.y) * step(innerUV.y, 1.0);

    // Sleek, razor-thin glowing frame line around the vertical terminal screen
    float borderDist = min(min(innerUV.x, 1.0 - innerUV.x), min(innerUV.y, 1.0 - innerUV.y));
    float fwBorder = fwidth(borderDist);
    float frameLine = (1.0 - smoothstep(0.002, 0.002 + max(fwBorder * 1.5, 0.006), borderDist)) * inBounds;
    float contentMask = smoothstep(0.006, 0.016, borderDist) * inBounds;

    // 6 sleek, discrete vertical binary streams across the tower face
    float cols = 6.0;
    float colId = floor(innerUV.x * cols);
    float colU_raw = fract(innerUV.x * cols);

    // 24% padding on each side of column -> 48% clear dark space between streams!
    float charMarginX = 0.24;
    float colU = (colU_raw - charMarginX) / (1.0 - 2.0 * charMarginX);
    bool inCharX = (colU >= 0.0 && colU <= 1.0);

    // Faint optical guide track running down each stream channel
    float trackGuide = (1.0 - smoothstep(0.0, 0.045, abs(colU_raw - 0.5))) * 0.15;

    // 28 to 36 rows of characters across tower height: sleek typography, not giant digits
    float rows = clamp(floor(max(heightMeters, 2.0) * 1.8), 26.0, 36.0);
    float colHash = fract(sin(colId * 83.17 + seedPhase * 11.0 + phaseOffset * 5.0) * 43758.5453);
    float colSpeed = 1.4 + colHash * 1.6;

    // High-speed vertical cascade
    float scrollY = (1.0 - innerUV.y) * rows + time * colSpeed + phaseOffset * 7.0 + colHash * 13.0;
    float rowId = floor(scrollY);
    float rowV_raw = fract(scrollY);

    float charMarginY = 0.14;
    float rowV = (rowV_raw - charMarginY) / (1.0 - 2.0 * charMarginY);
    bool inCharY = (rowV >= 0.0 && rowV <= 1.0);

    // Discrete packet bursts along the stream with gaps between packets
    float packetPeriod = 16.0 + colHash * 8.0;
    float packetDropLen = 8.0 + colHash * 4.0;
    float packetCycle = mod(scrollY, packetPeriod);
    float streamHeadPos = packetCycle / packetDropLen;
    float inPacket = step(streamHeadPos, 1.0);
    float head = smoothstep(0.76, 1.0, streamHeadPos) * inPacket;
    float tail = pow(streamHeadPos, 1.4) * inPacket;
    float streamIntensity = tail * 2.2 + head * 4.2;

    // Pure binary: strictly 0 or 1!
    float glyph = 0.0;
    if (inCharX && inCharY && inPacket > 0.5) {
        float bitSeed = fract(sin(colId * 17.13 + rowId * 91.41 + seedPhase * 3.0) * 43758.5453);
        float bitFlip = floor(time * 6.0 + bitSeed * 9.0);
        int isOne = int(floor(bitSeed * 2.0 + bitFlip * 0.5)) & 1;
        glyph = evaluateGlyph(isOne, vec2(colU, rowV));
    }

    vec3 glyphColor = mix(dataColor, headColor, head * 0.95);
    float charLum = glyph * (streamIntensity + 0.40);
    vec3 charLit = glyphColor * charLum;
    vec3 trackGlow = dataColor * (trackGuide * tail * 0.40);
    vec3 frameGlow = dataColor * (frameLine * 1.5);
    vec3 backdrop = uBodyColor * 0.14; // Pitch-dark obsidian backing

    return (backdrop + charLit + trackGlow) * contentMask + frameGlow;
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
        vec3 streamBody = uPrimaryColor;
        vec3 streamHead = mix(uHotColor, vec3(1.0, 1.0, 1.0), 0.80);
        vec3 rain = renderMatrixRain(signUV, 2.6, 2.0, uTime, seedPhase, 0.0, streamBody, streamHead);
        vec3 signBase = uBodyColor * 0.40;
        finalRGB = signBase + rain;
    } else if (uStructureKind == 2) {
        // =====================================================================
        // MEMORY SLAB (Selective data-rain on corridor-facing or front faces)
        // =====================================================================
        float height01 = clamp(fragLocalPos.y + 0.5, 0.0, 1.0);
        float taper = 0.92 + 0.08 * cos(height01 * 3.14159265);
        vec3 untaperedPos = fragLocalPos;
        untaperedPos.xz /= max(taper, 0.1);
        vec3 absPos = abs(untaperedPos);

        float max1 = max(max(absPos.x, absPos.y), absPos.z);
        float min1 = min(min(absPos.x, absPos.y), absPos.z);
        float max2 = absPos.x + absPos.y + absPos.z - max1 - min1;

        // Thinner, smoother wireframe edge with sub-pixel screen-space blending
        float distToEdge = 0.5 - max2;
        float fwEdge = fwidth(distToEdge);
        float edgeSpan = max(fwEdge * 1.6, 0.012);
        float boxEdge = 1.0 - smoothstep(0.003, 0.003 + edgeSpan, distToEdge);

        // Dark matte obsidian/charcoal body
        vec3 baseBody = uBodyColor * (0.60 + 0.40 * diff);

        vec3 halfDir = normalize(lightDir + viewDir);
        float spec = pow(max(dot(N, halfDir), 0.0), 32.0) * 0.8;

        float edgePulse = sin(fragPosition.y * 2.0 - uTime * 5.0 + seedPhase) * 0.5 + 0.5;
        float smoothBox = pow(boxEdge, 1.4);
        vec3 neonLines = neonColor * (boxEdge * 0.9 + smoothBox * 1.1) * (0.85 + edgePulse * 0.35);

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
            vec2 faceUV;
            float faceWidth = fragColor.w > 0.5 ? fragColor.w : 4.0;
            float faceHeight = fragColor.z > 0.5 ? fragColor.z : 2.5;

            if (isCorridorFace) {
                float localZ = untaperedPos.z;
                float u = (fragPosition.x < 0.0) ? (localZ + 0.5) : (0.5 - localZ);
                faceUV = vec2(clamp(u, 0.0, 1.0), height01);
            } else {
                float localX = untaperedPos.x;
                float u = localX + 0.5;
                faceUV = vec2(clamp(u, 0.0, 1.0), height01);
            }

            // Contrasting data stream color: primary data with incandescent hot/white head
            vec3 streamBody = uPrimaryColor;
            vec3 streamHead = mix(uHotColor, vec3(1.0, 1.0, 1.0), 0.80);
            vec3 rain = renderMatrixRain(faceUV, faceWidth, faceHeight, uTime, seedPhase, fragColor.y + slabSeed, streamBody, streamHead);
            finalRGB = baseBody + neonLines * boxEdge + rain;
        } else {
            // Flat face remains pure dark charcoal/black; only chamfered edges catch highlights
            float edgeSpec = spec * boxEdge;
            float edgeRim = rim * boxEdge;
            finalRGB = baseBody + neonLines + neonColor * (edgeSpec * 0.8 + edgeRim * 0.4);
        }
    } else if (uStructureKind == 1) {
        // =====================================================================
        // CACHE TOWER (Vertical Binary Code Waterfall down corridor/front faces)
        // =====================================================================
        // 8-sided prism vertical seam and cap detection with screen-space anti-aliasing
        float angle = atan(fragLocalPos.z, fragLocalPos.x);
        float facetFrac = fract((angle / 6.283185307) * 8.0 + 0.5);
        float distToCorner = 0.5 - abs(facetFrac - 0.5);
        float fwCorner = fwidth(distToCorner);
        float seamSpan = max(fwCorner * 1.6, 0.016);
        float vertEdge = 1.0 - smoothstep(0.003, 0.003 + seamSpan, distToCorner);

        float distToCap = 0.5 - abs(fragLocalPos.y);
        float fwCap = fwidth(distToCap);
        float capSpan = max(fwCap * 1.6, 0.012);
        float capEdge = 1.0 - smoothstep(0.003, 0.003 + capSpan, distToCap);
        float prismEdge = max(vertEdge, capEdge);

        // Dark matte obsidian/charcoal body
        vec3 baseBody = uBodyColor * (0.60 + 0.40 * diff);
        vec3 halfDir = normalize(lightDir + viewDir);
        float spec = pow(max(dot(N, halfDir), 0.0), 32.0) * 0.8;

        float edgePulse = sin(fragPosition.y * 2.0 - uTime * 5.0 + seedPhase) * 0.5 + 0.5;
        float smoothPrism = pow(prismEdge, 1.4);
        vec3 neonLines = neonColor * (prismEdge * 0.9 + smoothPrism * 1.1) * (0.85 + edgePulse * 0.35);

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

            // Contrasting data stream color: secondary/gold stream with incandescent head
            vec3 streamBody = uSecondaryColor;
            vec3 streamHead = mix(uHotColor, vec3(1.0, 1.0, 1.0), 0.80);
            vec3 binaryFlow = renderBinaryFlow(towerUV, 3.2, 18.0, uTime, seedPhase, towerSeed * 6.28, streamBody, streamHead);
            finalRGB = baseBody + neonLines * prismEdge + binaryFlow;
        } else {
            // Flat face remains pure dark charcoal/black; only prism edges catch highlights
            float edgeSpec = spec * prismEdge;
            float edgeRim = rim * prismEdge;
            finalRGB = baseBody + neonLines + neonColor * (edgeSpec * 0.8 + edgeRim * 0.4);
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
        finalRGB = mix(uBodyColor * 0.4, plasmaColor, 0.55 + 0.35 * diff) +
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
        vec3 base = uBodyColor * 0.50;
        finalRGB = base + mix(uPrimaryColor, neonColor, 0.5) * slot * 0.35;
    } else if (uStructureKind == 10) {
        // Glowing data port instead of loading door
        vec2 uv = fragLocalPos.zy + 0.5;
        float port = smoothstep(0.2, 0.22, uv.x) * (1.0 - smoothstep(0.78, 0.8, uv.x));
        port *= smoothstep(0.1, 0.12, uv.y) * (1.0 - smoothstep(0.6, 0.62, uv.y));
        
        float scanline = step(0.9, fract(uv.y * 10.0 - uTime * 2.0));
        vec3 portGlow = uPrimaryColor * port * (0.5 + scanline * 0.5) * (1.0 + uIntensity);
        
        vec3 base = uBodyColor * 0.50;
        finalRGB = base + portGlow + neonColor * rim * 0.2;
    } else if (uAccentColor.a > 1.5) {
        // =====================================================================
        // CONDUIT DATA STREAM PATH (Smooth, non-flickering luminescent neon)
        // =====================================================================
        // Dark matte obsidian/carbon tube base with subtle directional lighting
        vec3 tubeBase = uBodyColor * (0.65 + 0.35 * diff);

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

        // Pulse energy: luminous glowing packet traveling inside the dark pipe
        vec3 pulseEnergy = mix(neonColor, vec3(0.90, 0.98, 1.0), 0.75) * smoothPulse * 0.90;

        // Reuse streamCoord as the scroll axis for hash-based glyphs; cross-coordinate aligns
        // with the conduit cross-section across X, Y, or Z spans
        float crossCoord = abs(flowScaled.x) > 0.5 ? fragLocalPos.z : fragLocalPos.x;
        float glyphCol = floor((crossCoord + 0.5) * 5.0);
        float glyphRow = floor(streamCoord * 18.0);
        float glyphCode = sin(dot(vec2(glyphCol, glyphRow), vec2(12.9898, 78.233)));
        int charIdx = int(abs(glyphCode * 16.0)) % 16;
        vec2 glyphWithin = vec2(fract((crossCoord + 0.5) * 5.0), fract(streamCoord * 18.0));
        float glyph = evaluateGlyph(charIdx, glyphWithin);
        vec3 glyphColor = mix(neonColor, vec3(0.75, 0.95, 1.0), 0.6) * glyph * smoothPulse * 0.60;

        float boostCarrier = pow(max(sin(streamCoord * 31.4159 + seedPhase), 0.0), 9.0);
        float conduitEdge = smoothstep(0.440, 0.485, max(abs(fragLocalPos.x), max(abs(fragLocalPos.y), abs(fragLocalPos.z))));
        vec3 edgeGlow = neonColor * conduitEdge * 0.40;

        finalRGB = tubeBase + pulseEnergy + glyphColor + edgeGlow +
                   uSecondaryColor * boostCarrier * uIntensity * 0.25;
    } else {
        // =====================================================================
        // DIGITAL GLASS MONOLITH & ARCHITECTURAL DETAILS (Podiums, Pilasters)
        // =====================================================================
        float height01 = clamp(fragLocalPos.y + 0.5, 0.0, 1.0);
        float slabTaper = mix(1.035, 0.92, height01);
        vec3 untapered = fragLocalPos;
        untapered.xz /= max(slabTaper, 0.1);
        vec3 absPos = abs(untapered);
        float max1 = max(max(absPos.x, absPos.y), absPos.z);
        float min1 = min(min(absPos.x, absPos.y), absPos.z);
        float max2 = absPos.x + absPos.y + absPos.z - max1 - min1;

        // Thinner, smoother wireframe edge with sub-pixel screen-space blending
        float distToEdge = 0.5 - max2;
        float fwEdge = fwidth(distToEdge);
        float edgeSpan = max(fwEdge * 1.6, 0.012);
        float boxEdge = 1.0 - smoothstep(0.003, 0.003 + edgeSpan, distToEdge);

        // Cylinder/Prism edge detection if used in detail
        float cylRadius = length(fragLocalPos.xz);
        float angle = atan(fragLocalPos.z, fragLocalPos.x);
        float facetFrac = fract((angle / 6.283185307) * 8.0 + 0.5);
        float distToCorner = 0.5 - abs(facetFrac - 0.5);
        float vertEdge = 1.0 - smoothstep(0.003, 0.003 + max(fwidth(distToCorner) * 1.6, 0.016), distToCorner);
        float capEdge = 1.0 - smoothstep(0.003, 0.003 + max(fwidth(0.5 - absPos.y) * 1.6, 0.012), 0.5 - absPos.y);
        float cylEdge = max(vertEdge, capEdge);

        float isCylinder = step(0.46, cylRadius) * (1.0 - step(0.47, max2));
        float edgeFactor = mix(boxEdge, cylEdge, isCylinder);

        // Dark matte obsidian/charcoal body
        vec3 baseBody = uBodyColor * (0.60 + 0.40 * diff);

        // Specular highlight restricted strictly to wireframe bevels
        vec3 halfDir = normalize(lightDir + viewDir);
        float spec = pow(max(dot(N, halfDir), 0.0), 32.0) * 0.8;

        float structuralDetail = (uStructureKind == 8) ? 1.0 : 0.0;
        
        // Wireframe lines
        float edgePulse = sin(fragPosition.y * 2.0 - uTime * 5.0 + seedPhase) * 0.5 + 0.5;
        float lineStrength = mix(1.1, 0.50, structuralDetail);
        float smoothFactor = pow(edgeFactor, 1.4);
        vec3 neonLines = neonColor * (edgeFactor * lineStrength * 0.8 + smoothFactor * lineStrength * 1.0) * (0.85 + edgePulse * 0.35);

        float edgeSpec = spec * edgeFactor * mix(0.8, 0.2, structuralDetail);
        float edgeRim = rim * edgeFactor * mix(0.6, 0.2, structuralDetail);
        finalRGB = baseBody + neonLines + neonColor * (edgeSpec + edgeRim);
    }

    // Atmospheric horizon fog blending to dark palette horizon
    vec3 fogColor = mix(uBodyColor * 0.6, uSkyHorizon, 0.25 + uIntensity * 0.20);
    float fogFactor = clamp(fragDist, 0.0, 1.0);
    finalRGB = mix(fogColor, finalRGB, fogFactor);

    finalColor = vec4(finalRGB, 1.0);
}
