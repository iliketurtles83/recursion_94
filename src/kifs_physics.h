#ifndef KIFS_PHYSICS_H
#define KIFS_PHYSICS_H

#include "raylib.h"
#include <math.h>
#include <stdbool.h>

typedef struct {
    float dist;
    float factor;
} KifsSminResult;

static inline KifsSminResult KifsSmin(float a, float b, float k) {
    float safeK = k > 1e-6f ? k : 1e-6f;
    float h = 0.5f + 0.5f * (b - a) / safeK;
    if (h < 0.0f) h = 0.0f;
    if (h > 1.0f) h = 1.0f;
    float d = (b * (1.0f - h) + a * h) - safeK * h * (1.0f - h);
    KifsSminResult res = { d, h };
    return res;
}

typedef struct {
    Vector3 position;
    float radius;
    Vector3 color;
    float blendRadius;
    Vector3 velocity;
    float mass;
    bool active;
    bool isStatic;
} KifsEntityData;

static inline float KifsSdBox(Vector3 p, Vector3 b) {
    Vector3 q = { fabsf(p.x) - b.x, fabsf(p.y) - b.y, fabsf(p.z) - b.z };
    float ox = fmaxf(q.x, 0.0f);
    float oy = fmaxf(q.y, 0.0f);
    float oz = fmaxf(q.z, 0.0f);
    float outside = sqrtf(ox * ox + oy * oy + oz * oz);
    float inside = fminf(fmaxf(q.x, fmaxf(q.y, q.z)), 0.0f);
    return outside + inside;
}

static inline float KifsSdSphere(Vector3 p, float r) {
    return sqrtf(p.x * p.x + p.y * p.y + p.z * p.z) - r;
}

static inline float KifsMapScene(Vector3 p, int iterations, float scaleFactor, Vector3 offset) {
    Vector3 w = p;
    float scale = 1.0f;
    int iters = iterations > 0 ? iterations : 6;
    float s = scaleFactor > 0.0f ? scaleFactor : 1.85f;

    for (int i = 0; i < iters; ++i) {
        // 1. Fold across planes of symmetry
        w.x = fabsf(w.x) - 1.2f;
        w.y = fabsf(w.y) - 0.8f;
        w.z = fabsf(w.z) - 1.2f;

        if (w.x < w.y) { float tmp = w.x; w.x = w.y; w.y = tmp; }
        if (w.x < w.z) { float tmp = w.x; w.x = w.z; w.z = tmp; }
        if (w.y < w.z) { float tmp = w.y; w.y = w.z; w.z = tmp; }

        // 2. Scale and translate
        w.x = w.x * s - offset.x * (s - 1.0f);
        w.y = w.y * s - offset.y * (s - 1.0f);
        w.z = w.z * s - offset.z * (s - 1.0f);
        scale *= s;
    }

    // Normalize terminal distance to prevent stepping past boundaries
    float len = sqrtf(w.x * w.x + w.y * w.y + w.z * w.z);
    return (len - 0.4f) / scale;
}

static inline float KifsMapEntities(Vector3 p, const KifsEntityData *entities, int entityCount, Vector3 *outColor) {
    if (entityCount <= 0 || !entities) {
        if (outColor) *outColor = (Vector3){ 0.0f, 0.0f, 0.0f };
        return 1e5f;
    }

    float accumDist = 1e5f;
    Vector3 accumColor = { 0.0f, 0.0f, 0.0f };
    int startIndex = -1;

    for (int i = 0; i < entityCount; ++i) {
        if (!entities[i].active) continue;
        float dx = p.x - entities[i].position.x;
        float dy = p.y - entities[i].position.y;
        float dz = p.z - entities[i].position.z;
        accumDist = sqrtf(dx * dx + dy * dy + dz * dz) - entities[i].radius;
        accumColor = entities[i].color;
        startIndex = i;
        break;
    }

    if (startIndex == -1) {
        if (outColor) *outColor = (Vector3){ 0.0f, 0.0f, 0.0f };
        return 1e5f;
    }

    for (int i = startIndex + 1; i < entityCount; ++i) {
        if (!entities[i].active) continue;
        float dx = p.x - entities[i].position.x;
        float dy = p.y - entities[i].position.y;
        float dz = p.z - entities[i].position.z;
        float d = sqrtf(dx * dx + dy * dy + dz * dz) - entities[i].radius;
        float k = entities[i].blendRadius > 1e-4f ? entities[i].blendRadius : 1e-4f;
        KifsSminResult res = KifsSmin(accumDist, d, k);
        accumColor = (Vector3){
            entities[i].color.x * (1.0f - res.factor) + accumColor.x * res.factor,
            entities[i].color.y * (1.0f - res.factor) + accumColor.y * res.factor,
            entities[i].color.z * (1.0f - res.factor) + accumColor.z * res.factor
        };
        accumDist = res.dist;
    }

    if (outColor) *outColor = accumColor;
    return accumDist;
}

