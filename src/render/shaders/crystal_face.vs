#version 330
// Crystal field faces: triangles of the same tumbling polyhedra as crystal.vs.
in vec3 vertexPosition;
in vec2 vertexTexCoord;     // face uv
in vec2 vertexTexCoord2;    // shape slot (-1 = static), face id
in vec3 vertexNormal;
in vec4 vertexTangent;      // 0, intensity, hash, 0
in vec4 vertexColor;
uniform mat4 matModel;
uniform mat4 matView;
uniform mat4 matProjection;
uniform vec4 uShapeA[48];
uniform vec4 uShapeB[48];
#include "common.glsl"
out vec3 fColor;
out float fIntensity;
out float fDist;
out float fFacing;
void main()
{
    vec3 p = vertexPosition, n = vertexNormal;
    if (vertexTexCoord2.x > -0.5) {
        int g = int(vertexTexCoord2.x + 0.5);
        float ang = uShapeA[g].w + uShapeB[g].w * uTime;
        p = uShapeA[g].xyz + rotate_axis(p, uShapeB[g].xyz, ang);
        n = rotate_axis(n, uShapeB[g].xyz, ang);
    }
    vec3 wp = (matModel * vec4(p, 1.0)).xyz;
    fColor = srgb_to_linear(vertexColor.rgb);
    fIntensity = vertexTangent.y;
    fDist = length(wp - uCamPos);
    fFacing = abs(dot(normalize(mat3(matModel) * n), normalize(uCamPos - wp)));
    gl_Position = matProjection * matView * vec4(wp, 1.0);
}
