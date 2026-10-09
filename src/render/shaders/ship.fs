#version 330
in vec3 fNormal, fView, fColor, fLocal, fLocalNormal, fWorld;
in float fMaterial;
out vec4 finalColor;
uniform sampler2D texture0;
uniform int uHasSkin;
uniform vec3 uKeyDir,uFillDir,uRimColor;
uniform float uTime,uBoost,uDamage;
const float PI=3.14159265;
vec3 fresnel(float c,vec3 f0){return f0+(1.0-f0)*pow(1.0-c,5.0);}
vec3 sky(vec3 r){
    vec3 c=mix(vec3(.015,.025,.04),vec3(.15,.25,.36),smoothstep(-.6,.7,r.y));
    float band=pow(max(0.0,1.0-abs(r.y-.2)*2.0),8.0);
    return c+vec3(.035,.20,.31)*band;
}
vec3 material_sample(){
    if(uHasSkin==0)return vec3(.7);
    vec3 w=pow(abs(normalize(fLocalNormal)),vec3(8));w/=max(w.x+w.y+w.z,.001);
    vec3 x=texture(texture0,fLocal.yz*.62+vec2(.13,.29)).rgb;
    vec3 y=texture(texture0,fLocal.xz*.62+vec2(.17,.07)).rgb;
    vec3 z=texture(texture0,fLocal.xy*.62+vec2(.41,.23)).rgb;
    return pow(x*w.x+y*w.y+z*w.z,vec3(2.2));
}
vec3 lighting(vec3 n,vec3 v,vec3 l,vec3 base,vec3 f0,float metal,float rough){
    vec3 h=normalize(v+l);
    float nv=max(dot(n,v),.001),nl=max(dot(n,l),.001),nh=max(dot(n,h),0),vh=max(dot(v,h),0);
    float a=rough*rough,a2=a*a,d=nh*nh*(a2-1)+1;
    float distribution=a2/max(PI*d*d,.0001),k=(rough+1)*(rough+1)/8;
    float visibility=(nv/(nv*(1-k)+k))*(nl/(nl*(1-k)+k));
    vec3 f=fresnel(vh,f0),spec=distribution*visibility*f/max(4*nv*nl,.001);
    return ((1-f)*(1-metal)*base/PI+spec)*nl;
}
float scar(vec2 center,vec2 radius){
    vec2 p=(fLocal.xz-center)/radius;
    float grain=sin(p.x*17+p.y*13)*sin(p.y*21-p.x*8)*.09;
    return 1-smoothstep(.55,1.1,length(p)+grain);
}
float crack(vec2 p,vec2 a,vec2 b){
    vec2 d=b-a;float t=clamp(dot(p-a,d)/dot(d,d),0,1);
    float dist=length(p-a-d*t),aa=max(fwidth(dist),.0008);
    return 1-smoothstep(.0012,.0024+aa,dist);
}
void main(){
    vec3 n=normalize(fNormal),v=normalize(fView);
    if(dot(n,v)<0)n=-n;
    bool glass=fMaterial>.5&&fMaterial<1.5,carbon=fMaterial>1.5&&fMaterial<2.5;
    bool heatShield=fMaterial>3.5;
    float metal=heatShield?.92:fMaterial>2.5?.83:carbon?.10:.16;
    vec3 tex=material_sample(),base=fColor*tex;
    float shade=dot(tex,vec3(.2126,.7152,.0722));
    float rough=glass?.085:carbon?.62:heatShield?.22:fMaterial>2.5?.29:.43;
    rough+=glass?0:(1-shade)*.14;
    // Small relief from panel joints and wear; mipmaps stabilize distant detail.
    if(!glass){
        float height=shade*.0018;
        vec3 px=dFdx(fWorld),py=dFdy(fWorld),r1=cross(py,n),r2=cross(n,px);
        float det=dot(px,r1);
        vec3 gradient=sign(det)*(dFdx(height)*r1+dFdy(height)*r2);
        n=normalize(max(abs(det),.0000001)*n-gradient);
    } else base=fColor*.48;
    float stage1=clamp(uDamage*2,0,1),stage2=clamp(uDamage*2-1,0,1);
    float scorch=max(scar(vec2(-.83,-.60),vec2(.50,.58))*stage1,
        scar(vec2(.94,-.85),vec2(.48,.48))*stage2);
    scorch=max(scorch,scar(vec2(.04,.80),vec2(.22,.50))*(stage1*.35+stage2*.65));
    scorch=max(scorch,scar(vec2(-.05,-.67),vec2(.26,.48))*(stage1*.75+stage2*.25));
    scorch*=smoothstep(-.04,.12,fLocal.y);
    float fracture=0;
    if(glass){
        vec2 p=fLocal.xz,impact=vec2(-.07,.28);
        fracture=max(crack(p,impact,vec2(-.19,.61)),crack(p,impact,vec2(.19,.08)));
        fracture=max(fracture,crack(p,vec2(-.13,.46),vec2(.03,.71)));
        fracture=max(fracture,crack(p,impact,vec2(-.14,-.15)));
        fracture=max(fracture,crack(p,vec2(.05,.18),vec2(.17,.43)))*stage1;
        fracture=max(fracture,max(crack(p,vec2(-.09,-.72),vec2(.06,-.36)),crack(p,vec2(-.04,-.60),vec2(.13,-.70)))*stage1);
        fracture=max(fracture,max(crack(p,vec2(.12,.60),vec2(-.19,-.08)),crack(p,vec2(.12,.60),vec2(-.05,.81)))*stage2);
        base=mix(base,vec3(.25,.31,.34),fracture*.75);rough+=fracture*.5+uDamage*.12;
    }else{
        float chipped=1-smoothstep(.10,.22,shade);
        base=mix(base,vec3(.018,.014,.011),scorch*.92);
        base=mix(base,vec3(.30,.29,.25),scorch*chipped*.55);
        rough=mix(rough,.79,scorch);metal=mix(metal,.35,scorch);
    }
    if(heatShield){
        float temper=.5+.5*sin(fLocal.z*24.0+atan(fLocal.y+.05,abs(fLocal.x)-.48)*2.0);
        base*=mix(vec3(1.0),mix(vec3(.78,.64,.51),vec3(.46,.55,.68),temper),.35);
    }
    float variance=dot(dFdx(n),dFdx(n))+dot(dFdy(n),dFdy(n));
    rough=clamp(sqrt(rough*rough+variance*.12),.075,.85);
    vec3 f0=mix(vec3(.04),base,metal);
    if(glass)f0=vec3(.08);
    vec3 col=lighting(n,v,normalize(uKeyDir),base,f0,metal,rough)*vec3(3.8,3.65,3.35);
    col+=lighting(n,v,normalize(uFillDir),base,f0,metal,rough)*vec3(.45,.65,.90);
    vec3 reflection=sky(reflect(-v,n)),f=fresnel(max(dot(n,v),0),f0);
    col+=base*vec3(.20,.28,.35)*(1-metal*.75)+reflection*f*(glass?1.8:1.1);
    if(glass)col+=vec3(.008,.022,.026);
    if(!glass)col*=1-.65*scorch;
    col+=vec3(.34,.42,.44)*fracture;
    // A few dim hot tears in critically damaged plating, without broad bloom.
    float tear=1-smoothstep(.001,.005+fwidth(fLocal.z),abs(fLocal.z+.67+.08*sin(fLocal.x*13)));
    col+=vec3(.40,.055,.004)*tear*scorch*stage2;
    col+=uRimColor*pow(1-max(dot(n,v),0),4.0)*.035;
    // Exhaust illuminates the aft hardware locally instead of tinting the hull.
    float left=exp(-length(fLocal-vec3(-.48,-.05,-1.78))*7.0),right=exp(-length(fLocal-vec3(.48,-.05,-1.78))*7.0);
    col+=vec3(.025,.14,.40)*(left+right)*(.25+1.8*uBoost);
    if(heatShield)col+=vec3(.055,.012,.002)*uBoost;
    finalColor=vec4(col,1);
}
