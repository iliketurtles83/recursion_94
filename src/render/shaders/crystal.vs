#version 330
// Crystal field edges: camera-facing segments of polyhedra that tumble slowly around their
// centres (slot >= 0: vertices are offsets from uShapeA[slot].xyz) or stand still (slot -1).
in vec3 vertexPosition;     // this endpoint
in vec2 vertexTexCoord;     // u (0 at A, 1 at B), side
in vec2 vertexTexCoord2;    // shape slot (-1 = static), unused
in vec3 vertexNormal;       // other endpoint
in vec4 vertexTangent;      // halfWidth, intensity, hash, endFlag
in vec4 vertexColor;
uniform mat4 matModel;
uniform mat4 matView;
uniform mat4 matProjection;
uniform vec4 uShapeA[48];   // centre (chunk-local), phase
uniform vec4 uShapeB[48];   // axis, angular speed (rad/s)
#include "common.glsl"
out vec2 fUV;
out vec3 fColor;
out float fIntensity;
out float fDist;
out float fHash;
void main()
{
    vec3 a = vertexPosition, b = vertexNormal;
    if (vertexTexCoord2.x > -0.5) {
        int g = int(vertexTexCoord2.x + 0.5);
        float ang = uShapeA[g].w + uShapeB[g].w * uTime;
        a = uShapeA[g].xyz + rotate_axis(a, uShapeB[g].xyz, ang);
        b = uShapeA[g].xyz + rotate_axis(b, uShapeB[g].xyz, ang);
    }
    vec3 wa = (matModel * vec4(a, 1.0)).xyz;
    vec3 wb = (matModel * vec4(b, 1.0)).xyz;
    float endSign = vertexTangent.w > 0.5 ? -1.0 : 1.0;
    vec3 seg = (wb - wa) * endSign;              // start -> end
    float len = length(seg);
    vec3 dir = len > 1e-6 ? seg / len : vec3(0.0, 0.0, 1.0);
    vec3 toCam = normalize(uCamPos - wa);
    vec3 side = cross(dir, toCam);
    float sl = length(side);
    side = sl > 1e-5 ? side / sl : normalize(cross(dir, vec3(0.0, 1.0, 0.0)) + 1e-4);
    float dist = length(uCamPos - wa);
    float hwEff;
    float k = min_width(vertexTangent.x, dist, hwEff);
    vec3 p = wa + side * hwEff * vertexTexCoord.y - dir * hwEff * endSign * 0.7;
    fUV = vertexTexCoord;
    fColor = srgb_to_linear(vertexColor.rgb);
    fIntensity = vertexTangent.y * k;
    fDist = dist;
    fHash = vertexTangent.z;
    gl_Position = matProjection * matView * vec4(p, 1.0);
}
