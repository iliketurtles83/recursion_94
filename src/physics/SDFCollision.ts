/**
 * SDFCollision.ts
 *
 * CPU-side analytical distance field evaluation, surface normal computation,
 * and exact soft-body collision resolution without triangle mesh colliders.
 * Mirrors the GPU-side polynomial smooth minimum (smin) pipeline.
 */

export interface Vector3Like {
    x: number;
    y: number;
    z: number;
}

export class Vector3 implements Vector3Like {
    public x: number;
    public y: number;
    public z: number;

    constructor(x: number = 0, y: number = 0, z: number = 0) {
        this.x = x;
        this.y = y;
        this.z = z;
    }

    public static from(v: Vector3Like): Vector3 {
        return new Vector3(v.x, v.y, v.z);
    }

    public clone(): Vector3 {
        return new Vector3(this.x, this.y, this.z);
    }

    public copy(v: Vector3Like): this {
        this.x = v.x;
        this.y = v.y;
        this.z = v.z;
        return this;
    }

    public set(x: number, y: number, z: number): this {
        this.x = x;
        this.y = y;
        this.z = z;
        return this;
    }

    public add(v: Vector3Like): Vector3 {
        return new Vector3(this.x + v.x, this.y + v.y, this.z + v.z);
    }

    public sub(v: Vector3Like): Vector3 {
        return new Vector3(this.x - v.x, this.y - v.y, this.z - v.z);
    }

    public scale(s: number): Vector3 {
        return new Vector3(this.x * s, this.y * s, this.z * s);
    }

    public dot(v: Vector3Like): number {
        return this.x * v.x + this.y * v.y + this.z * v.z;
    }

    public lengthSq(): number {
        return this.x * this.x + this.y * this.y + this.z * this.z;
    }

    public length(): number {
        return Math.hypot(this.x, this.y, this.z);
    }

    public distanceTo(v: Vector3Like): number {
        return Math.hypot(this.x - v.x, this.y - v.y, this.z - v.z);
    }

    public normalize(): Vector3 {
        const len = Math.hypot(this.x, this.y, this.z);
        if (len < 1e-7) {
            return new Vector3(0, 1, 0);
        }
        return new Vector3(this.x / len, this.y / len, this.z / len);
    }
}

/**
 * Result of polynomial smooth minimum operation
 */
export interface SminResult {
    dist: number;
    factor: number;
}

/**
 * Analytical polynomial smooth minimum matching GLSL:
 * float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
 * float d = mix(b, a, h) - k * h * (1.0 - h);
 * return SminResult(d, h);
 */
export function smin(a: number, b: number, k: number): SminResult {
    const safeK = Math.max(k, 1e-6);
    const h = Math.min(Math.max(0.5 + 0.5 * (b - a) / safeK, 0.0), 1.0);
    const d = (b * (1.0 - h) + a * h) - safeK * h * (1.0 - h);
    return { dist: d, factor: h };
}

/**
 * Soft-body gameplay entity representation
 */
export interface SoftBodyEntity {
    id: number;
    type?: string;
    position: Vector3;
    velocity: Vector3;
    radius: number;
    color: [number, number, number];
    blendRadius: number; // 'k' parameter
    mass?: number;
    active: boolean;
    isBonding?: boolean;
    bondedEntityIds?: number[];
    isStatic?: boolean;
}

// Global registry for convenience when using standalone functional calls
let activeRegistry: SoftBodyEntity[] = [];

export function setActiveEntityRegistry(entities: SoftBodyEntity[]): void {
    activeRegistry = entities;
}

export function getActiveEntityRegistry(): SoftBodyEntity[] {
    return activeRegistry;
}

/**
 * Analytical distance field query against all active entity spheres using CPU smin(a, b, k).
 * Mirrors mapEntities() in shaders.
 */
