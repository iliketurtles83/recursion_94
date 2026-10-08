#version 330
in vec3 fPos,fNormal;
in vec2 fUV;
in float fPart;
out vec4 finalColor;
uniform vec3 uCamPos;
uniform vec3 uTheme;
void main(){
    vec3 n=normalize(fNormal),v=normalize(uCamPos-fPos);
    if(dot(n,v)<0.0)n=-n;
    float distance=length(uCamPos-fPos),detail=1.0-smoothstep(45.0,130.0,distance);
    vec2 grid=fUV*vec2(3,10),cell=abs(fract(grid)-.5),aa=max(fwidth(grid),vec2(.001));
    float seam=max(smoothstep(.46-aa.x,.49+aa.x,cell.x),smoothstep(.46-aa.y,.49+aa.y,cell.y));
    float bolt=1-smoothstep(.045,.065+length(aa),length(fract(grid)-vec2(.16)));
    float edge=1-smoothstep(.015,.035+fwidth(fUV.x),min(fUV.x,1-fUV.x));
    float strut=1-smoothstep(.015,.026+fwidth(fUV.y),abs(fUV.y-.5));
    float vent=(1-smoothstep(.16,.22,abs(fUV.x-.5)))*smoothstep(.6,.64,fUV.y)*(1-smoothstep(.9,.94,fUV.y));
    float slats=.55+.45*smoothstep(.15,.4,fract(fUV.y*50));
    seam*=detail;bolt*=detail;strut*=detail;vent*=detail;
    vec3 metal=vec3(.045,.079,.105)*(1-seam*.65)*(1-vent*slats*.6);
    metal+=vec3(.07,.10,.14)*(bolt*.4+strut*.3);
    vec3 col=metal*(.7+.5*max(dot(n,normalize(vec3(-.4,.8,.3))),0.0));
    col+=uTheme*edge*.16;
    if(fPart>.5){
        bool recess=fPart>1.5&&fPart<2.5;
        vec3 base=recess?vec3(.009,.015,.022):fPart>3.5?vec3(.10,.082,.057):vec3(.08,.11,.13);
        vec3 light=normalize(vec3(-.4,.8,.3)),h=normalize(v+light);
        float spec=pow(max(dot(n,h),0.0),recess?10.0:38.0)*detail;
        // Hardware has no neon outlines; its relief reads through reflected light.
        col=base*(.42+.58*max(dot(n,light),0.0))+vec3(.13,.17,.20)*spec;
        col=mix(vec3(.035,.056,.070),col,detail);
    }
    float fog=exp(-max(distance-30,0.0)*.013);
    finalColor=vec4(mix(uTheme*.006,col,fog),1);
}
