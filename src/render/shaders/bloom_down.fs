#version 330
// Dual-Kawase downsample.
in vec2 fUV;
out vec4 finalColor;
uniform sampler2D texture0;
uniform vec2 uTexel;        // source texel size
void main()
{
    vec2 o = uTexel;
    vec3 sum = texture(texture0, fUV).rgb * 4.0;
    sum += texture(texture0, fUV + vec2(-o.x, -o.y)).rgb;
    sum += texture(texture0, fUV + vec2( o.x, -o.y)).rgb;
    sum += texture(texture0, fUV + vec2(-o.x,  o.y)).rgb;
    sum += texture(texture0, fUV + vec2( o.x,  o.y)).rgb;
    finalColor = vec4(sum / 8.0, 1.0);
}
