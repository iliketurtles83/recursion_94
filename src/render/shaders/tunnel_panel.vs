#version 330
in vec3 vertexPosition;
in vec3 vertexNormal;
in vec2 vertexTexCoord;
in vec2 vertexTexCoord2;
uniform mat4 mvp, matModel;
out vec3 fPos, fNormal;
out vec2 fUV;
out float fPart;
void main(){fPos=(matModel*vec4(vertexPosition,1)).xyz;fNormal=mat3(matModel)*vertexNormal;fUV=vertexTexCoord;fPart=vertexTexCoord2.y;gl_Position=mvp*vec4(vertexPosition,1);}
