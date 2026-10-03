#version 330
// Screen-space core light: hot center, halo, anamorphic streak, faint rays.
in vec2 fUV;
out vec4 finalColor;
uniform vec2  uCorePos;     // uv
uniform float uAspect;
uniform float uCoreIntensity;
uniform float uCoreSize;
uniform float uStreak;
uniform float uRays;
uniform float uRayRot;
uniform float uVisible;
uniform vec3  uColor;
void main()
{
    vec2 d = (fUV - uCorePos) * vec2(uAspect, 1.0);
    float r = length(d);
    float s = uCoreSize;
    float core = exp(-r / (s * 0.12)) + 0.35 * exp(-r / (s * 0.35));
    float halo = 0.08 * exp(-r / (s * 1.6)) + 0.02 * exp(-r / (s * 5.0));
    float streak = exp(-abs(d.y) / (s * 0.035)) * exp(-abs(d.x) / (s * 4.0)) * uStreak;
    float a = atan(d.y, d.x);
    float rays = (pow(abs(cos(a * 3.0 + uRayRot)), 60.0) + 0.6 * pow(abs(cos(a * 2.0 - uRayRot * 0.7 + 0.5)), 80.0));
    rays *= exp(-r / (s * 2.2)) * uRays;
    vec3 col = uColor * (core + halo) * uCoreIntensity + uColor * (streak + rays) * uCoreIntensity * 0.15;
    finalColor = vec4(col * uVisible, 1.0);
}
