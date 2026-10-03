#version 330
// 9-tap gaussian along uStep (the reflection uses two vertical passes).
in vec2 fUV;
out vec4 finalColor;
uniform sampler2D texture0;
uniform vec2 uStep;
void main()
{
    const float w[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
    vec3 c = texture(texture0, fUV).rgb * w[0];
    for (int i = 1; i < 5; i++) {
        c += texture(texture0, fUV + uStep * float(i)).rgb * w[i];
        c += texture(texture0, fUV - uStep * float(i)).rgb * w[i];
    }
    finalColor = vec4(c, 1.0);
}
