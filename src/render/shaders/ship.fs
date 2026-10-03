#version 330
in vec3 fNormal;
in vec3 fView;
in vec3 fColor;
out vec4 finalColor;
uniform vec3 uRimColor;
uniform vec3 uFillDir;      // light from the core, render space
void main()
{
    vec3 n = normalize(fNormal);
    if (dot(n, fView) < 0.0) n = -n;
    float rim = pow(1.0 - max(dot(n, fView), 0.0), 3.0);
    float fill = max(dot(n, -uFillDir), 0.0);
    vec3 col = fColor * (0.25 + 0.6 * fill) + uRimColor * rim * 0.8;
    finalColor = vec4(col, 1.0);
}
