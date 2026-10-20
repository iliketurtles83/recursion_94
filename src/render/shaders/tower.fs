#version 330
// Dark monolith bodies: procedural panel seams and recessed panels, a faint cyan rim,
// underglow from the grid floor, and fog toward the sky colour along the view ray (same
// formula as sky.fs) so distant towers dissolve into the horizon without popping.
in vec3 fPos;
in vec2 fUV;
in vec3 fNormal;
in vec2 fInfo;
in float fFace;
in vec3 fTint;
out vec4 finalColor;
uniform vec3  uBody;        // linear base colour
uniform vec3  uRim;         // linear rim colour * intensity
uniform vec3  uUnderglow;   // linear grid bounce colour * intensity
uniform vec3  uKeyDir;      // unit, toward the light
uniform vec4  uPanel;       // panel width, panel height, seam darkening, recessed fraction
uniform vec2  uFogRange;    // start, end
uniform vec3  uZenith, uHorizon;
uniform vec2  uHaze;        // width, horizon multiplier
uniform float uWindows;     // 0 = original monolith, 1 = DATA // RUN lit windows (city cityGameLook only)
#include "common.glsl"

vec3 sky_color(vec3 dir)
{
    float e = max(dir.y, 0.0);
    float band = exp(-e / max(uHaze.x, 1e-4));
    return mix(uZenith, uHorizon, band) * mix(1.0, uHaze.y, band);
}

void main()
{
    vec3 n = normalize(fNormal);
    vec3 toCam = uCamPos - fPos;
    float dist = length(toCam);
    vec3 V = toCam / dist;
    float seed = fInfo.x * 97.0 + fFace * 13.0;

    // Panels: columns of uPanel.x, rows of uPanel.y with a per-column vertical offset.
    float column = floor(fUV.x / uPanel.x);
    float rowOff = hash11(column * 7.13 + seed) * uPanel.y;
    vec2 q = vec2(fUV.x / uPanel.x, (fUV.y + rowOff) / uPanel.y);
    vec2 cell = floor(q), f = fract(q);
    vec2 w = fwidth(q);
    vec2 edge = min(f, 1.0 - f);
    float seamX = 1.0 - smoothstep(0.0, 0.04 + w.x * 1.5, edge.x);
    float seamY = 1.0 - smoothstep(0.0, 0.03 + w.y * 1.5, edge.y);
    // Fade seams out where panels shrink below a few pixels (no shimmer at distance).
    float seam = max(seamX * (1.0 - smoothstep(0.15, 0.4, w.x)), seamY * (1.0 - smoothstep(0.15, 0.4, w.y)));
    float h = hash21(cell + vec2(seed, seed * 0.37));
    float shade = mix(0.7, 1.2, h);
    if (h > 1.0 - uPanel.w) shade *= 0.45;

    float key = max(dot(n, uKeyDir), 0.0);
    float roof = fFace > 3.5 ? 0.5 : 1.0;
    vec3 col = uBody * fTint * shade * mix(0.3 + 0.7 * key, 0.42 + 0.85 * key, uWindows) * roof * (1.0 - uPanel.z * seam);
    if (uWindows > 0.0) {
        // Glazed windows and metal mullions make the towers read as architecture.
        float window = smoothstep(.13,.19,f.x)*(1.0-smoothstep(.76,.83,f.x))
                     * smoothstep(.25,.32,f.y)*(1.0-smoothstep(.63,.72,f.y));
        float occupied = step(.72,h) * step(fFace,3.5);
        vec3 lamp = mix(vec3(.13,.34,.43),vec3(.62,.37,.15),step(.93,h));
        vec3 lit = mix(col,col*.48,window*.45);
        lit += lamp * window * occupied * .45;
        vec3 H = normalize(V+uKeyDir);
        lit += vec3(.08,.16,.22)*pow(max(dot(n,H),0.0),48.0)*window;
        lit += uBody*.16*(1.0-seam)*smoothstep(.0,.025,edge.x);
        col = mix(col, lit, uWindows);
    }
    float rim = pow(1.0 - max(dot(n, V), 0.0), 4.0);
    col += uRim * rim * roof;
    col += uUnderglow * exp(-fUV.y * 0.6) * (fFace < 3.5 ? 1.0 : 0.0);

    col = mix(col, sky_color(-V), smoothstep(uFogRange.x, uFogRange.y, dist));
    finalColor = vec4(col, 1.0);
}