export function queryEntityDistance(
    point: Vector3,
    excludeIndex: number = -1,
    entities?: SoftBodyEntity[]
): { distance: number; hitEntityIndex: number } {
    const list = entities || activeRegistry;
    let accumDist = 1e5;
    let hitEntityIndex = -1;

    let startIndex = -1;
    for (let i = 0; i < list.length; ++i) {
        if (i === excludeIndex || !list[i].active) continue;
        const ent = list[i];
        accumDist = point.distanceTo(ent.position) - ent.radius;
        hitEntityIndex = i;
        startIndex = i;
        break;
    }

    if (startIndex === -1) {
        return { distance: 1e5, hitEntityIndex: -1 };
    }

    for (let i = startIndex + 1; i < list.length; ++i) {
        if (i === excludeIndex || !list[i].active) continue;
        const ent = list[i];
        const d = point.distanceTo(ent.position) - ent.radius;
        const k = Math.max(ent.blendRadius, 1e-4);
        const res = smin(accumDist, d, k);
        accumDist = res.dist;
        if (res.factor < 0.5) {
            hitEntityIndex = i;
        }
    }

    return { distance: accumDist, hitEntityIndex };
}

/**
 * Alias for queryEntityDistance to match queryEntitySDF naming
 */
export function queryEntitySDF(
    point: Vector3,
    excludeIndex: number = -1,
    entities?: SoftBodyEntity[]
): { distance: number; hitEntityIndex: number } {
    return queryEntityDistance(point, excludeIndex, entities);
}

/**
 * Computes analytical contact surface normal using 6-point central differences.
 * Normal points along the gradient of increasing distance (away from the surface interior).
 */
export function calculateContactNormal(
    p: Vector3,
    eps: number = 0.001,
    excludeIndex: number = -1,
    entities?: SoftBodyEntity[]
): Vector3 {
    const dx = queryEntityDistance(new Vector3(p.x + eps, p.y, p.z), excludeIndex, entities).distance -
               queryEntityDistance(new Vector3(p.x - eps, p.y, p.z), excludeIndex, entities).distance;
    const dy = queryEntityDistance(new Vector3(p.x, p.y + eps, p.z), excludeIndex, entities).distance -
               queryEntityDistance(new Vector3(p.x - eps, p.y, p.z), excludeIndex, entities).distance;
    const dz = queryEntityDistance(new Vector3(p.x, p.y, p.z + eps), excludeIndex, entities).distance -
               queryEntityDistance(new Vector3(p.x - eps, p.y, p.z - eps), excludeIndex, entities).distance;
    return new Vector3(dx, dy, dz).normalize();
}

/**
 * Alias for calculateContactNormal to match calculateSDFNormal naming
 */
export function calculateSDFNormal(
    point: Vector3,
    eps: number = 0.001,
    excludeIndex: number = -1,
    entities?: SoftBodyEntity[]
): Vector3 {
    return calculateContactNormal(point, eps, excludeIndex, entities);
}

/**
 * Computes surface normal using 4-tap tetrahedral gradient estimator.
 */
export function calculateTetrahedralNormal(
    p: Vector3,
    eps: number = 0.001,
    excludeIndex: number = -1,
    entities?: SoftBodyEntity[]
): Vector3 {
    const k1 = new Vector3( eps, -eps, -eps);
    const k2 = new Vector3(-eps, -eps,  eps);
    const k3 = new Vector3(-eps,  eps, -eps);
    const k4 = new Vector3( eps,  eps,  eps);

    const d1 = queryEntityDistance(p.add(k1), excludeIndex, entities).distance;
    const d2 = queryEntityDistance(p.add(k2), excludeIndex, entities).distance;
    const d3 = queryEntityDistance(p.add(k3), excludeIndex, entities).distance;
    const d4 = queryEntityDistance(p.add(k4), excludeIndex, entities).distance;

    const nx = k1.x * d1 + k2.x * d2 + k3.x * d3 + k4.x * d4;
    const ny = k1.y * d1 + k2.y * d2 + k3.y * d3 + k4.y * d4;
    const nz = k1.z * d1 + k2.z * d2 + k3.z * d3 + k4.z * d4;

    return new Vector3(nx, ny, nz).normalize();
}

/**
 * Exact collision resolution between two soft-body entities.
 * If distance d < 0 (penetration detected), computes penetration depth -d,
 * pushes entities apart along contact normal by half penetration depth,
 * and applies restitution impulse to reflect velocities along contact normal.
 */
