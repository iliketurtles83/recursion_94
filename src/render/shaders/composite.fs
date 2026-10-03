#version 330
in vec2 fUV;
out vec4 finalColor;
uniform sampler2D texture0;     // HDR scene
uniform sampler2D uBloom;
uniform float uBloomIntensity;
uniform vec2  uResolution;
uniform vec2  uVanish;          // uv, y up
uniform float uZoom;            // radial blur strength
uniform vec2  uZoomMask;        // inner, outer (aspect-corrected distance)
uniform int   uZoomTaps;
uniform float uCA;              // pixels at the edge
uniform float uExposure;
uniform int   uTonemap;
uniform float uSaturation;
uniform float uVignette;
uniform float uGrain;
uniform float uWhiteout;
uniform float uTime;

vec3 hdr(vec2 uv) { return texture(texture0, uv).rgb + texture(uBloom, uv).rgb * uBloomIntensity; }

const mat3 ACESIn = mat3(0.59719, 0.07600, 0.02840, 0.35458, 0.90834, 0.13383, 0.04823, 0.01566, 0.83777);
const mat3 ACESOut = mat3(1.60475, -0.10208, -0.00327, -0.53108, 1.10813, -0.07276, -0.07367, -0.00605, 1.07602);
vec3 rrt_odt(vec3 v) { vec3 a = v * (v + 0.0245786) - 0.000090537; vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081; return a / b; }
vec3 aces(vec3 c) { return clamp(ACESOut * rrt_odt(ACESIn * c), 0.0, 1.0); }

float hash12(vec2 p) { vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }

void main()
{
    vec2 uv = fUV;
    float aspect = uResolution.x / uResolution.y;
    vec2 fromV = uv - uVanish;
    float d = length(fromV * vec2(aspect, 1.0));
    float mask = smoothstep(uZoomMask.x, uZoomMask.y, d);
    float strength = uZoom * mask;
    vec2 caOff = (d > 1e-4 ? fromV / max(length(fromV), 1e-4) : vec2(0.0)) * uCA * smoothstep(0.0, 0.9, d) / uResolution;

    int taps = (strength * length(fromV * uResolution) > 0.75) ? uZoomTaps : 1;
    vec3 acc = vec3(0.0);
    for (int i = 0; i < taps; i++) {
        float t = taps > 1 ? float(i) / float(taps - 1) : 0.0;
        vec2 suv = uv - fromV * strength * t;
        if (uCA > 0.0) {
            acc.r += hdr(suv + caOff).r;
            acc.g += hdr(suv).g;
            acc.b += hdr(suv - caOff).b;
        } else {
            acc += hdr(suv);
        }
    }
    vec3 col = acc / float(taps);

    col *= uExposure;
    if (uTonemap == 1) col = aces(col);
    else col = clamp(col, 0.0, 1.0);
    float l = dot(col, vec3(0.2126, 0.7152, 0.0722));
    col = clamp(mix(vec3(l), col, uSaturation), 0.0, 1.0);

    float v = length((uv - 0.5) * vec2(aspect, 1.0));
    col *= mix(1.0, smoothstep(1.15, 0.25, v), uVignette);

    col = pow(col, vec3(1.0 / 2.2));
    col += (hash12(uv * uResolution + fract(uTime * 7.31) * 157.0) - 0.5) * uGrain;
    col = mix(col, vec3(1.0), clamp(uWhiteout, 0.0, 1.0));
    finalColor = vec4(col, 1.0);
}
