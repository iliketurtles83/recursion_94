#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

out vec4 finalColor;

uniform vec2 uResolution;
uniform float uTime;
uniform int uRunSeed;
uniform vec3 uPrimaryColor;
uniform vec3 uSecondaryColor;

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

    vec3 fractalColor = mix(uPrimaryColor, uSecondaryColor,
                            sin(marched * 0.22 + uTime * 0.4) * 0.5 + 0.5) * glow;

    // Fades top/bottom so the fractal reads as a horizon glow rather than a
    // full skybox, leaving room for the moon/aurora/silhouette sprites on top.
    float horizonMask = exp(-pow((fragTexCoord.y - 0.50) * 2.6, 2.0));
    vec3 base = mix(vec3(0.001, 0.002, 0.008), vec3(0.004, 0.007, 0.018), fragTexCoord.y);

    finalColor = vec4(base + fractalColor * horizonMask, 1.0) * fragColor;
}