export function resolveCollision(
    entityA: SoftBodyEntity,
    entityB: SoftBodyEntity,
    restitution: number = 0.5
): boolean {
    if (!entityA.active || !entityB.active) return false;

    const delta = entityB.position.sub(entityA.position);
    const dist = delta.length();
    const sumRadii = entityA.radius + entityB.radius;
    const d = dist - sumRadii;

    // d < 0: penetration detected
    if (d < 0) {
        const penetrationDepth = -d;
        let normal: Vector3;

        if (dist > 1e-6) {
            normal = delta.scale(1.0 / dist);
        } else {
            // Degenerate coincidence fallback
            normal = new Vector3(0, 1, 0);
        }

        // Positional separation
        const halfPen = penetrationDepth * 0.5;
        if (!entityA.isStatic && !entityB.isStatic) {
            entityA.position = entityA.position.sub(normal.scale(halfPen));
            entityB.position = entityB.position.add(normal.scale(halfPen));
        } else if (!entityA.isStatic && entityB.isStatic) {
            entityA.position = entityA.position.sub(normal.scale(penetrationDepth));
        } else if (entityA.isStatic && !entityB.isStatic) {
            entityB.position = entityB.position.add(normal.scale(penetrationDepth));
        }

        // Restitution velocity impulse along normal
        const relVel = entityB.velocity.sub(entityA.velocity);
        const vn = relVel.dot(normal);

        // Only resolve if objects are approaching each other
        if (vn < 0) {
            const massA = entityA.mass ?? (entityA.radius * entityA.radius * entityA.radius);
            const massB = entityB.mass ?? (entityB.radius * entityB.radius * entityB.radius);

            if (!entityA.isStatic && !entityB.isStatic) {
                const invMassA = 1.0 / massA;
                const invMassB = 1.0 / massB;
                const j = -(1.0 + restitution) * vn / (invMassA + invMassB);
                const impulse = normal.scale(j);
                entityA.velocity = entityA.velocity.sub(impulse.scale(invMassA));
                entityB.velocity = entityB.velocity.add(impulse.scale(invMassB));
            } else if (!entityA.isStatic && entityB.isStatic) {
                const j = -(1.0 + restitution) * vn * massA;
                entityA.velocity = entityA.velocity.sub(normal.scale(j / massA));
            } else if (entityA.isStatic && !entityB.isStatic) {
                const j = -(1.0 + restitution) * vn * massB;
                entityB.velocity = entityB.velocity.add(normal.scale(j / massB));
            }
        }

        return true;
    }

    return false;
}

/**
 * SDF Collision System instance wrapping entity collections
 */
export class SDFCollisionSystem {
    public entities: SoftBodyEntity[];

    constructor(entities: SoftBodyEntity[] = []) {
        this.entities = entities;
    }

    public queryEntityDistance(point: Vector3, excludeIndex: number = -1): { distance: number; hitEntityIndex: number } {
        return queryEntityDistance(point, excludeIndex, this.entities);
    }

    public queryEntitySDF(point: Vector3, excludeIndex: number = -1): { distance: number; hitEntityIndex: number } {
        return queryEntitySDF(point, excludeIndex, this.entities);
    }

    public calculateContactNormal(point: Vector3, eps: number = 0.001, excludeIndex: number = -1): Vector3 {
        return calculateContactNormal(point, eps, excludeIndex, this.entities);
    }

    public calculateSDFNormal(point: Vector3, eps: number = 0.001, excludeIndex: number = -1): Vector3 {
        return calculateSDFNormal(point, eps, excludeIndex, this.entities);
    }

    public resolveCollision(entityA: SoftBodyEntity, entityB: SoftBodyEntity, restitution: number = 0.5): boolean {
        return resolveCollision(entityA, entityB, restitution);
    }

    public resolveAllCollisions(restitution: number = 0.5): number {
        let collisionCount = 0;
        const len = this.entities.length;
        for (let i = 0; i < len; ++i) {
            const a = this.entities[i];
            if (!a.active) continue;
            for (let j = i + 1; j < len; ++j) {
                const b = this.entities[j];
                if (!b.active) continue;
                if (resolveCollision(a, b, restitution)) {
                    collisionCount++;
                }
            }
        }
        return collisionCount;
    }
}
