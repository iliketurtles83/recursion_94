#version 330
// GPU-only speed streaks. One static mesh of quads; everything is derived from the
// per-streak id. Streaks live in a cylinder around the path (uVolume 0, tunnels) or in a
// slab above the floor (uVolume 1, open environments), following the path's curves.
in vec3 vertexPosition;
in vec2 vertexTexCoord;     // u: 0 = near end, 1 = far end; v: side
in vec2 vertexTexCoord2;    // streak id, unused
uniform mat4 matView;
uniform mat4 matProjection;
// Path frames sampled every uPathStep units from z = -uZBehind (render space).
#define PATH_N 16
uniform vec4 uPathP[PATH_N];   // center xyz, radius
uniform vec4 uPathB[PATH_N];   // right
uniform vec4 uPathN[PATH_N];   // up
uniform float uPathStep;
uniform float uTravel;      // wrapped distance travelled
uniform float uSpeed;       // effective speed for stretching
uniform float uZRange, uZBehind;
uniform float uDensity;     // fraction of streaks drawn
uniform float uLenMul;
uniform vec2  uWidth;       // min, max
uniform float uHelixFrac, uHelixRate;
uniform float uCometFrac, uPurpleFrac;
uniform float uStreakIntensity;
uniform vec3  uColWhite, uColPurple;
uniform float uVolume;      // 0 cylinder, 1 slab
uniform vec3  uSlab;        // half width, y min, y max (path frame, N = up)
uniform float uFallTilt;    // slab: rise per unit of distance ahead (falling rain)
uniform vec2  uSlabGap;     // slab: half width of the mostly empty centre band, fraction of streaks allowed in it
#include "common.glsl"
out vec2 fUV;
out vec3 fColor;
out float fIntensity;
out float fComet;
void main()
{
    float id = vertexTexCoord2.x;
    float h1 = hash11(id * 1.37 + 0.11);
    if (h1 > uDensity) { gl_Position = vec4(2.0, 2.0, 2.0, 1.0); fUV = vec2(0.0); fColor = vec3(0.0); fIntensity = 0.0; fComet = 0.0; return; }
    float h2 = hash11(id * 2.71 + 0.37), h3 = hash11(id * 3.17 + 0.59), h4 = hash11(id * 5.03 + 0.83);
    float h5 = hash11(id * 7.39 + 0.21), h6 = hash11(id * 11.3 + 0.47), h7 = hash11(id * 13.7 + 0.71);
    float h8 = hash11(id * 17.9 + 0.13), h9 = hash11(id * 19.1 + 0.97);
    float theta = h2 * 6.2831853;
    float rf = mix(0.2, 1.2, pow(h3, 0.35));
    // Speed multiplier quantized to 1/8 steps so the travel wrap is seamless.
    float m = (5.0 + floor(h4 * 9.0)) / 8.0;
    float z = mod(h5 * uZRange - uTravel * m, uZRange) - uZBehind;
    if (h6 < uHelixFrac) {
        rf = mix(0.92, 1.15, h3);
        theta += uHelixRate * z;
    }
    bool comet = h7 < uCometFrac;
    float len = max(uSpeed * m * 0.06 * uLenMul, 0.15) * (comet ? 3.0 : 1.0);
    float w = mix(uWidth.x, uWidth.y, h8) * (comet ? 1.7 : 1.0);
    float zz = z + vertexTexCoord.x * len;
    float fi = clamp((zz + uZBehind) / uPathStep, 0.0, float(PATH_N - 1) - 0.001);
    int i0 = int(fi);
    float f = fi - float(i0);
    vec4 P = mix(uPathP[i0], uPathP[i0 + 1], f);
    vec3 B = normalize(mix(uPathB[i0].xyz, uPathB[i0 + 1].xyz, f));
    vec3 N = normalize(mix(uPathN[i0].xyz, uPathN[i0 + 1].xyz, f));
    vec3 p, dir;
    if (uVolume > 0.5) {
        // Higher the further ahead, so streaks descend as they approach: falling rain.
        // Keep the band over the lane (where the action is) nearly clear: most streaks
        // go to the sides, only uSlabGap.y of them into the centre.
        float side = h2 < 0.5 ? -1.0 : 1.0, u = fract(h2 * 2.0);
        float gap = min(uSlabGap.x, uSlab.x);
        float sx = h6 < uSlabGap.y ? mix(-gap, gap, u) : side * mix(gap, uSlab.x, u);
        float sy = mix(uSlab.y, uSlab.z, h3) + uFallTilt * max(zz, 0.0);
        p = P.xyz + B * sx + N * sy;
        dir = normalize(normalize(uPathP[i0 + 1].xyz - uPathP[i0].xyz) + N * uFallTilt);
    } else {
        float r = P.w * rf;
        p = P.xyz + B * (cos(theta) * r) + N * (sin(theta) * r);
        dir = normalize(uPathP[i0 + 1].xyz - uPathP[i0].xyz);
    }
    vec3 toCam = normalize(uCamPos - p);
    vec3 side = normalize(cross(dir, toCam) + vec3(1e-5));
    float dist = length(uCamPos - p);
    float wEff;
    float k = min_width(w, dist, wEff);
    p += side * wEff * vertexTexCoord.y;
    float zFar = uZRange - uZBehind;
    float fade = smoothstep(-uZBehind, -uZBehind + 6.0, z) * (1.0 - smoothstep(zFar - 50.0, zFar, z + len));
    // Streaks right next to the camera smear across the screen; keep them faint.
    fade *= smoothstep(2.0, 12.0, dist);
    fUV = vertexTexCoord;
    fColor = (h9 < uPurpleFrac) ? uColPurple : uColWhite;
    fIntensity = uStreakIntensity * k * fade * (comet ? 2.5 : 1.0) * mix(0.5, 1.2, h8);
    fComet = comet ? 1.0 : 0.0;
    gl_Position = matProjection * matView * vec4(p, 1.0);
}
