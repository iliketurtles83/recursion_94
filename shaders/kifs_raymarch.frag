#version 300 es
precision highp float;
precision highp int;

// Uniforms
uniform vec2 u_resolution;      // Viewport dimensions in pixels.
uniform float u_time;           // Elapsed application time in seconds.
uniform vec3 u_camPos;          // Camera position in world space.
uniform mat4 u_invViewProj;     // Inverse view-projection matrix to reconstruct world-space ray trajectories.
uniform int u_iterations;       // Recursive fold count (default: 5 to 7).
uniform float u_scale;          // Dilation factor per iteration (e.g., 1.85 to 2.0).
uniform vec3 u_offset;          // Spatial translation vector per fold (e.g., vec3(1.0, 0.5, 1.0)).

// Entity Uniform Buffer & Representation (up to 16 dynamic entities)
struct EntityData {
    vec3 position;
    float radius;
    vec3 color;
    float blendRadius; // 'k' parameter
};

uniform int u_entityCount;
uniform EntityData u_entities[16];

in vec2 v_uv;
out vec4 fragColor;

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

// Primitive SDF Evaluators
float sdBox(vec3 p, vec3 b) {
    vec3 q = abs(p) - b;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
}

float sdSphere(vec3 p, float r) {
    return length(p) - r;
}

// Kaleidoscopic Iterated Function System (KIFS) scene map
float mapScene(vec3 p) {
    vec3 w = p;
    float scale = 1.0;
    int iters = (u_iterations > 0) ? u_iterations : 6;
    float s = (u_scale > 0.0) ? u_scale : 1.85;
    vec3 off = (length(u_offset) > 0.0) ? u_offset : vec3(1.0, 0.5, 1.0);

    for (int i = 0; i < iters; ++i) {
        // 1. Fold across planes of symmetry
        w = abs(w) - vec3(1.2, 0.8, 1.2);
        if (w.x < w.y) w.xy = w.yx;
        if (w.x < w.z) w.xz = w.zx;
        if (w.y < w.z) w.yz = w.zy;

        // 2. Scale and translate
        w = w * s - off * (s - 1.0);
        scale *= s;
    }
    // Normalize terminal distance to prevent stepping past boundaries
    return (length(w) - 0.4) / scale;
}

// Function alias for KIFS folding evaluator
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

// 4-tap tetrahedral gradient estimator evaluating the combined isosurface
vec3 calcNormal(vec3 p) {
    const vec2 k = vec2(1.0, -1.0) * 0.0005;
    return normalize(
        k.xyy * mapSceneWithEntities(p + k.xyy) +
        k.yyx * mapSceneWithEntities(p + k.yyx) +
        k.yxy * mapSceneWithEntities(p + k.yxy) +
        k.xxx * mapSceneWithEntities(p + k.xxx)
    );
}

// Raymarching with over-relaxation (omega ~ 1.2) and dynamic epsilon scaling
float raymarch(vec3 ro, vec3 rd, float maxDist, int maxSteps) {
    float t = 0.0;
    float candidate_error = 0.0;
    const float omega = 1.2;

    for (int i = 0; i < maxSteps; ++i) {
        vec3 p = ro + rd * t;
        float d = mapSceneWithEntities(p);

        // Over-relaxation to accelerate ray convergence across distant fractal cavities
        if (omega > 1.0 && (d + candidate_error) < candidate_error) {
            t -= candidate_error;
            d = mapSceneWithEntities(ro + rd * t);
            t += d;
            candidate_error = 0.0;
        } else {
            candidate_error = d * (omega - 1.0);
            t += d * omega;
        }

        // Dynamic epsilon scaling: epsilon(t) = 0.001 * t
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

// Raymarching variant collecting step count for ambient occlusion estimation
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
    // Reconstruct world-space ray trajectories from inverse view-projection matrix
    vec2 fragCoord = gl_FragCoord.xy;
    vec2 uv = fragCoord / u_resolution;
    vec2 ndc = uv * 2.0 - 1.0;

    vec4 pNearClip = u_invViewProj * vec4(ndc, -1.0, 1.0);
    vec4 pFarClip  = u_invViewProj * vec4(ndc,  1.0, 1.0);
    vec3 pNear = pNearClip.xyz / pNearClip.w;
    vec3 pFar  = pFarClip.xyz  / pFarClip.w;

    vec3 ro = u_camPos;
    vec3 rd = normalize(pFar - pNear);

    // Fallback if matrix is identity/uninitialized
    if (length(rd) < 0.001 || isnan(rd.x)) {
        float aspect = u_resolution.x / max(u_resolution.y, 1.0);
        rd = normalize(vec3(ndc.x * aspect, ndc.y, -1.5));
    }

    const int maxSteps = 80;
    const float maxDist = 90.0;
    int steps = 0;
    float t = raymarchWithSteps(ro, rd, maxDist, maxSteps, steps);

    if (t >= 0.0) {
        vec3 hitPos = ro + rd * t;
        vec3 normal = calcNormal(hitPos);

        // Directional key light
        vec3 keyLightDir = normalize(vec3(0.577, 0.78, -0.45));
        float diff = max(dot(normal, keyLightDir), 0.0);

        // Ambient occlusion estimated from step counts
        float ao = clamp(float(steps) / float(maxSteps), 0.0, 1.0);
        float ambientLight = 1.0 - ao * 0.82;

        // Isosurface color evaluation (entity or world)
        vec4 sceneColorData = mapSceneWithEntitiesColor(hitPos);
        vec3 baseColor = sceneColorData.yzw;
        vec3 litColor = baseColor * (diff * 0.85 + 0.15) * ambientLight;

        // Subtle specular highlight on smooth normals
        vec3 viewDir = normalize(ro - hitPos);
        vec3 halfDir = normalize(keyLightDir + viewDir);
        float spec = pow(max(dot(normal, halfDir), 0.0), 16.0);
        litColor += vec3(0.8, 0.9, 1.0) * spec * 0.25 * ambientLight;

        // Exponential depth fog
        float fogDensity = 0.022;
        float fog = 1.0 - exp(-t * fogDensity);
        vec3 fogColor = vec3(0.012, 0.022, 0.055);
        vec3 finalColor = mix(litColor, fogColor, clamp(fog, 0.0, 1.0));

        fragColor = vec4(finalColor, 1.0);
    } else {
        // On miss (t < 0.0): output transparent alpha or backdrop color placeholder
        fragColor = vec4(0.0, 0.0, 0.0, 0.0);
    }
}
