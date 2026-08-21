#version 330

// Attributes from Raylib mesh
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;

// Uniforms automatically passed by Raylib
uniform mat4 mvp;

// Z offset of the plane's model-space origin in world space (keeps pattern/fog aligned with DrawModel placement)
uniform float modelOffsetZ;
uniform float virtualPlayerZ;
uniform float uTime;
uniform float uIntensity;
uniform int uRunSeed;

// Outputs to Fragment Shader
out vec3 fragWorldPos;
out vec2 fragTexCoord;
out vec3 fragNormal;

const float CURVATURE_COEFF = 0.00035;

void main()
{
    vec3 worldPos = vertexPosition + vec3(0.0, 0.0, modelOffsetZ);

    // Multi-octave rolling terrain relief;
    // height is analytic so lighting normal is smooth across the continuous plane.
    float waveZ = worldPos.z - virtualPlayerZ;

    // Octave 1: Primary cross-ripple
    float u1 = worldPos.x * 0.05 + waveZ * 0.06;
    float c1 = worldPos.x * 0.02;
    float amp1 = 1.6;
    float h1 = amp1 * sin(u1) * cos(c1);
    float dhdx1 = amp1 * (0.05 * cos(u1) * cos(c1) - 0.02 * sin(u1) * sin(c1));
    float dhdz1 = amp1 * (0.06 * cos(u1) * cos(c1));

    // Octave 2: Slower secondary wave (undulating terrain swell)
    float u2 = worldPos.x * 0.015 - waveZ * 0.02;
    float amp2 = 1.4;
    float h2 = amp2 * sin(u2);
    float dhdx2 = amp2 * (0.015 * cos(u2));
    float dhdz2 = amp2 * (-0.02 * cos(u2));

    // Boost does not alter world/music time. It opens an additional seeded
    // interference layer so acceleration is communicated as energy instead.
    float seedPhase = float(uint(uRunSeed) & 1023u) * 0.006135923;
    float rippleA = worldPos.x * 0.16 + waveZ * 0.11 - uTime * 4.5 + seedPhase;
    float rippleB = waveZ * 0.035 + uTime * 1.8;
    float rippleAmp = uIntensity * 0.42;
    float boostWave = rippleAmp * sin(rippleA) * cos(rippleB);
    float dhdxBoost = rippleAmp * 0.16 * cos(rippleA) * cos(rippleB);
    float dhdzBoost = rippleAmp * (0.11 * cos(rippleA) * cos(rippleB) -
                                    0.035 * sin(rippleA) * sin(rippleB));

    float h = h1 + h2 + boostWave;
    float dhdx = dhdx1 + dhdx2 + dhdxBoost;
    float dhdz = dhdz1 + dhdz2 + dhdzBoost;

    worldPos.y += h;

    // Horizon curvature (downward bend of distant terrain)
    float dist = max(-worldPos.z, 0.0);
    float curveDrop = CURVATURE_COEFF * dist * dist;
    worldPos.y -= curveDrop;

    vec3 displacedLocal = vertexPosition + vec3(0.0, h - curveDrop, 0.0);

    fragWorldPos = worldPos;
    fragTexCoord = vertexTexCoord;
    fragNormal = normalize(vec3(-dhdx, 1.0, -dhdz));

    gl_Position = mvp * vec4(displacedLocal, 1.0);
}
