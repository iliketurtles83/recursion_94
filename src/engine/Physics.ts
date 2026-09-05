/**
 * Physics.ts
 *
 * CPU-side port of the KIFS fractal distance field math and collision controller.
 * Enables player grounding, heightfield queries, and wall boundary collision resolution
 * directly against the procedural fractal world without maintaining a separate polygon mesh.
 */

export interface Vector3 {
    x: number;
    y: number;
    z: number;
}

export function vec3(x: number = 0, y: number = 0, z: number = 0): Vector3 {
    return { x, y, z };
}

export function vec3Add(a: Vector3, b: Vector3): Vector3 {
    return { x: a.x + b.x, y: a.y + b.y, z: a.z + b.z };
}

export function vec3Sub(a: Vector3, b: Vector3): Vector3 {
    return { x: a.x - b.x, y: a.y - b.y, z: a.z - b.z };
}

export function vec3Scale(v: Vector3, s: number): Vector3 {
    return { x: v.x * s, y: v.y * s, z: v.z * s };
}

export function vec3Dot(a: Vector3, b: Vector3): number {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

export function vec3Length(v: Vector3): number {
    return Math.hypot(v.x, v.y, v.z);
}

export function vec3Normalize(v: Vector3): Vector3 {
    const len = Math.hypot(v.x, v.y, v.z);
    if (len < 1e-7) return { x: 0, y: 0, z: 0 };
    return { x: v.x / len, y: v.y / len, z: v.z / len };
}

// Primitive SDF Evaluators
export function sdBox(p: Vector3, b: Vector3): number {
    const qx = Math.abs(p.x) - b.x;
    const qy = Math.abs(p.y) - b.y;
    const qz = Math.abs(p.z) - b.z;
    const outside = Math.hypot(Math.max(qx, 0.0), Math.max(qy, 0.0), Math.max(qz, 0.0));
    const inside = Math.min(Math.max(qx, Math.max(qy, qz)), 0.0);
    return outside + inside;
}

export function sdSphere(p: Vector3, r: number): number {
    return Math.hypot(p.x, p.y, p.z) - r;
}

/**
 * CPU-side port of the KIFS recursive fractal scene map.
 * Divides terminal distance by the cumulative scaling factor to conserve the Euclidean metric.
 */
export function mapScene(
    p: Vector3,
    iterations: number = 6,
    scaleFactor: number = 1.85,
    offset: Vector3 = { x: 1.0, y: 0.5, z: 1.0 }
): number {
    let w: Vector3 = { x: p.x, y: p.y, z: p.z };
    let scale = 1.0;

    for (let i = 0; i < iterations; ++i) {
        // 1. Fold across planes of symmetry
        w = {
            x: Math.abs(w.x) - 1.2,
            y: Math.abs(w.y) - 0.8,
            z: Math.abs(w.z) - 1.2
        };

        if (w.x < w.y) { const tmp = w.x; w.x = w.y; w.y = tmp; }
        if (w.x < w.z) { const tmp = w.x; w.x = w.z; w.z = tmp; }
        if (w.y < w.z) { const tmp = w.y; w.y = w.z; w.z = tmp; }

        // 2. Scale and translate
        w = {
            x: w.x * scaleFactor - offset.x * (scaleFactor - 1.0),
            y: w.y * scaleFactor - offset.y * (scaleFactor - 1.0),
            z: w.z * scaleFactor - offset.z * (scaleFactor - 1.0)
        };
        scale *= scaleFactor;
    }

    // Normalize terminal distance to prevent stepping past boundaries
    return (Math.hypot(w.x, w.y, w.z) - 0.4) / scale;
}

export function kifsFold(
    p: Vector3,
    iterations?: number,
    scaleFactor?: number,
    offset?: Vector3
): number {
    return mapScene(p, iterations, scaleFactor, offset);
}

/**
 * Tetrahedral 4-tap gradient estimator for computing surface normals on the CPU.
 */
export function calcNormal(
    p: Vector3,
    iterations: number = 6,
    scaleFactor: number = 1.85,
    offset: Vector3 = { x: 1.0, y: 0.5, z: 1.0 },
    eps: number = 0.0005
): Vector3 {
    const k1 = { x:  eps, y: -eps, z: -eps };
    const k2 = { x: -eps, y: -eps, z:  eps };
    const k3 = { x: -eps, y:  eps, z: -eps };
    const k4 = { x:  eps, y:  eps, z:  eps };

    const d1 = mapScene(vec3Add(p, k1), iterations, scaleFactor, offset);
    const d2 = mapScene(vec3Add(p, k2), iterations, scaleFactor, offset);
    const d3 = mapScene(vec3Add(p, k3), iterations, scaleFactor, offset);
    const d4 = mapScene(vec3Add(p, k4), iterations, scaleFactor, offset);

    const nx = k1.x * d1 + k2.x * d2 + k3.x * d3 + k4.x * d4;
    const ny = k1.y * d1 + k2.y * d2 + k3.y * d3 + k4.y * d4;
    const nz = k1.z * d1 + k2.z * d2 + k3.z * d3 + k4.z * d4;

    return vec3Normalize({ x: nx, y: ny, z: nz });
}

/**
 * CPU sphere-tracing raymarcher with over-relaxation and dynamic epsilon scaling.
 */
export function raymarch(
    ro: Vector3,
    rd: Vector3,
    maxDist: number = 80.0,
    maxSteps: number = 80,
    iterations: number = 6,
    scaleFactor: number = 1.85,
    offset: Vector3 = { x: 1.0, y: 0.5, z: 1.0 }
): { hit: boolean; distance: number; steps: number } {
    let t = 0.0;
    let candidateError = 0.0;
    const omega = 1.2;

    const dir = vec3Normalize(rd);

    for (let step = 0; step < maxSteps; ++step) {
        const p = vec3Add(ro, vec3Scale(dir, t));
        let d = mapScene(p, iterations, scaleFactor, offset);

        // Over-relaxation
        if (omega > 1.0 && (d + candidateError) < candidateError) {
            t -= candidateError;
            d = mapScene(vec3Add(ro, vec3Scale(dir, t)), iterations, scaleFactor, offset);
            t += d;
            candidateError = 0.0;
        } else {
            candidateError = d * (omega - 1.0);
            t += d * omega;
        }

        const eps = Math.max(0.0002, 0.001 * t);
        if (d < eps) {
            return { hit: true, distance: t, steps: step + 1 };
        }
        if (t >= maxDist) {
            break;
        }
    }

    return { hit: false, distance: -1.0, steps: maxSteps };
}

export interface WallCollisionResult {
    colliding: boolean;
    penetration: number;
    normal: Vector3;
    distance: number;
}

export interface GroundQueryResult {
    groundFound: boolean;
    groundY: number;
    distance: number;
    normal: Vector3;
}

export class CollisionController {
    public iterations: number;
    public scale: number;
    public offset: Vector3;

    constructor(iterations: number = 6, scale: number = 1.85, offset?: Vector3) {
        this.iterations = iterations;
        this.scale = scale;
        this.offset = offset ?? { x: 1.0, y: 0.5, z: 1.0 };
    }

    /**
     * Evaluates the signed distance field at position p
     */
    public sampleDistance(p: Vector3): number {
        return mapScene(p, this.iterations, this.scale, this.offset);
    }

    /**
     * Evaluates the surface normal at position p using 4-tap tetrahedral gradient
     */
    public sampleNormal(p: Vector3): Vector3 {
        return calcNormal(p, this.iterations, this.scale, this.offset);
    }

    /**
     * Queries ground elevation directly below (x, z) starting from startY.
     * Uses sphere-tracing downward along (0, -1, 0).
     */
    public queryGroundHeight(
        x: number,
        z: number,
        startY: number = 15.0,
        maxDepth: number = 50.0
    ): GroundQueryResult {
        const ro: Vector3 = { x, y: startY, z };
        const rd: Vector3 = { x: 0, y: -1, z: 0 };

        const result = raymarch(ro, rd, maxDepth, 64, this.iterations, this.scale, this.offset);
        if (result.hit) {
            const hitPoint: Vector3 = { x, y: startY - result.distance, z };
            const normal = this.sampleNormal(hitPoint);
            return {
                groundFound: true,
                groundY: hitPoint.y,
                distance: result.distance,
                normal
            };
        }

        return {
            groundFound: false,
            groundY: startY - maxDepth,
            distance: maxDepth,
            normal: { x: 0, y: 1, z: 0 }
        };
    }

    /**
     * Tests whether a spherical volume at position penetrates fractal walls or obstacles.
     */
    public checkWallCollision(position: Vector3, radius: number): WallCollisionResult {
        const distance = this.sampleDistance(position);
        if (distance < radius) {
            const normal = this.sampleNormal(position);
            const penetration = radius - distance;
            return {
                colliding: true,
                penetration,
                normal,
                distance
            };
        }
        return {
            colliding: false,
            penetration: 0,
            normal: { x: 0, y: 0, z: 0 },
            distance
        };
    }

    /**
     * Grounds a player controller by maintaining a desired clearance above the fractal ground.
     */
    public groundPlayer(
        playerPos: Vector3,
        desiredClearance: number = 1.5,
        smoothing: number = 0.2
    ): Vector3 {
        const ground = this.queryGroundHeight(playerPos.x, playerPos.z, playerPos.y + 2.0);
        if (ground.groundFound) {
            const targetY = ground.groundY + desiredClearance;
            const newY = playerPos.y + (targetY - playerPos.y) * Math.min(1.0, smoothing);
            return { x: playerPos.x, y: newY, z: playerPos.z };
        }
        return { ...playerPos };
    }

    /**
     * Pushes a player out of any penetrating fractal boundaries.
     */
    public resolvePlayerPenetration(playerPos: Vector3, radius: number): Vector3 {
        const collision = this.checkWallCollision(playerPos, radius);
        if (collision.colliding) {
            return vec3Add(playerPos, vec3Scale(collision.normal, collision.penetration));
        }
        return { ...playerPos };
    }
}