static inline float KifsMapSceneWithEntities(Vector3 p, int iterations, float scaleFactor, Vector3 offset,
                                            const KifsEntityData *entities, int entityCount) {
    float worldDist = KifsMapScene(p, iterations, scaleFactor, offset);
    if (entityCount <= 0 || !entities) return worldDist;
    float entDist = KifsMapEntities(p, entities, entityCount, NULL);
    return worldDist < entDist ? worldDist : entDist;
}

static inline Vector3 KifsCalcNormal(Vector3 p, int iterations, float scaleFactor, Vector3 offset) {
    const float eps = 0.0005f;
    Vector3 k1 = {  eps, -eps, -eps };
    Vector3 k2 = { -eps, -eps,  eps };
    Vector3 k3 = { -eps,  eps, -eps };
    Vector3 k4 = {  eps,  eps,  eps };

    float d1 = KifsMapScene((Vector3){ p.x + k1.x, p.y + k1.y, p.z + k1.z }, iterations, scaleFactor, offset);
    float d2 = KifsMapScene((Vector3){ p.x + k2.x, p.y + k2.y, p.z + k2.z }, iterations, scaleFactor, offset);
    float d3 = KifsMapScene((Vector3){ p.x + k3.x, p.y + k3.y, p.z + k3.z }, iterations, scaleFactor, offset);
    float d4 = KifsMapScene((Vector3){ p.x + k4.x, p.y + k4.y, p.z + k4.z }, iterations, scaleFactor, offset);

    Vector3 n = {
        k1.x * d1 + k2.x * d2 + k3.x * d3 + k4.x * d4,
        k1.y * d1 + k2.y * d2 + k3.y * d3 + k4.y * d4,
        k1.z * d1 + k2.z * d2 + k3.z * d3 + k4.z * d4
    };
    float len = sqrtf(n.x * n.x + n.y * n.y + n.z * n.z);
    if (len < 1e-7f) return (Vector3){ 0.0f, 1.0f, 0.0f };
    return (Vector3){ n.x / len, n.y / len, n.z / len };
}

static inline bool KifsResolveCollision(KifsEntityData *a, KifsEntityData *b, float restitution) {
    if (!a || !b || !a->active || !b->active) return false;

    float dx = b->position.x - a->position.x;
    float dy = b->position.y - a->position.y;
    float dz = b->position.z - a->position.z;
    float dist = sqrtf(dx * dx + dy * dy + dz * dz);
    float sumRadii = a->radius + b->radius;
    float d = dist - sumRadii;

    if (d < 0.0f) {
        float penetration = -d;
        Vector3 normal;
        if (dist > 1e-6f) {
            normal = (Vector3){ dx / dist, dy / dist, dz / dist };
        } else {
            normal = (Vector3){ 0.0f, 1.0f, 0.0f };
        }

        float halfPen = penetration * 0.5f;
        if (!a->isStatic && !b->isStatic) {
            a->position.x -= normal.x * halfPen;
            a->position.y -= normal.y * halfPen;
            a->position.z -= normal.z * halfPen;
            b->position.x += normal.x * halfPen;
            b->position.y += normal.y * halfPen;
            b->position.z += normal.z * halfPen;
        } else if (!a->isStatic) {
            a->position.x -= normal.x * penetration;
            a->position.y -= normal.y * penetration;
            a->position.z -= normal.z * penetration;
        } else if (!b->isStatic) {
            b->position.x += normal.x * penetration;
            b->position.y += normal.y * penetration;
            b->position.z += normal.z * penetration;
        }

        float rvx = b->velocity.x - a->velocity.x;
        float rvy = b->velocity.y - a->velocity.y;
        float rvz = b->velocity.z - a->velocity.z;
        float vn = rvx * normal.x + rvy * normal.y + rvz * normal.z;

        if (vn < 0.0f) {
            float mA = a->mass > 0.0f ? a->mass : (a->radius * a->radius * a->radius);
            float mB = b->mass > 0.0f ? b->mass : (b->radius * b->radius * b->radius);

            if (!a->isStatic && !b->isStatic) {
                float invMA = 1.0f / mA;
                float invMB = 1.0f / mB;
                float j = -(1.0f + restitution) * vn / (invMA + invMB);
                a->velocity.x -= normal.x * (j * invMA);
                a->velocity.y -= normal.y * (j * invMA);
                a->velocity.z -= normal.z * (j * invMA);
                b->velocity.x += normal.x * (j * invMB);
                b->velocity.y += normal.y * (j * invMB);
                b->velocity.z += normal.z * (j * invMB);
            }
        }
        return true;
    }
    return false;
}

#endif // KIFS_PHYSICS_H
