#version 330

in vec3 fragPosition;
in vec3 fragNormal;
in vec3 fragLocalPos;
in float fragFade;

out vec4 finalColor;

uniform float uTime;
uniform float uIntensity;
uniform int uRunSeed;
uniform vec3 uPrimaryColor;
uniform vec3 uSecondaryColor;
uniform vec3 uBodyColor;
uniform vec3 uSkyHorizon;

void main()
{
    vec3 normal = normalize(fragNormal);
    float upward = max(normal.y, 0.0);
    float cliff = 1.0 - upward;

    // Dark matte obsidian/basalt base (rich dark charcoal / black)
    vec3 baseColor = uBodyColor * 0.45;
    
    // Directional shading for cliff sides and floor depth
    vec3 lightDir = normalize(vec3(0.35, 0.85, 0.45));
    float diff = max(dot(normal, lightDir), 0.0);
    baseColor *= (0.65 + 0.35 * diff);

    // World-space grid lines (X and Z) - Top surfaces only
    // 4-meter grid period with screen-space derivative anti-aliasing (fwidth)
    float coordX = abs(fract(fragPosition.x * 0.25) - 0.5) * 4.0;
    float coordZ = abs(fract(fragPosition.z * 0.25) - 0.5) * 4.0;
    float spanX = max(fwidth(coordX) * 1.5, 0.016);
    float spanZ = max(fwidth(coordZ) * 1.5, 0.016);
    float gridX = 1.0 - smoothstep(0.0, spanX, coordX);
    float gridZ = 1.0 - smoothstep(0.0, spanZ, coordZ);
    float grid = max(gridX, gridZ) * upward;

    // Moving data pulses along the grid lines
    float pulse = sin(fragPosition.z * 0.4 - uTime * 3.5) * 0.5 + 0.5;
    pulse *= sin(fragPosition.x * 0.4 + uTime * 1.8) * 0.5 + 0.5;

    // Instanced block boundary wireframes (Top surface tile rims)
    float distToTileRim = 0.5 - max(abs(fragLocalPos.x), abs(fragLocalPos.z));
    float rimSpan = max(fwidth(distToTileRim) * 1.5, 0.012);
    float tileBorder = (1.0 - smoothstep(0.002, 0.002 + rimSpan, distToTileRim)) * upward;

    // Top rim of vertical cliff edges
    float distToCliffTop = 0.5 - fragLocalPos.y;
    float cliffSpan = max(fwidth(distToCliffTop) * 1.5, 0.012);
    float cliffTopRim = (1.0 - smoothstep(0.002, 0.002 + cliffSpan, distToCliffTop)) * cliff;

    // Digital canyon strata and vertical grid continuation along cliff drops
    float cliffTrace = (abs(normal.x) > 0.5 ? gridZ : gridX) * cliff;
    float strataCoord = abs(fract(fragPosition.y * 0.5) - 0.5) * 2.0;
    float strataSpan = max(fwidth(strataCoord) * 1.5, 0.016);
    float cliffStrata = (1.0 - smoothstep(0.0, strataSpan, strataCoord)) * cliff;
    float cliffWire = clamp(cliffTrace * 0.50 + cliffStrata * 0.50, 0.0, 1.0);

    // Wireframe glow: smooth, sub-pixel blended vector lines
    float wireMask = clamp(grid * 0.70 + tileBorder * 0.80 + cliffTopRim * 0.50 + cliffWire * 0.45, 0.0, 1.0);
    float smoothWire = pow(wireMask, 1.4);

    float cliffPulse = sin(fragPosition.y * 0.8 - uTime * 2.5) * 0.5 + 0.5;
    vec3 cliffColor = mix(uPrimaryColor, uSecondaryColor, cliffPulse);
    vec3 gridColor = mix(mix(uSecondaryColor, uPrimaryColor, pulse), cliffColor, cliff * 0.55);
    vec3 glow = gridColor * (wireMask * 0.45 + smoothWire * 0.55) * (0.60 + uIntensity * 0.50);

    vec3 color = baseColor + glow;

    // Atmospheric horizon fog blending to deep dark horizon
    vec3 fog = mix(uBodyColor * 0.6, uSkyHorizon, 0.22 + uIntensity * 0.18);
    color = mix(fog, color, fragFade);

    finalColor = vec4(color, 1.0);
}
