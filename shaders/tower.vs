#version 330

// Mesh attributes
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;

// Instancing matrix attribute uploaded by Raylib DrawMeshInstanced
in mat4 instanceTransform;

// Raylib automatic camera uniforms
uniform mat4 matProjection;
uniform mat4 matView;
uniform float uTime;
uniform float uIntensity;
uniform float uBeatPulse;
uniform int uRunSeed;
uniform int uStructureKind;

// Outputs to fragment shader
out vec3 fragPosition;
out vec3 fragNormal;
out vec3 fragLocalPos;
out vec4 fragColor;
out float fragDist;

const float CURVATURE_COEFF = 0.00035;

void main()
{
    vec3 localPos = vertexPosition;

    // Distinct silhouettes emerge from the same compact chamfered mesh. This
    // keeps instancing cheap while breaking up the repeated-box profile.
    float height01 = localPos.y + 0.5;
    if (uStructureKind == 3 || uStructureKind >= 5) {
        // Conduits retain a constant section so spans join cleanly.
    } else if (uStructureKind == 1) {
        localPos.x *= 1.0 - 0.08 * height01;
        localPos.z += localPos.x * 0.055;
    } else if (uStructureKind == 2) {
        localPos.xz *= 0.92 + 0.08 * cos(height01 * 3.14159265);
    } else if (uStructureKind == 4) {
        float twist = (height01 - 0.5) * 0.22;
        mat2 rotation = mat2(cos(twist), -sin(twist), sin(twist), cos(twist));
        localPos.xz = rotation * localPos.xz;
        localPos.xz *= mix(1.08, 0.80, height01);
    } else {
        localPos.xz *= mix(1.035, 0.92, height01);
    }
    fragLocalPos = localPos;

    // Transform normal to world space
    mat3 normalMatrix = transpose(inverse(mat3(instanceTransform)));
    fragNormal = normalize(normalMatrix * vertexNormal);

    // Apply instance transform matrix to vertex position (affine: 3x3 scale/rot + translation)
    vec4 worldPos = vec4(mat3(instanceTransform) * localPos + instanceTransform[3].xyz, 1.0);

    // Audio-reactive structure breathing: subtle Y-axis scale on kick drum.
    // Structures pulse outward from their base, stronger near the player.
    float dist = max(-worldPos.z, 0.0);
    float proximityFade = 1.0 - smoothstep(0.0, 120.0, dist);
    float breathe = uBeatPulse * 0.04 * proximityFade;
    worldPos.y *= 1.0 + breathe;
    worldPos.xz *= 1.0 + breathe * 0.3;

    // Architecture is deliberately rigid. Temporal vertex displacement made
    // separately instanced roof pieces shear against their parent buildings
    // and created shimmer at long range. Motion remains in material effects,
    // atmosphere and the stationary-treadmill scroll instead.

    // Apply identical horizon curvature so instanced structures and floor bend together
    float dist = max(-worldPos.z, 0.0);
    worldPos.y -= CURVATURE_COEFF * dist * dist;

    // CPU-streamed world coordinates. Sparse silhouettes begin before the
    // full-detail environment so the horizon remains populated without adding
    // near-field clutter.
    float fadeIn = smoothstep(-640.0, -448.0, worldPos.z);
    float fadeOut = 1.0 - smoothstep(12.0, 25.0, worldPos.z);
    float horizonFactor = fadeIn * fadeOut;

    fragPosition = worldPos.xyz;
    // Repurpose instance transform 4th row slack as per-instance channel
    fragColor = vec4(instanceTransform[0].w, instanceTransform[1].w,
                      instanceTransform[2].w, instanceTransform[3].w);
    fragDist = horizonFactor;

    // Project world space position to clip space
    gl_Position = matProjection * matView * worldPos;
}
