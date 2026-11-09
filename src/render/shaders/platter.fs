#version 330
// Platter floor: a floppy disk's sector map, per pixel, from polar coordinates around the
// spindle. See env/env_platter.c. Ray/plane intersection and depth write as in grid.fs.
//
// Layout (all pure functions of radius, angle and seed):
//   track   t = floor((rOut - r) / pitch), 0 at the rim
//   sector  phi = angle / 2pi * S + t * skew  (skew staggers sector starts track to track)
//   along   f = fract(phi): header 0.010-0.065, gap, data 0.10-0.965, gap
//   LBA     (t * 2 + head) * S + sector, head 0; LBA < 33 is boot + 2 FATs + root dir
//   files   4-sector blocks; a file starts where hash < fileStart, a block belongs to the
//           nearest start before it; free space grows inward; some blocks are fragments
//   flux    thin MFM transition lines: random for data, the regular format fill when free
in vec2 fUV;
out vec4 finalColor;
uniform float uAspect, uTanHalf;
uniform vec3  uCamRight, uCamUp, uCamFwd;
uniform vec2  uClip;          // near, far
uniform float uFloorY;        // render space
uniform vec2  uCentre;        // spindle centre, render space xz
uniform vec4  uDisk;          // rOut, rIn, tracks, sectors
uniform vec4  uLayout;        // skew, flux cells, hub radius, -
uniform vec4  uAlloc;         // file start chance, fragment chance, bad chance, -
uniform vec4  uLevels;        // header, data, index, ring intensity
uniform vec2  uFloorFog;      // start, end
uniform int   uSeed;
uniform vec3  uFloorColor, uHorizonColor, uSystemColor, uBadColor, uEmptyColor, uHeaderColor;
uniform vec3  uFileA, uFileB, uFileC;
#include "common.glsl"

const float TAU = 6.28318530718;

float h2(int a, int b)
{
    uint h = uint(a) * 0x9E3779B1u + uint(b) * 0x85EBCA77u + uint(uSeed) * 0xC2B2AE3Du;
    h ^= h >> 15; h *= 0x2C1B3C6Du;
    h ^= h >> 12; h *= 0x297A2D39u;
    h ^= h >> 15;
    return float(h & 0xFFFFFFu) / 16777216.0;
}

// Box-filtered indicator of [a, b] at x over a footprint w (fraction of the footprint inside).
float band(float x, float a, float b, float w)
{
    w = max(w, 1e-4);
    return clamp((min(x + 0.5 * w, b) - max(x - 0.5 * w, a)) / w, 0.0, 1.0);
}

// Thin ring of half width hw at radius r0, filtered over footprint w.
float ring(float r, float r0, float hw, float w)
{
    return band(r, r0 - hw, r0 + hw, w);
}

void main()
{
    vec2 ndc = fUV * 2.0 - 1.0;
    vec3 dir = normalize(uCamFwd + uCamRight * ndc.x * uTanHalf * uAspect + uCamUp * ndc.y * uTanHalf);
    float height = uCamPos.y - uFloorY;
    if (height <= 0.0 || dir.y > -1e-6) discard;
    float tHit = height / -dir.y;
    float zv = tHit * dot(dir, uCamFwd);
    if (zv >= uClip.y * 0.999) discard;
    float n = uClip.x, f = uClip.y;
    gl_FragDepth = ((f + n) / (f - n) - (2.0 * f * n) / ((f - n) * zv)) * 0.5 + 0.5;

    vec3 p = uCamPos + dir * tHit;
    vec2 q = p.xz - uCentre;
    float r = length(q);
    // Angle grows clockwise seen from above, the direction the ship travels (path.c).
    float turn = fract(atan(-q.y, q.x) / TAU + 1.0);
    // Pixel footprint in world units; the angle's gradient is 1/r, so no seam at the wrap.
    float wp = max(length(dFdx(p.xz)), length(dFdy(p.xz)));

    float rOut = uDisk.x, rIn = uDisk.y, T = uDisk.z, S = uDisk.w;
    float pitch = (rOut - rIn) / T;
    vec3 col = uFloorColor;

    if (r < rOut && r > rIn) {
        float tf = (rOut - r) / pitch;
        int t = int(floor(tf));
        float u = fract(tf);
        float wT = wp / pitch;                            // footprint in tracks
        float phi = turn * S + float(t) * uLayout.x;
        int s = int(mod(floor(phi), S));
        float fa = fract(phi);
        float wS = wp / max(r, 1e-3) * S / TAU;           // footprint in sectors
        int lba = (t * 2) * int(S) + s;

        // Files.
        int blk = lba / 4;
        int fid = blk - 16;
        for (int k = 0; k < 16; k++) {
            if (h2(blk - k, 7) < uAlloc.x) { fid = blk - k; break; }
        }
        float freeP = 0.15 + 0.6 * clamp(float(t) / T, 0.0, 1.0);
        bool isFree = h2(fid, 3) < freeP;
        if (h2(blk, 11) < uAlloc.y) { fid = blk * 31 + 5; isFree = false; }
        bool isSys = lba < 33;
        bool isBad = h2(lba, 13) < uAlloc.z;
        float hsel = h2(fid, 5);
        vec3 hue = hsel < 0.3333 ? uFileA : (hsel < 0.6667 ? uFileB : uFileC);
        float shade = 0.45 + 0.55 * h2(fid, 9);
        bool fmt = isFree && !isSys;
        vec3 base = isSys ? uSystemColor : (fmt ? uEmptyColor * 1.6 : hue * shade);
        if (isBad) base = uBadColor * 1.6;

        // Flux lines across the track.
        float NC = uLayout.y;
        float fc = fa * NC;
        int cell = int(floor(fc));
        bool bit = fmt ? (cell % 3 == 0) : (h2(lba * 1024 + cell, 17) > 0.5);
        float wF = wS * NC;
        float flux = bit ? band(fract(fc), 0.28, 0.72, wF) : 0.0;
        flux = mix(flux, fmt ? 0.15 : 0.22, smoothstep(0.35, 1.0, wF));

        float header = band(fa, 0.010, 0.065, wS);
        float data = band(fa, 0.10, 0.965, wS);
        float lane = band(u, 0.12, 0.88, wT);

        vec3 c = base * (0.10 + 0.55 * flux) * data * uLevels.y;
        c += uHeaderColor * header * (fmt ? 0.3 : 1.0) * uLevels.x;
        c *= lane;
        // Once tracks shrink below a pixel, settle to the mean instead of shimmering.
        vec3 mean = (uEmptyColor * 0.6 + uFileA * 0.25) * 0.25 * uLevels.y + uHeaderColor * 0.04 * uLevels.x;
        c = mix(c, mean, smoothstep(0.4, 1.2, wT));
        col += c;

        // Index mark: one radial line where every track begins.
        float d = abs(fract(turn + 0.5) - 0.5) * TAU * r;
        col += uHeaderColor * band(d, -0.12, 0.12, wp) * uLevels.z;
    }

    // Rim, inner edge and hub rings.
    col += vec3(0.45, 0.75, 1.0) * ring(r, rOut + 1.5, 0.25, wp) * uLevels.w;
    col += vec3(0.45, 0.75, 1.0) * ring(r, rIn - 1.0, 0.12, wp) * uLevels.w * 0.5;
    col += vec3(1.0, 0.65, 0.3) * ring(r, uLayout.z, 0.3, wp) * uLevels.w;
    if (r > rOut + 3.0) col = uFloorColor * 0.4;

    col = mix(col, uHorizonColor, smoothstep(uFloorFog.x, uFloorFog.y, tHit));
    finalColor = vec4(col, 1.0);
}
