#version 330

in vec3 fragPosition;
in vec3 fragNormal;
in vec4 fragColor;

out vec4 finalColor;

uniform float uTime;
uniform float uIntensity;
uniform vec3 uPrimaryColor;
uniform vec3 uSecondaryColor;
uniform int uObjectClass;
uniform int uEnemyType;

void main()
{
    vec3 base = fragColor.rgb;
    float normalLength = length(fragNormal);
    vec3 N = normalLength > 0.1 ? normalize(fragNormal) : vec3(0.0, 1.0, 0.0);
    vec3 lightDir = normalize(vec3(-0.38, 0.82, 0.44));
    float lightFloor = uObjectClass == 1 ? 0.36 : 0.30;
    float diffuse = lightFloor + (1.0 - lightFloor) * max(dot(N, lightDir), 0.0);

    vec3 viewDir = normalize(vec3(0.0, 5.2, 9.0) - fragPosition);
    vec3 halfDir = normalize(lightDir + viewDir);
    float specular = pow(max(dot(N, halfDir), 0.0), 28.0);
    float rim = pow(1.0 - max(dot(N, viewDir), 0.0), 2.6);

    float brightest = max(max(base.r, base.g), base.b);
    float darkest = min(min(base.r, base.g), base.b);
    float saturation = brightest - darkest;
    float emissive = smoothstep(0.52, 0.92, brightest) *
                     smoothstep(0.12, 0.42, saturation);
    float energy = 0.92 + 0.08 * sin(uTime * 2.1 + fragPosition.z * 0.11);

    vec3 metal = base * diffuse;
    metal += mix(uPrimaryColor, uSecondaryColor, 0.36) * specular * 0.22;
    metal += mix(base, uPrimaryColor, 0.22) * rim * 0.13;
    vec3 lit = mix(metal, base * energy, emissive * (0.50 + uIntensity * 0.12));

    // Enemies occupy a separate visual hierarchy from the cool architecture.
    // Raised shadow color, a warm rim, and stronger saturated emission preserve
    // their silhouettes in dense districts without adding UI outlines.
    if (uObjectClass == 1) {
        vec3 enemyAccent = mix(uSecondaryColor, vec3(1.0, 0.72, 0.20), 0.14);
        lit = max(lit, base * 0.56 + enemyAccent * 0.012);
        lit += enemyAccent * rim * 0.18;
        lit += mix(base, enemyAccent, 0.24) * emissive * 0.075;

        // Per-type signatures so silhouettes read apart at a glance instead of
        // collapsing into the same generic "glowing hull" treatment.
        if (uEnemyType == 0) {
            // Drifter: slow plasma pulse riding the hull, mesh-locked via the normal.
            float plasma = sin(atan(N.y, N.x) * 5.0 + fragPosition.z * 0.4 + uTime * 3.0) * 0.5 + 0.5;
            lit += enemyAccent * plasma * 0.16;
        } else if (uEnemyType == 1) {
            // Chaser: scrolling speed streaks along its long axis sell the dive.
            float streak = smoothstep(0.7, 1.0, fract(fragPosition.z * 2.2 - uTime * 6.0));
            lit += enemyAccent * streak * rim * 0.55;
        } else if (uEnemyType == 2) {
            // Splitter: posterized facets foreshadow its blocky fracture-on-death.
            float voxelBand = floor(diffuse * 4.0) / 4.0;
            lit = mix(lit, base * (0.4 + voxelBand * 0.6), 0.5);
        } else if (uEnemyType == 4) {
            // Boss core: angular banded glow (not radial) plus a slow fractal
            // crack pattern so the silhouette reads as a faceted machine, not a circle.
            float band = cos(atan(N.y, N.x) * 8.0 + uTime * 0.4) * 0.5 + 0.5;
            float crack = abs(sin(fragPosition.x * 6.0) * sin(fragPosition.y * 6.0) *
                              sin(fragPosition.z * 6.0 + uTime * 0.6));
            lit += enemyAccent * band * 0.22;
            lit += mix(base, vec3(1.0, 0.9, 0.8), 0.5) * smoothstep(0.82, 0.98, crack) * 0.4;
        }
    } else if (uObjectClass == 2) {
        lit += mix(uPrimaryColor, uSecondaryColor, 0.5) * rim * 0.09;
    }

    finalColor = vec4(lit, fragColor.a);
}
