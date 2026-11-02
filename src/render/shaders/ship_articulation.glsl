// UV2.y: signed wing (1), winglet (2), trailing flap (3); zero is fixed hull.
// Shared by opaque plating and attached lights, in both camera/reflection passes.
uniform vec2 uSteer;
vec3 ship_rx(vec3 p,float a){float c=cos(a),s=sin(a);return vec3(p.x,c*p.y-s*p.z,s*p.y+c*p.z);}
vec3 ship_ry(vec3 p,float a){float c=cos(a),s=sin(a);return vec3(c*p.x+s*p.z,p.y,-s*p.x+c*p.z);}
vec3 ship_rz(vec3 p,float a){float c=cos(a),s=sin(a);return vec3(c*p.x-s*p.y,s*p.x+c*p.y,p.z);}
void ship_pose(inout vec3 p,inout vec3 n,float part){
    if(abs(part)<.5)return;
    float side=sign(part),kind=abs(part);
    if(kind>1.5&&kind<2.5){
        vec3 pivot=vec3(side*1.01,.10,-1.09);
        float yaw=side*.25*uSteer.x,fold=-.35*uSteer.x+side*.12*uSteer.y;
        p=pivot+ship_rz(ship_ry(p-pivot,yaw),fold);
        n=ship_rz(ship_ry(n,yaw),fold);
    }else if(kind>2.5){
        vec3 pivot=vec3(side*.85,.13,-1.08);
        float flap=-.42*uSteer.y+side*.32*uSteer.x;
        p=pivot+ship_rx(p-pivot,flap);n=ship_rx(n,flap);
    }
    vec3 root=vec3(side*.28,.07,-.20);
    float roll=-.18*uSteer.x+side*.055*uSteer.y,pitch=.10*uSteer.y;
    p=root+ship_rz(ship_rx(p-root,pitch),roll);
    n=ship_rz(ship_rx(n,pitch),roll);
}
