#version 330
in vec3 fNormal, fView, fColor, fLocal, fLocalNormal, fWorld;
in float fMaterial;
out vec4 finalColor;
uniform sampler2D texture0;
uniform int uHasSkin;
uniform vec3 uKeyDir,uFillDir,uRimColor;
uniform float uTime,uBoost;
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
void main(){
    vec3 n=normalize(fNormal),v=normalize(fView);
    if(dot(n,v)<0)n=-n;
    bool glass=fMaterial>.5&&fMaterial<1.5,carbon=fMaterial>1.5&&fMaterial<2.5;
    float metal=fMaterial>2.5?.83:carbon?.10:.16;
    vec3 tex=material_sample(),base=fColor*tex;
    float shade=dot(tex,vec3(.2126,.7152,.0722));
    float rough=glass?.085:carbon?.62:fMaterial>2.5?.29:.43;
    rough+=glass?0:(1-shade)*.14;
    // Small relief from panel joints and wear; mipmaps stabilize distant detail.
    if(!glass){
        float height=shade*.0018;
        vec3 px=dFdx(fWorld),py=dFdy(fWorld),r1=cross(py,n),r2=cross(n,px);
        float det=dot(px,r1);
        vec3 gradient=sign(det)*(dFdx(height)*r1+dFdy(height)*r2);
        n=normalize(max(abs(det),.0000001)*n-gradient);
    } else base=fColor*.48;
    float variance=dot(dFdx(n),dFdx(n))+dot(dFdy(n),dFdy(n));
    rough=clamp(sqrt(rough*rough+variance*.12),.075,.85);
    vec3 f0=mix(vec3(.04),base,metal);
    if(glass)f0=vec3(.08);
    vec3 col=lighting(n,v,normalize(uKeyDir),base,f0,metal,rough)*vec3(3.8,3.65,3.35);
    col+=lighting(n,v,normalize(uFillDir),base,f0,metal,rough)*vec3(.45,.65,.90);
    vec3 reflection=sky(reflect(-v,n)),f=fresnel(max(dot(n,v),0),f0);
    col+=base*vec3(.20,.28,.35)*(1-metal*.75)+reflection*f*(glass?1.8:1.1);
    if(glass)col+=vec3(.008,.022,.026);
    col+=uRimColor*pow(1-max(dot(n,v),0),4.0)*.035;
    col+=vec3(.001,.005,.008)*uBoost;
    finalColor=vec4(col,1);
}
