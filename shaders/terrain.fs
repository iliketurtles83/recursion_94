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
    vec3 lightDir = normalize(vec3(-0.32, 0.82, 0.38));
    float diffuse = max(dot(normal, lightDir), 0.0);
    float upward = max(normal.y, 0.0);

    float seedPhase = float(uint(uRunSeed) & 1023u) * 0.006135923;
    float strata = 0.5 + 0.5 * sin(fragPosition.y * 1.7 + fragPosition.x * 0.08 + seedPhase);
    float edge = 1.0 - smoothstep(0.34, 0.48,
                                max(abs(fragLocalPos.x), abs(fragLocalPos.z)));
    float terraceLine = 1.0 - smoothstep(0.035, 0.075,
                                       abs(fract(fragPosition.y * 0.24 + seedPhase) - 0.5));

    vec3 rock = mix(vec3(0.006, 0.009, 0.014), vec3(0.025, 0.032, 0.042),
                    0.22 + diffuse * 0.58 + upward * 0.20);
    rock += mix(uSecondaryColor, uPrimaryColor, 0.35) * strata * 0.018;

    float digitalEnergy = (edge * 0.025 + terraceLine * upward * 0.045) *
                          (0.55 + uIntensity * 0.45);
    vec3 color = rock + mix(uSecondaryColor, uPrimaryColor, 0.58) * digitalEnergy;
    vec3 fog = mix(vec3(0.004, 0.007, 0.016), uSecondaryColor, 0.018);
    color = mix(fog, color, fragFade);

    finalColor = vec4(color, 1.0);
}
