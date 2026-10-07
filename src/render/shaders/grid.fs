#version 330
// Infinite neon grid floor: per-pixel ray/plane intersection, filtered grid, depth write.
// See render/gridfloor.h.
in vec2 fUV;
out vec4 finalColor;
uniform float uAspect, uTanHalf;
uniform vec3  uCamRight, uCamUp, uCamFwd;
uniform vec2  uClip;          // near, far
uniform float uFloorY;        // render space
uniform vec2  uGridOffset;    // world grid phase of render-space origin (xz)
uniform float uCell;
uniform float uFilter;        // 0 = debug: no footprint filtering
uniform float uNodeSize;      // intersection pad half size (cells)
uniform vec2  uRefl;          // reflection strength, Fresnel R0
uniform sampler2D uReflection;   // mirrored scene, blurred; same screen uv as this pixel
uniform vec2  uLine;          // core half width, halo width (in cells)
uniform vec4  uLineI;         // core HDR, halo rel., node boost, white core
uniform vec4  uFloorFog;      // fog start, fog end, horizon glow, sheen
uniform vec3  uLineColor, uFloorColor, uHorizonColor;   // linear
#include "common.glsl"

// Coverage of a box of half width hw around the nearest line, averaged over the footprint w
// (widened to >= half a pixel with its energy kept, mean coverage once cells are sub-pixel).
float box_cov(float d, float hw, float w)
{
    float dw = max(hw, w * 0.5);
    float aa = w * 0.75;
    float c = (1.0 - smoothstep(dw - aa, dw + aa, d)) * (hw / dw);
    return mix(c, 2.0 * hw, clamp(w * 2.0 - 1.0, 0.0, 1.0));
}

// One grid axis. u: coordinate in cells; w: pixel footprint in cells.
// Returns (core coverage, halo), both averaged over the footprint, so lines whose
// spacing approaches a pixel converge to their mean brightness instead of aliasing.
vec2 grid_axis(float u, float w)
{
    float d = abs(fract(u + 0.5) - 0.5);            // distance to nearest line (cells)
    // Core: box of half width hw, widened to >= half a pixel with its energy kept.
    float hw = uLine.x;
    float dw = max(hw, w * 0.5);
    float aa = w * 0.75;
    float core = (1.0 - smoothstep(dw - aa, dw + aa, d)) * (hw / dw);
    core = mix(core, 2.0 * hw, clamp(w * 2.0 - 1.0, 0.0, 1.0));
    // Halo: gaussian widened by the footprint (energy kept), then its lattice mean.
    float h = uLine.y;
    float hf = sqrt(h * h + 0.25 * w * w);
    float halo = (h / hf) * exp(-(d * d) / (hf * hf));
    halo = mix(halo, h * 1.7724539, smoothstep(0.12, 0.35, hf));
    return vec2(core, halo);
}

void main()
{
    vec2 ndc = fUV * 2.0 - 1.0;
    vec3 dir = normalize(uCamFwd + uCamRight * ndc.x * uTanHalf * uAspect + uCamUp * ndc.y * uTanHalf);
    float height = uCamPos.y - uFloorY;
    if (height <= 0.0 || dir.y > -1e-6) discard;
    float t = height / -dir.y;
    float zv = t * dot(dir, uCamFwd);               // view-space depth
    if (zv >= uClip.y * 0.999) discard;
    float n = uClip.x, f = uClip.y;
    gl_FragDepth = ((f + n) / (f - n) - (2.0 * f * n) / ((f - n) * zv)) * 0.5 + 0.5;

    vec3 p = uCamPos + dir * t;
    vec2 g = (p.xz + uGridOffset) / uCell;
    vec2 w = uFilter > 0.5 ? fwidth(g) : vec2(1e-4);
    vec2 ax = grid_axis(g.x, w.x), az = grid_axis(g.y, w.y);
    float core = ax.x + az.x - ax.x * az.x;
    vec2 mx=grid_axis(g.x*.25,w.x*.25), mz=grid_axis(g.y*.25,w.y*.25);
    core=core*.35+(mx.x+mz.x-mx.x*mz.x)*.65;
    float halo = ax.y + az.y;
    // Square pads where lines cross.
    float dx = abs(fract(g.x + 0.5) - 0.5), dz = abs(fract(g.y + 0.5) - 0.5);
    float node = box_cov(dx, uNodeSize, w.x) * box_cov(dz, uNodeSize, w.y);

    float energy = 0.85 + 0.3 * uIntensity;
    vec3 coreCol = mix(uLineColor, vec3(1.0), uLineI.w);
    vec3 lines = (coreCol * core + uLineColor * (halo * uLineI.y + node * uLineI.z)) * uLineI.x * energy;

    // Glossy dark floor: grazing angles pick up the horizon colour (until real reflections).
    float graze = pow(1.0 - clamp(-dir.y, 0.0, 1.0), 6.0);
    vec3 horizon = uHorizonColor * uFloorFog.z;
    vec3 col = uFloorColor + lines + horizon * graze * uFloorFog.w;
    if (uRefl.x > 0.0) {
        float fres = uRefl.y + (1.0 - uRefl.y) * pow(1.0 - clamp(-dir.y, 0.0, 1.0), 5.0);
        col += texture(uReflection, fUV).rgb * uRefl.x * fres;
    }
    col = mix(col, horizon, smoothstep(uFloorFog.x, uFloorFog.y, t));
    finalColor = vec4(col, 1.0);
}
