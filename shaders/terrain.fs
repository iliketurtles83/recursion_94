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

void main()
{
    vec3 normal = normalize(fragNormal);
    float upward = max(normal.y, 0.0);

    // Dark glass base
    vec3 baseColor = mix(vec3(0.002, 0.005, 0.010), uSecondaryColor * 0.05, 0.5);
    
    // Grid lines in world space (X and Z)
    float gridX = 1.0 - smoothstep(0.0, 0.05, abs(fract(fragPosition.x * 0.25) - 0.5));
    float gridZ = 1.0 - smoothstep(0.0, 0.05, abs(fract(fragPosition.z * 0.25) - 0.5));
    float grid = max(gridX, gridZ) * upward;
    
    // Moving data pulses along the grid
    float pulse = sin(fragPosition.z * 0.5 - uTime * 4.0) * 0.5 + 0.5;
    pulse *= sin(fragPosition.x * 0.5 + uTime * 2.0) * 0.5 + 0.5;
    
    // Edge highlights (from instanced cubes)
    float edge = 1.0 - smoothstep(0.45, 0.49, max(abs(fragLocalPos.x), abs(fragLocalPos.z)));
    
    vec3 gridColor = mix(uSecondaryColor, uPrimaryColor, pulse);
    vec3 glow = gridColor * (grid * 0.4 + edge * 0.3) * (0.5 + uIntensity * 1.5);
    
    vec3 color = baseColor + glow;
    
    vec3 fog = mix(vec3(0.002, 0.004, 0.012), uSecondaryColor * 0.035, uIntensity);
    color = mix(fog, color, fragFade);

    finalColor = vec4(color, 1.0);
}
