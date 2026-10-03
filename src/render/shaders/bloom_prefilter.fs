#version 330
in vec2 fUV;
out vec4 finalColor;
uniform sampler2D texture0;
uniform vec2 uTexel;        // source texel size
uniform vec2 uThreshold;    // threshold, knee (fraction)

float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

void main()
{
    vec3 a = texture(texture0, fUV + uTexel * vec2(-1.0, -1.0)).rgb;
    vec3 b = texture(texture0, fUV + uTexel * vec2( 1.0, -1.0)).rgb;
    vec3 c = texture(texture0, fUV + uTexel * vec2(-1.0,  1.0)).rgb;
    vec3 d = texture(texture0, fUV + uTexel * vec2( 1.0,  1.0)).rgb;
    // Karis average to suppress fireflies.
    float wa = 1.0 / (1.0 + luma(a)), wb = 1.0 / (1.0 + luma(b)), wc = 1.0 / (1.0 + luma(c)), wd = 1.0 / (1.0 + luma(d));
    vec3 col = (a * wa + b * wb + c * wc + d * wd) / (wa + wb + wc + wd);
    float br = max(col.r, max(col.g, col.b));
    float knee = uThreshold.x * uThreshold.y + 1e-5;
    float soft = clamp(br - uThreshold.x + knee, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 1e-5);
    float contrib = max(soft, br - uThreshold.x) / max(br, 1e-5);
    finalColor = vec4(min(col * contrib, vec3(64.0)), 1.0);
}
