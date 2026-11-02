#version 330
// Opaque city tower bodies (env/city_towers.c). Vertices are chunk-local.
in vec3 vertexPosition;
in vec2 vertexTexCoord;     // face (u, v) in world units: u along the face from the tower corner, v above the floor
in vec2 vertexTexCoord2;    // tower hash 0..1, tower height
in vec3 vertexNormal;
in vec4 vertexTangent;      // face id (0..3 sides, 4 roof), tier, -, -
in vec4 vertexColor;        // body tint
uniform mat4 mvp;
uniform mat4 matModel;
out vec3 fPos;
out vec2 fUV;
out vec3 fNormal;
out vec2 fInfo;
out float fFace;
out vec3 fTint;
void main()
{
    fPos = (matModel * vec4(vertexPosition, 1.0)).xyz;
    fUV = vertexTexCoord;
    fNormal = vertexNormal;
    fInfo = vertexTexCoord2;
    fFace = vertexTangent.x;
    fTint = vertexColor.rgb;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
