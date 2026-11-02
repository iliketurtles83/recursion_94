#version 330
// Dual-Kawase upsample of the coarser level, added onto this level's downsample.
in vec2 fUV;
out vec4 finalColor;
uniform sampler2D texture0;  // coarser level
uniform sampler2D uBase;     // this level (down chain)
uniform vec2 uTexel;         // coarser texel size
uniform float uRadius;
void main()
{
    vec2 o = uTexel * 0.5;
    vec3 sum = vec3(0.0);
    sum += texture(texture0, fUV + vec2(-2.0 * o.x, 0.0)).rgb;
    sum += texture(texture0, fUV + vec2( 2.0 * o.x, 0.0)).rgb;
    sum += texture(texture0, fUV + vec2(0.0, -2.0 * o.y)).rgb;
    sum += texture(texture0, fUV + vec2(0.0,  2.0 * o.y)).rgb;
    sum += texture(texture0, fUV + vec2(-o.x, -o.y)).rgb * 2.0;
    sum += texture(texture0, fUV + vec2( o.x, -o.y)).rgb * 2.0;
    sum += texture(texture0, fUV + vec2(-o.x,  o.y)).rgb * 2.0;
    sum += texture(texture0, fUV + vec2( o.x,  o.y)).rgb * 2.0;
    vec3 up = sum / 12.0;
    finalColor = vec4(texture(uBase, fUV).rgb + up * uRadius, 1.0);
}
