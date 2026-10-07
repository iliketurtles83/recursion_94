#version 330
in vec3 vertexPosition;
in vec3 vertexNormal;
in vec2 vertexTexCoord;
uniform mat4 mvp, matModel;
out vec3 fPos, fNormal;
out vec2 fUV;
void main(){fPos=(matModel*vec4(vertexPosition,1)).xyz;fNormal=mat3(matModel)*vertexNormal;fUV=vertexTexCoord;gl_Position=mvp*vec4(vertexPosition,1);}
