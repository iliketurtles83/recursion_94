#version 330
in vec3 vertexPosition;
in vec3 vertexNormal;
in vec4 vertexColor;
uniform mat4 mvp;
uniform mat4 matModel;
uniform vec3 uCamPos;
out vec3 fNormal;
out vec3 fView;
out vec3 fColor;
void main()
{
    vec3 wp = (matModel * vec4(vertexPosition, 1.0)).xyz;
    fNormal = normalize(mat3(matModel) * vertexNormal);
    fView = normalize(uCamPos - wp);
    fColor = pow(vertexColor.rgb, vec3(2.2));
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
