// Same Rx-then-Ry gimbal convention as flight_gimbal_rotate().
uniform vec2 uGimbalLeft, uGimbalRight;
vec3 nozzle_rotate(vec3 p,vec2 a){
    float cx=cos(a.x),sx=sin(a.x),cy=cos(a.y),sy=sin(a.y);
    vec3 q=vec3(p.x,cx*p.y-sx*p.z,sx*p.y+cx*p.z);
    return vec3(cy*q.x+sy*q.z,q.y,-sy*q.x+cy*q.z);
}
void nozzle_pose(inout vec3 p,inout vec3 n,float side){
    vec2 a=side<0?uGimbalLeft:uGimbalRight;
    vec3 pivot=vec3(side*.48,-.05,-1.40);
    p=pivot+nozzle_rotate(p-pivot,a);n=nozzle_rotate(n,a);
}
