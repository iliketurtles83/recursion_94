#version 330
in vec3 fPos;
in vec3 fNormal;
out vec4 finalColor;
uniform vec3 uColor, uCamPos;
uniform float uEmission;
void main() {
    vec3 n=normalize(fNormal),v=normalize(uCamPos-fPos);
    if(dot(n,v)<0.0)n=-n;
    vec3 l=normalize(vec3(-.45,.8,.38));
    float rim=pow(1.0-max(dot(n,v),0.0),3.0);
    vec3 lit=uColor*(.38+max(dot(n,l),0.0)*1.3)+vec3(.12,.22,.32)*rim;
    finalColor=vec4(uEmission>0.0?uColor*uEmission:lit,1.0);
}
