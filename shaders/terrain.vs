#version 330

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;
in mat4 instanceTransform;

uniform mat4 matProjection;
uniform mat4 matView;

out vec3 fragPosition;
out vec3 fragNormal;
out vec3 fragLocalPos;
out float fragFade;

const float CURVATURE_COEFF = 0.00035;

void main()
{
    vec4 worldPos = instanceTransform * vec4(vertexPosition, 1.0);
    mat3 normalMatrix = transpose(inverse(mat3(instanceTransform)));

    float dist = max(-worldPos.z, 0.0);
    worldPos.y -= CURVATURE_COEFF * dist * dist;

    fragPosition = worldPos.xyz;
    fragNormal = normalize(normalMatrix * vertexNormal);
    fragLocalPos = vertexPosition;
    fragFade = smoothstep(-640.0, -448.0, worldPos.z) *
               (1.0 - smoothstep(12.0, 25.0, worldPos.z));

    gl_Position = matProjection * matView * worldPos;
}
