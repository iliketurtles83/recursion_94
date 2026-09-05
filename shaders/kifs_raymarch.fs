#version 330

in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;

uniform vec2 u_resolution;
uniform float u_time;
uniform vec3 u_camPos;
uniform vec3 u_camTarget;
uniform int u_iterations;
uniform float u_scale;
uniform vec3 u_offset;
uniform float u_travelZ;

// Entity Uniform Buffer & Representation (up to 16 dynamic entities)
struct EntityData {
    vec3 position;
    float radius;
    vec3 color;
    float blendRadius; // 'k' parameter
};

uniform int u_entityCount;
uniform EntityData u_entities[16];

// Polynomial Smooth Minimum
struct SminResult {
    float dist;
    float factor;
};

SminResult smin(float a, float b, float k) {
    float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
    float d = mix(b, a, h) - k * h * (1.0 - h);
    return SminResult(d, h);
}

float sdBox(vec3 p, vec3 b) {
    vec3 q = abs(p) - b;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
}

float sdSphere(vec3 p, float r) {
    return length(p) - r;
}

float mapScene(vec3 p) {
    vec3 w = p;
    w.z -= u_travelZ;
    
    vec3 cell = w;
    // Mirror X to create left and right walls
    cell.x = abs(cell.x) - 20.0; 
    
    // Repeat Z for infinite tunnel
    float repeatZ = 24.0;
    cell.z = mod(cell.z, repeatZ) - repeatZ * 0.5;
    
    // Mirror Y to create floor and ceiling
    cell.y = abs(cell.y - 3.5) - 11.5;
    
    // Scale for KIFS
    float worldScale = 12.0;
    vec3 w_kifs = cell / worldScale;
    
    float scale = 1.0;
    int iters = (u_iterations > 0) ? u_iterations : 5;

    for (int i = 0; i < iters; ++i) {
        w_kifs = abs(w_kifs);
        if (w_kifs.x < w_kifs.y) w_kifs.xy = w_kifs.yx;
        if (w_kifs.x < w_kifs.z) w_kifs.xz = w_kifs.zx;
        if (w_kifs.y < w_kifs.z) w_kifs.yz = w_kifs.zy;

        w_kifs = w_kifs * 2.8 - vec3(1.8);
        scale *= 2.8;
    }
    
    float fractalDist = sdBox(w_kifs, vec3(1.0)) / scale * worldScale;
    
    // Bound the fractal to prevent mod boundary planes
    float boxDist = sdBox(cell, vec3(11.5, 11.5, 11.5));
    fractalDist = max(fractalDist, boxDist);
    
    return fractalDist * 0.7; // Safety factor for KIFS overstepping
}

float kifsFold(vec3 p) {
    return mapScene(p);
}

// Composite Entity SDF Mapping
vec4 mapEntities(vec3 p) {
    if (u_entityCount <= 0) {
        return vec4(1e5, 0.0, 0.0, 0.0);
    }

    float accumDist = length(p - u_entities[0].position) - u_entities[0].radius;
    vec3 accumColor = u_entities[0].color;

    for (int i = 1; i < 16; ++i) {
        if (i >= u_entityCount) break;
        EntityData ent = u_entities[i];
        float d = length(p - ent.position) - ent.radius;
        float k = max(ent.blendRadius, 0.0001);
        SminResult res = smin(accumDist, d, k);
        accumColor = mix(ent.color, accumColor, res.factor);
        accumDist = res.dist;
    }

    return vec4(accumDist, accumColor);
}

// Combined world + entity distance evaluator
float mapSceneWithEntities(vec3 p) {
    float worldDist = mapScene(p);
    if (u_entityCount <= 0) return worldDist;
    vec4 ent = mapEntities(p);
    return min(worldDist, ent.x);
}

// Combined distance and surface color evaluator
vec4 mapSceneWithEntitiesColor(vec3 p) {
    float worldDist = mapScene(p);
    vec3 worldColor = mix(
        vec3(0.12, 0.74, 0.92),
        vec3(0.92, 0.28, 0.62),
        0.5 + 0.5 * sin(p.z * 0.18 + u_time * 0.35)
    );

    if (u_entityCount <= 0) {
        return vec4(worldDist, worldColor);
    }

    vec4 ent = mapEntities(p);
    if (ent.x < worldDist) {
        return ent;
    }
    return vec4(worldDist, worldColor);
}

