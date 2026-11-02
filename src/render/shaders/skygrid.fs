#version 330
// Sparse "ceiling" lattice high above an open environment (render/gridfloor.h, ceiling mode):
// each pixel intersects its view ray with the plane y = uFloorY (above the camera), and draws
// only the cell edges whose hash passes uSparse, as thin faint lines. Additive (emissive pass)
// with gl_FragDepth, so nearer geometry occludes it. Filtered like the floor grid.
in vec2 fUV;
out vec4 finalColor;
uniform float uAspect, uTanHalf;
uniform vec3  uCamRight, uCamUp, uCamFwd;
uniform vec2  uClip;
uniform float uFloorY;        // plane height, render space
uniform vec2  uGridOffset;
uniform float uCell;
uniform float uSparse;        // fraction of cell edges drawn
uniform vec2  uLine;          // core half width (cells), unused
uniform vec4  uLineI;         // intensity, -, -, -
uniform vec4  uFloorFog;      // fade start, fade end
uniform vec3  uLineColor;
#include "common.glsl"

// One axis: coverage of the nearest line at coordinate u (cells), lit for the segment along v.
float axis(float u, float v, float w, float wv, float salt)
{
    float li = floor(u + 0.5);
    float d = abs(u - li);
    float hw = uLine.x;
    float dw = max(hw, w * 0.5);
    float aa = w * 0.75;
    float c = (1.0 - smoothstep(dw - aa, dw + aa, d)) * (hw / dw);
    c = mix(c, 2.0 * hw, clamp(w * 2.0 - 1.0, 0.0, 1.0));
    float lit = step(hash21(vec2(li, floor(v)) + salt), uSparse);
    lit = mix(lit, uSparse, smoothstep(0.3, 1.0, max(w, wv)));   // sub-pixel segments: their mean
    return c * lit;
}

void main()
{
    vec2 ndc = fUV * 2.0 - 1.0;
    vec3 dir = normalize(uCamFwd + uCamRight * ndc.x * uTanHalf * uAspect + uCamUp * ndc.y * uTanHalf);
    float height = uFloorY - uCamPos.y;
    if (height <= 0.0 || dir.y < 1e-6) discard;
    float t = height / dir.y;
    float zv = t * dot(dir, uCamFwd);
    if (zv >= uClip.y * 0.999) discard;
    float n = uClip.x, f = uClip.y;
    gl_FragDepth = ((f + n) / (f - n) - (2.0 * f * n) / ((f - n) * zv)) * 0.5 + 0.5;
    vec3 p = uCamPos + dir * t;
    vec2 g = (p.xz + uGridOffset) / uCell;
    vec2 w = fwidth(g);
    float cov = max(axis(g.x, g.y, w.x, w.y, 0.0), axis(g.y, g.x, w.y, w.x, 17.0));
    // Lines sparser than a pixel converge to their mean; the whole plane fades with distance.
    float fade = 1.0 - smoothstep(uFloorFog.x, uFloorFog.y, t);
    vec3 col = uLineColor * cov * uLineI.x * fade;
    finalColor = vec4(col, 1.0);
}
