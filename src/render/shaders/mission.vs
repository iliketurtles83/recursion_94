#version 330
in vec3 vertexPosition;
in vec3 vertexNormal;
uniform mat4 mvp;
uniform mat4 matModel;
out vec3 fPos;
out vec3 fNormal;
void main() {
    fPos=(matModel*vec4(vertexPosition,1.0)).xyz;
    fNormal=normalize(mat3(matModel)*vertexNormal);
    gl_Position=mvp*vec4(vertexPosition,1.0);
}
