#version 330

in vec3 fragPosition;
in vec3 fragNormal;
in vec3 fragLocalPos;
in vec3 fragMaterialPos;
in float fragFade;

out vec4 finalColor;

uniform float uTime;
uniform float virtualPlayerZ;
uniform float uIntensity;
uniform int uRunSeed;
uniform vec3 uPrimaryColor;
uniform vec3 uSecondaryColor;
uniform vec3 uBodyColor;
uniform vec3 uSkyHorizon;
uniform vec3 uViewPosition;
uniform sampler2D uDetailAtlas;
uniform int uDetailEnabled;

float materialHash(ivec2 p) {
    uint h=uint(p.x)*1597334677u^uint(p.y)*3812015801u^uint(uRunSeed);
    h^=h>>16; h*=0x7feb352du; h^=h>>15; h*=0x846ca68bu;
    return float((h^(h>>16))&65535u)/65535.0;
}
vec3 deckMaterial(vec3 normal,float worldZ) {
    vec2 uv=normal.y>0.55?vec2(fragMaterialPos.x,worldZ):
            (abs(normal.x)>0.5?vec2(worldZ,fragMaterialPos.y):fragMaterialPos.xy);
    float aa=max(max(fwidth(uv.x),fwidth(uv.y))*0.7,0.001);
    float fine=1.0-smoothstep(0.065,0.25,aa);
    float micro=1.0-smoothstep(0.018,0.06,aa);
    vec4 detail=uDetailEnabled!=0?texture(uDetailAtlas,uv*0.25):vec4(0.5,0.5,0.5,0);
    vec2 tile=vec2(2.0,4.0),id=floor(uv/tile),f=fract(uv/tile)*tile;
    vec2 edges=min(f,tile-f);
    float d=min(edges.x,edges.y);
    float seam=(1.0-smoothstep(0.024,0.024+aa,d))*fine;
    float bevel=(1.0-smoothstep(0.07,0.07+aa,d))*fine;
    float bolt=(1.0-smoothstep(0.047,0.047+aa,length(edges-0.14)))*fine;
    float screw=(1.0-smoothstep(0.008,0.008+aa,abs(edges.x-edges.y)))*bolt;
    float h=materialHash(ivec2(id));
    float hatchD=length(f-vec2(1.0,2.0));
    float hatch=(1.0-smoothstep(0.62,0.62+aa,hatchD))*step(h,0.17)*fine;
    float hatchRim=(1.0-smoothstep(0.045,0.045+aa,abs(hatchD-0.56)))*hatch;
    float hatchSlot=(1.0-smoothstep(0.034,0.034+aa,abs(f.y-2.0)))*hatch*
                     (1.0-smoothstep(0.24,0.24+aa,abs(f.x-1.0)));
    float caution=(1.0-smoothstep(0.15,0.15+aa,abs(f.x-0.28)))*
                  step(0.45,f.y)*step(f.y,3.55)*step(0.35,h)*step(h,0.46)*fine;
    float stripe=smoothstep(0.45-aa,0.55+aa,fract((f.x+f.y)*2.4));
    float drain=(1.0-smoothstep(0.10,0.10+aa,abs(f.x-1.0)))*
                step(0.28,f.y)*step(f.y,3.72)*step(0.72,h)*fine;
    float grate=0.5+0.5*cos(uv.y*62.8);
    float tread=(1.0-smoothstep(0.022,0.022+aa,abs(fract((uv.x+uv.y)*3.5)-0.5)/3.5))*
                (1.0-step(0.62,h))*micro;
    float grit=materialHash(ivec2(floor(uv*43.0)));
    float scuff=(1.0-smoothstep(0.006,0.006+aa,abs(fract(uv.y*14.0)-0.5)/14.0))*
                step(0.95,materialHash(ivec2(floor(uv*vec2(3.0,14.0)))))*micro;
    float height=-seam*0.03-bevel*0.012-drain*(0.035+grate*0.025)+bolt*0.009+tread*0.004;
    height+=(detail.r-0.5)*0.045*fine;
    height+=hatch*0.012-hatchRim*0.030-hatchSlot*0.022;
    vec3 dx=dFdx(fragPosition),dy=dFdy(fragPosition);
    vec3 rx=cross(dy,normal),ry=cross(normal,dx);
    float det=dot(dx,rx);
    vec3 n=normalize(normal*max(abs(det),1e-9)-sign(det)*(rx*dFdx(height)+ry*dFdy(height)));
    vec3 paint=vec3(0.092,0.105,0.122)*(0.74+h*0.40);
    paint*=1.0-seam*0.70-bevel*0.12+micro*(grit-0.5)*0.09;
    paint=mix(paint,vec3(0.012,0.017,0.022),drain*(0.65+grate*0.30));
    paint=mix(paint,vec3(0.12,0.14,0.16)*(1.0-screw*0.8),bolt);
    paint+=vec3(0.018)*scuff+vec3(0.007)*tread;
    paint*=0.86+detail.g*0.26;
    paint=mix(paint,paint*vec3(0.78,0.65,0.48),smoothstep(0.43,0.70,detail.b)*0.45);
    paint+=vec3(0.025)*smoothstep(0.32,0.86,detail.a)*fine;
    paint=mix(paint,paint*1.25,hatch);
    paint*=1.0-hatchRim*0.64-hatchSlot*0.72;
    paint=mix(paint,mix(vec3(0.018,0.022,0.029),vec3(0.23,0.20,0.105),stripe),caution*0.75);
    vec3 light=normalize(vec3(-0.40,0.84,0.37));
    vec3 viewDir=normalize(uViewPosition-fragPosition);
    float spec=pow(max(dot(n,normalize(light+viewDir)),0.0),28.0+h*32.0);
    return paint*(0.42+max(dot(n,light),0.0)*0.63)+
           vec3(0.26,0.29,0.33)*spec*0.20*(1.0-seam)*(0.6+h*0.4);
}