vec3 calcNormal(vec3 p) {
    const vec2 k = vec2(1.0, -1.0) * 0.0005;
    return normalize(
        k.xyy * mapSceneWithEntities(p + k.xyy) +
        k.yyx * mapSceneWithEntities(p + k.yyx) +
        k.yxy * mapSceneWithEntities(p + k.yxy) +
        k.xxx * mapSceneWithEntities(p + k.xxx)
    );
}

float raymarch(vec3 ro, vec3 rd, float maxDist, int maxSteps) {
    float t = 0.0;
    float candidate_error = 0.0;
    const float omega = 1.2;

    for (int i = 0; i < maxSteps; ++i) {
        vec3 p = ro + rd * t;
        float d = mapSceneWithEntities(p);

        if (omega > 1.0 && (d + candidate_error) < candidate_error) {
            t -= candidate_error;
            d = mapSceneWithEntities(ro + rd * t);
            t += d;
            candidate_error = 0.0;
        } else {
            candidate_error = d * (omega - 1.0);
            t += d * omega;
        }

        float eps = max(0.0002, 0.001 * t);
        if (d < eps) {
            return t;
        }
        if (t >= maxDist) {
            break;
        }
    }
    return -1.0;
}

float raymarchWithSteps(vec3 ro, vec3 rd, float maxDist, int maxSteps, out int stepsOut) {
    float t = 0.0;
    float candidate_error = 0.0;
    const float omega = 1.2;
    stepsOut = 0;

    for (int i = 0; i < maxSteps; ++i) {
        stepsOut = i + 1;
        vec3 p = ro + rd * t;
        float d = mapSceneWithEntities(p);

        if (omega > 1.0 && (d + candidate_error) < candidate_error) {
            t -= candidate_error;
            d = mapSceneWithEntities(ro + rd * t);
            t += d;
            candidate_error = 0.0;
        } else {
            candidate_error = d * (omega - 1.0);
            t += d * omega;
        }

        float eps = max(0.0002, 0.001 * t);
        if (d < eps) {
            return t;
        }
        if (t >= maxDist) {
            break;
        }
    }
    return -1.0;
}

void main() {
    vec2 ndc = fragTexCoord * 2.0 - 1.0;
    vec3 ro = u_camPos;
    
    vec3 forward = normalize(u_camTarget - ro);
    vec3 right = normalize(cross(forward, vec3(0.0, 1.0, 0.0)));
    vec3 up = cross(right, forward);
    
    float aspect = u_resolution.x / max(u_resolution.y, 1.0);
    // ndc.y is flipped in Raylib
    vec3 rd = normalize(forward * 1.5 + right * ndc.x * aspect + up * ndc.y);

    const int maxSteps = 80;
    const float maxDist = 90.0;
    int steps = 0;
    float t = raymarchWithSteps(ro, rd, maxDist, maxSteps, steps);

    if (t >= 0.0) {
        vec3 hitPos = ro + rd * t;
        vec3 normal = calcNormal(hitPos);

        vec3 keyLightDir = normalize(vec3(0.577, 0.78, -0.45));
        float diff = max(dot(normal, keyLightDir), 0.0);

        float ao = clamp(float(steps) / float(maxSteps), 0.0, 1.0);
        float ambientLight = 1.0 - ao * 0.82;

        vec4 sceneColorData = mapSceneWithEntitiesColor(hitPos);
        vec3 baseColor = sceneColorData.yzw;
        vec3 litColor = baseColor * (diff * 0.85 + 0.15) * ambientLight;

        vec3 viewDir = normalize(ro - hitPos);
        vec3 halfDir = normalize(keyLightDir + viewDir);
        float spec = pow(max(dot(normal, halfDir), 0.0), 16.0);
        litColor += vec3(0.8, 0.9, 1.0) * spec * 0.25 * ambientLight;

        float fogDensity = 0.022;
        float fog = 1.0 - exp(-t * fogDensity);
        vec3 fogColor = vec3(0.012, 0.022, 0.055);
        vec3 finalColorRgb = mix(litColor, fogColor, clamp(fog, 0.0, 1.0));

        finalColor = vec4(finalColorRgb, 1.0) * fragColor;
    } else {
        finalColor = vec4(0.0, 0.0, 0.0, 0.0);
    }
}
