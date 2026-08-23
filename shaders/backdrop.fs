#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

out vec4 finalColor;

uniform vec2 uResolution;
uniform float uTime;
uniform int uRunSeed;
uniform vec3 uPrimaryColor;
uniform vec3 uSecondaryColor;

vec3 cyclePalette(float phase, float height) {
    vec3 nightLow = vec3(0.008, 0.012, 0.040);
    vec3 nightHigh = vec3(0.001, 0.003, 0.014);
    vec3 dawnLow = vec3(0.95, 0.12, 0.32);
    vec3 dawnHigh = vec3(0.08, 0.22, 0.48);
    vec3 dayLow = vec3(0.20, 0.58, 0.78);
    vec3 dayHigh = vec3(0.035, 0.22, 0.52);
    vec3 duskLow = vec3(1.00, 0.20, 0.08);
    vec3 duskHigh = vec3(0.20, 0.06, 0.38);

    vec3 low;
    vec3 high;
    if (phase < 0.25) {
        float blend = smoothstep(0.0, 0.25, phase);
        low = mix(nightLow, dawnLow, blend);
        high = mix(nightHigh, dawnHigh, blend);
    } else if (phase < 0.50) {
        float blend = smoothstep(0.25, 0.50, phase);
        low = mix(dawnLow, dayLow, blend);
        high = mix(dawnHigh, dayHigh, blend);
    } else if (phase < 0.75) {
        float blend = smoothstep(0.50, 0.75, phase);
        low = mix(dayLow, duskLow, blend);
        high = mix(dayHigh, duskHigh, blend);
    } else {
        float blend = smoothstep(0.75, 1.0, phase);
        low = mix(duskLow, nightLow, blend);
        high = mix(duskHigh, nightHigh, blend);
    }
    return mix(low, high, smoothstep(0.0, 1.0, height));
}

void main() {
    vec2 uv = fragTexCoord * 2.0 - 1.0;
    uv.x *= uResolution.x / max(uResolution.y, 1.0);

    float seedPhase = float(uint(uRunSeed) & 2047u) * 0.003067962;
    float spin = uTime * 0.05 + seedPhase * 6.2831853;
    float cs = cos(spin);
    float sn = sin(spin);

    vec3 rd = normalize(vec3(uv, 1.35));
    rd.xz = mat2(cs, -sn, sn, cs) * rd.xz;

    // Bounded box-fold raymarch (same recursive-cage technique as the
    // landmark core in tower.fs) used purely as a screen-space sky glow.
    vec3 p = vec3(0.0, 0.0, uTime * 0.12);
    float marched = 0.55;
    float glow = 0.0;
    for (int i = 0; i < 20; i++) {
        vec3 q = p + rd * marched;
        float scale = 1.0;
        q = abs(q) - 1.05;
        for (int f = 0; f < 4; f++) {
            q = abs(q) - (0.62 + 0.05 * sin(uTime * 0.23 + float(f) + seedPhase * 4.0));
            q.xy = q.x > q.y ? q.xy : q.yx;
            q.yz = q.y > q.z ? q.yz : q.zy;
            q *= 1.85;
            scale *= 1.85;
        }
        float d = (length(max(abs(q) - 0.9, 0.0)) - 0.14) / scale;
        d = abs(d) + 0.02;
        glow += 0.020 / (0.05 + d * d * 34.0);
        marched += max(d * 0.68, 0.05);
        if (marched > 22.0) break;
    }
    glow = clamp(glow, 0.0, 1.6);

    float cycle = fract(uTime / 96.0 + seedPhase * 0.08);
    float twilight = pow(abs(sin(cycle * 6.2831853)), 8.0);
    vec3 fractalColor = mix(uPrimaryColor, uSecondaryColor,
                            sin(marched * 0.22 + uTime * 0.4) * 0.5 + 0.5) * glow;

    // Fades top/bottom so the fractal reads as a horizon glow rather than a
    // full skybox, leaving room for the moon/aurora/silhouette sprites on top.
    float horizonMask = exp(-pow((fragTexCoord.y - 0.50) * 2.6, 2.0));
    vec3 base = cyclePalette(cycle, fragTexCoord.y);
    float digitalBands = step(0.72, fract(sin(floor(fragTexCoord.y * 72.0) +
                              seedPhase * 31.0) * 43758.5453));
    vec3 sunsetSignal = mix(vec3(1.0, 0.08, 0.32), vec3(0.05, 0.75, 1.0),
                            step(0.5, fract(fragTexCoord.y * 31.0 + seedPhase)));
    base += sunsetSignal * digitalBands * twilight * horizonMask * 0.045;

    finalColor = vec4(base + fractalColor * horizonMask * (0.45 + twilight * 0.55), 1.0) * fragColor;
}
