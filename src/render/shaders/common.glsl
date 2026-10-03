// Shared uniforms and helpers for emissive shaders (included by shaderlib).
uniform float uTime;        // wrapped seconds
uniform vec3  uCamPos;      // render space
uniform vec4  uFog;         // fogStart, fogEnd, fogToCore, unused
uniform vec4  uFade;        // farFadeStart, farFadeEnd, nearFadeStart, nearFadeEnd
uniform vec3  uCoreColor;   // linear
uniform float uChunkFade;   // 0..1 pop-in fade
uniform float uPixelScale;  // world size of one pixel at distance 1
uniform float uMinPx;       // minimum line half-width in pixels
uniform float uIntensity;   // global energy 0..1

float hash11(float p) { p = fract(p * 0.1031); p *= p + 33.33; p *= p + p; return fract(p); }
float hash21(vec2 p) { vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
float hash31(vec3 p3) { p3 = fract(p3 * 0.1031); p3 += dot(p3, p3.zyx + 31.32); return fract((p3.x + p3.y) * p3.z); }
vec3 hash33(vec3 p3) { p3 = fract(p3 * vec3(0.1031, 0.1030, 0.0973)); p3 += dot(p3, p3.yxz + 33.33); return fract((p3.xxy + p3.yxx) * p3.zyx); }

vec3 srgb_to_linear(vec3 c) { return pow(c, vec3(2.2)); }

vec3 rotate_axis(vec3 v, vec3 k, float a)
{
    float c = cos(a), s = sin(a);
    return v * c + cross(k, v) * s + k * dot(k, v) * (1.0 - c);
}

// Distance fog toward the core tint, far fade, near fade, chunk fade.
vec3 apply_atmo(vec3 col, float dist)
{
    float fogT = smoothstep(uFog.x, uFog.y, dist);
    float lum = max(col.r, max(col.g, col.b));
    col = mix(col, uCoreColor * lum * 1.2, fogT * uFog.z);
    float far = 1.0 - smoothstep(uFade.x, uFade.y, dist);
    float near = smoothstep(uFade.z, uFade.w, dist);
    return col * far * near * uChunkFade;
}

// Crisp core plus soft halo across v in -1..1.
float line_profile(float v, float aa)
{
    float a = abs(v);
    float core = 1.0 - smoothstep(0.42 - aa, 0.42 + aa, a);
    float halo = exp(-5.0 * a * a) * 0.35;
    float edge = 1.0 - smoothstep(0.75, 1.0, a);
    return (core + halo) * edge;
}

// Neon color across the line: white-hot core, tinted halo.
vec3 line_color(vec3 tint, float v, float aa, float whiteCore)
{
    float a = abs(v);
    float core = 1.0 - smoothstep(0.42 - aa, 0.42 + aa, a);
    float hot = 1.0 - smoothstep(0.0, 0.3 + aa, a);
    float halo = exp(-5.0 * a * a) * 0.35;
    float edge = 1.0 - smoothstep(0.75, 1.0, a);
    vec3 coreCol = mix(tint, vec3(1.0), whiteCore * hot);
    return (coreCol * core + tint * halo) * edge;
}

// Grows a line's half width to at least uMinPx pixels; returns intensity scale (energy kept).
float min_width(float hw, float dist, out float hwEff)
{
    hwEff = max(hw, uMinPx * dist * uPixelScale);
    return hw / hwEff;
}