void main()
{
    vec3 normal = normalize(fragNormal);
    float upward = max(normal.y, 0.0);
    float cliff = 1.0 - upward;

    // Dark matte obsidian/basalt base (rich dark charcoal / black)
    float worldZ = fragPosition.z - virtualPlayerZ;
    vec3 baseColor = uBodyColor * 1.05 + vec3(0.010, 0.014, 0.020);
    
    // Directional shading for cliff sides and floor depth
    vec3 lightDir = normalize(vec3(0.35, 0.85, 0.45));
    float diff = max(dot(normal, lightDir), 0.0);
    baseColor *= (0.65 + 0.35 * diff);

    // World-space grid lines (X and Z) - Top surfaces only
    // 4-meter grid period with screen-space derivative anti-aliasing (fwidth)
    float coordX = abs(fract(fragPosition.x * 0.25) - 0.5) * 4.0;
    float coordZ = abs(fract(worldZ * 0.25) - 0.5) * 4.0;
    float spanX = max(fwidth(coordX) * 1.5, 0.016);
    float spanZ = max(fwidth(coordZ) * 1.5, 0.016);
    float gridX = 1.0 - smoothstep(0.0, spanX, coordX);
    float gridZ = 1.0 - smoothstep(0.0, spanZ, coordZ);
    float grid = max(gridX, gridZ) * upward;

    // Moving data pulses along the grid lines
    float pulse = sin(fragPosition.z * 0.4 - uTime * 3.5) * 0.5 + 0.5;
    pulse *= sin(fragPosition.x * 0.4 + uTime * 1.8) * 0.5 + 0.5;

    // Instanced block boundary wireframes (Top surface tile rims)
    float distToTileRim = 0.5 - max(abs(fragLocalPos.x), abs(fragLocalPos.z));
    float rimSpan = max(fwidth(distToTileRim) * 1.5, 0.012);
    float tileBorder = (1.0 - smoothstep(0.002, 0.002 + rimSpan, distToTileRim)) * upward;

    // Top rim of vertical cliff edges
    float distToCliffTop = 0.5 - fragLocalPos.y;
    float cliffSpan = max(fwidth(distToCliffTop) * 1.5, 0.012);
    float cliffTopRim = (1.0 - smoothstep(0.002, 0.002 + cliffSpan, distToCliffTop)) * cliff;

    // Digital canyon strata and vertical grid continuation along cliff drops
    float cliffTrace = (abs(normal.x) > 0.5 ? gridZ : gridX) * cliff;
    float strataCoord = abs(fract(fragPosition.y * 0.5) - 0.5) * 2.0;
    float strataSpan = max(fwidth(strataCoord) * 1.5, 0.016);
    float cliffStrata = (1.0 - smoothstep(0.0, strataSpan, strataCoord)) * cliff;
    float cliffWire = clamp(cliffTrace * 0.50 + cliffStrata * 0.50, 0.0, 1.0);

    // Wireframe glow: smooth, sub-pixel blended vector lines
    float wireMask = clamp(grid * 0.70 + tileBorder * 0.80 + cliffTopRim * 0.50 + cliffWire * 0.45, 0.0, 1.0);
    float smoothWire = pow(wireMask, 1.4);

    float cliffPulse = sin(fragPosition.y * 0.8 - uTime * 2.5) * 0.5 + 0.5;
    vec3 cliffColor = mix(uPrimaryColor, uSecondaryColor, cliffPulse);
    vec3 gridColor = mix(mix(uSecondaryColor, uPrimaryColor, pulse), cliffColor, cliff * 0.55);
    float distanceFade = 1.0 - smoothstep(45.0, 220.0, -fragPosition.z);
    vec3 glow = gridColor * (wireMask * 0.45 + smoothWire * 0.55) *
                (0.10 + uIntensity * 0.07) * (0.40 + 0.60 * distanceFade);

    vec3 color = deckMaterial(normal,worldZ) + glow + baseColor*0.10;
    float rail = exp(-pow((abs(fragPosition.x) - 12.0) * 3.5, 2.0));
    float dash = smoothstep(0.30, 0.40, fract(worldZ / 8.0));
    color += uPrimaryColor * rail * dash * upward * distanceFade * 0.20;

    // Atmospheric horizon fog blending to deep dark horizon
    vec3 fog = mix(uBodyColor * 0.6, uSkyHorizon, 0.22 + uIntensity * 0.18);
    float aerial = 1.0 - exp(-max(-fragPosition.z - 36.0, 0.0) * 0.0034);
    fog += uPrimaryColor * 0.033;
    color = mix(color, fog, aerial * 0.65);
    color = mix(fog, color, fragFade);

    finalColor = vec4(color, 1.0);
}
