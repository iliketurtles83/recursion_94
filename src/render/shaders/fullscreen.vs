#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
out vec2 fUV;
void main()
{
    fUV = vertexTexCoord;
    gl_Position = vec4(vertexPosition.xy, 0.0, 1.0);
}
