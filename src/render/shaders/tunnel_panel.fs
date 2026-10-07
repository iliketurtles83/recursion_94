#version 330
in vec3 fPos,fNormal;
in vec2 fUV;
out vec4 finalColor;
uniform vec3 uCamPos;
void main(){
    vec3 n=normalize(fNormal),v=normalize(uCamPos-fPos);
    if(dot(n,v)<0.0)n=-n;
    vec2 grid=fUV*vec2(3,10),cell=abs(fract(grid)-.5),aa=max(fwidth(grid),vec2(.001));
    float seam=max(smoothstep(.46-aa.x,.49+aa.x,cell.x),smoothstep(.46-aa.y,.49+aa.y,cell.y));
    float bolt=1-smoothstep(.045,.065+length(aa),length(fract(grid)-vec2(.16)));
    float edge=1-smoothstep(.015,.035+fwidth(fUV.x),min(fUV.x,1-fUV.x));
    float strut=1-smoothstep(.015,.026+fwidth(fUV.y),abs(fUV.y-.5));
    float vent=(1-smoothstep(.16,.22,abs(fUV.x-.5)))*smoothstep(.6,.64,fUV.y)*(1-smoothstep(.9,.94,fUV.y));
    float slats=.55+.45*smoothstep(.15,.4,fract(fUV.y*50));
    vec3 metal=vec3(.045,.079,.105)*(1-seam*.65)*(1-vent*slats*.6);
    metal+=vec3(.07,.10,.14)*(bolt*.4+strut*.3);
    vec3 col=metal*(.7+.5*max(dot(n,normalize(vec3(-.4,.8,.3))),0.0));
    col+=vec3(.025,.22,.30)*edge*.5;
    float fog=exp(-max(length(uCamPos-fPos)-20,0.0)*.009);
    finalColor=vec4(mix(vec3(.004,.014,.025),col,fog),1);
}
