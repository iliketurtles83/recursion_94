/**
 * SoftBodySystem.ts
 *
 * Tracks soft-body entity lifecycles, positions, velocities, radii, and blending thresholds (k).
 * Handles volume-conserving dynamic merging and splitting (mass ejection and dash mechanics),
 * and synchronizes entity state between CPU analytical physics and GPU uniform buffers.
 */

import {
    Vector3,
    Vector3Like,
    SoftBodyEntity,
    resolveCollision,
    queryEntityDistance,
    calculateContactNormal,
    smin,
    setActiveEntityRegistry
} from '../physics/SDFCollision';

export interface EntityConfig {
    id?: number;
    type?: 'player' | 'hazard' | 'collectible' | 'projectile' | 'blob' | string;
    position?: Vector3 | Vector3Like;
    velocity?: Vector3 | Vector3Like;
    radius?: number;
    color?: [number, number, number];
    blendRadius?: number; // 'k' parameter
    mass?: number;
    active?: boolean;
    isStatic?: boolean;
}

export interface MergeEvent {
    survivorId: number;
    absorbedId: number;
    survivorType?: string;
    absorbedType?: string;
    oldRadius: number;
    newRadius: number;
    absorbedRadius: number;
    position: Vector3;
    timestamp: number;
}

export interface SplitEvent {
    parentId: number;
    childId: number;
    parentOldRadius: number;
    parentNewRadius: number;
    childRadius: number;
    position: Vector3;
    velocity: Vector3;
    timestamp: number;
}

export interface EntityUniformData {
    entityCount: number;
    positions: Float32Array;      // 16 * 3 floats
    radii: Float32Array;          // 16 floats
    colors: Float32Array;         // 16 * 3 floats
    blendRadii: Float32Array;     // 16 floats
    packedBuffer: Float32Array;   // 16 * 8 floats (std140 layout: [x,y,z,r, cr,cg,cb,k])
}

export interface CachedUniformLocations {
    u_entityCount: WebGLUniformLocation | null;
    entities: {
        position: WebGLUniformLocation | null;
        radius: WebGLUniformLocation | null;
        color: WebGLUniformLocation | null;
        blendRadius: WebGLUniformLocation | null;
    }[];
}

export class SoftBodySystem {
    public entities: SoftBodyEntity[] = [];
    public readonly maxEntities: number = 16;
    private nextEntityId: number = 1;
    public playerId: number = -1;

    // Simulation settings
    public globalDamping: number = 0.985;
    public minPlayerRadius: number = 0.35;
    public minSplitRadius: number = 0.15;
    public defaultRestitution: number = 0.5;

    // Event hooks
    public onMerge?: (event: MergeEvent) => void;
    public onSplit?: (event: SplitEvent) => void;
    public onHazardCollision?: (player: SoftBodyEntity, hazard: SoftBodyEntity) => void;

    // Pre-allocated uniform buffers to prevent gameplay-time heap allocations
    private uniformData: EntityUniformData = {
        entityCount: 0,
        positions: new Float32Array(16 * 3),
        radii: new Float32Array(16),
        colors: new Float32Array(16 * 3),
        blendRadii: new Float32Array(16),
        packedBuffer: new Float32Array(16 * 8)
    };

    constructor(initialEntities?: EntityConfig[]) {
        if (initialEntities) {
            for (const cfg of initialEntities) {
                this.createEntity(cfg);
            }
        }
        setActiveEntityRegistry(this.entities);
    }

    /**
     * Instantiates a new soft-body entity within the system.
     */
    public createEntity(config: EntityConfig = {}): SoftBodyEntity {
        const id = config.id ?? this.nextEntityId++;
        if (id >= this.nextEntityId) {
            this.nextEntityId = id + 1;
        }

        const radius = config.radius ?? 1.0;
        const mass = config.mass ?? (radius * radius * radius);

        const entity: SoftBodyEntity = {
            id,
            type: config.type ?? 'blob',
            position: config.position instanceof Vector3
                ? config.position.clone()
                : Vector3.from(config.position ?? { x: 0, y: 0, z: 0 }),
            velocity: config.velocity instanceof Vector3
                ? config.velocity.clone()
                : Vector3.from(config.velocity ?? { x: 0, y: 0, z: 0 }),
            radius,
            color: config.color ?? [0.2, 0.8, 0.9],
            blendRadius: config.blendRadius ?? 0.6,
            mass,
            active: config.active ?? true,
            isBonding: false,
            bondedEntityIds: [],
            isStatic: config.isStatic ?? false
        };

        this.entities.push(entity);
        if (entity.type === 'player' && this.playerId === -1) {
            this.playerId = entity.id;
        }

        return entity;
    }

    public getEntity(id: number): SoftBodyEntity | undefined {
        return this.entities.find(e => e.id === id);
    }

    public getPlayer(): SoftBodyEntity | undefined {
        if (this.playerId === -1) {
            return this.entities.find(e => e.type === 'player' && e.active);
        }
        return this.getEntity(this.playerId);
    }

    public setPlayer(entityOrId: SoftBodyEntity | number): void {
        if (typeof entityOrId === 'number') {
            this.playerId = entityOrId;
        } else {
            this.playerId = entityOrId.id;
            if (!this.entities.includes(entityOrId)) {
                this.entities.push(entityOrId);
            }
        }
    }

    public removeEntity(id: number): boolean {
        const index = this.entities.findIndex(e => e.id === id);
        if (index !== -1) {
            this.entities.splice(index, 1);
            if (this.playerId === id) {
                this.playerId = -1;
            }
            return true;
        }
        return false;
    }

    public getActiveEntities(): SoftBodyEntity[] {
        return this.entities.filter(e => e.active);
    }

    /**
     * Checks bonding proximity and merge triggers.
     * Bonding: center-to-center distance D < (r1 + r2 + k).
     * Merging: D < max(r1, r2).
     * Merges follow strict volumetric conservation: r_new = cbrt(r1^3 + r2^3).
     */
    public checkMergeSplitTriggers(): { merges: MergeEvent[]; bondingCount: number } {
        const merges: MergeEvent[] = [];
        let bondingCount = 0;

        const activeList = this.getActiveEntities();
        const len = activeList.length;

        // Reset bonding state on all active entities
        for (let i = 0; i < len; ++i) {
            activeList[i].isBonding = false;
            activeList[i].bondedEntityIds = [];
        }

        for (let i = 0; i < len; ++i) {
            const entA = activeList[i];
            if (!entA.active) continue;

            for (let j = i + 1; j < len; ++j) {
                const entB = activeList[j];
                if (!entB.active) continue;

                const delta = entB.position.sub(entA.position);
                const D = delta.length();
                const k = Math.max(entA.blendRadius, entB.blendRadius);
                const bondThreshold = entA.radius + entB.radius + k;

                // Check bonding state (liquid coalescence threshold)
                if (D < bondThreshold) {
                    entA.isBonding = true;
                    entB.isBonding = true;
                    entA.bondedEntityIds!.push(entB.id);
                    entB.bondedEntityIds!.push(entA.id);
                    bondingCount++;
                }

                // Check merge threshold: D < max(r1, r2)
                const mergeThreshold = Math.max(entA.radius, entB.radius);
                if (D < mergeThreshold) {
                    // Check if entities are eligible to merge
                    if (this.canMerge(entA, entB)) {
                        const event = this.executeMerge(entA, entB);
                        if (event) {
                            merges.push(event);
                        }
                    }
                }
            }
        }

        return { merges, bondingCount };
    }

    /**
     * Evaluates merge eligibility between two entities based on gameplay roles.
     */
    private canMerge(a: SoftBodyEntity, b: SoftBodyEntity): boolean {
        // Hazard and player do not merge; they collide and cause damage
        if ((a.type === 'player' && b.type === 'hazard') || (a.type === 'hazard' && b.type === 'player')) {
            if (this.onHazardCollision) {
                const player = a.type === 'player' ? a : b;
                const hazard = a.type === 'hazard' ? a : b;
                this.onHazardCollision(player, hazard);
            }
            return false;
        }

        // Collectibles always merge into player or other collectibles
        if (a.type === 'collectible' || b.type === 'collectible') return true;

        // Same-type entities merge (blob + blob, hazard + hazard)
        if (a.type === b.type) return true;

        // Player absorbs blobs
        if ((a.type === 'player' && b.type === 'blob') || (a.type === 'blob' && b.type === 'player')) {
            return true;
        }

        return false;
    }

    /**
     * Executes volumetric merge: r_new = cbrt(r1^3 + r2^3).
     * Conserves mass, volume, and momentum. Blends surface colors proportionally.
     */
    private executeMerge(a: SoftBodyEntity, b: SoftBodyEntity): MergeEvent | null {
        // Determine survivor (player takes precedence; otherwise larger radius survives)
        let survivor: SoftBodyEntity;
        let absorbed: SoftBodyEntity;

        if (a.type === 'player') {
            survivor = a;
            absorbed = b;
        } else if (b.type === 'player') {
            survivor = b;
            absorbed = a;
        } else if (a.radius >= b.radius) {
            survivor = a;
            absorbed = b;
        } else {
            survivor = b;
            absorbed = a;
        }

        const r1 = survivor.radius;
        const r2 = absorbed.radius;
        const v1 = r1 * r1 * r1;
        const v2 = r2 * r2 * r2;
        const vTotal = v1 + v2;

        // Strict volumetric conservation
        const rNew = Math.cbrt(vTotal);

        // Blended surface color based on mass contribution
        const c1 = survivor.color;
        const c2 = absorbed.color;
        const blendedColor: [number, number, number] = [
            (c1[0] * v1 + c2[0] * v2) / vTotal,
            (c1[1] * v1 + c2[1] * v2) / vTotal,
            (c1[2] * v1 + c2[2] * v2) / vTotal
        ];

        // Momentum conservation: (v1*m1 + v2*m2) / (m1+m2)
        const survivorVel = survivor.velocity;
        const absorbedVel = absorbed.velocity;
        const blendedVelocity = new Vector3(
            (survivorVel.x * v1 + absorbedVel.x * v2) / vTotal,
            (survivorVel.y * v1 + absorbedVel.y * v2) / vTotal,
            (survivorVel.z * v1 + absorbedVel.z * v2) / vTotal
        );

        const event: MergeEvent = {
            survivorId: survivor.id,
            absorbedId: absorbed.id,
            survivorType: survivor.type,
            absorbedType: absorbed.type,
            oldRadius: r1,
            newRadius: rNew,
            absorbedRadius: r2,
            position: survivor.position.clone(),
            timestamp: Date.now()
        };

        // Apply state changes
        survivor.radius = rNew;
        survivor.mass = vTotal;
        survivor.color = blendedColor;
        survivor.velocity = blendedVelocity;

        // Deactivate absorbed entity
        absorbed.active = false;

        if (this.onMerge) {
            this.onMerge(event);
        }

        return event;
    }

    /**
     * Splits an entity by ejecting mass along forwardDir.
     * Preserves volume: r_parent_new = cbrt(r_parent_old^3 - r_eject^3).
     */
    public splitEntity(
        parentId: number,
        ejectRadius: number,
        forwardDir?: Vector3,
        ejectSpeed: number = 18.0,
        childType: string = 'projectile'
    ): SoftBodyEntity | null {
        const parent = this.getEntity(parentId);
        if (!parent || !parent.active) return null;

        const parentOldVol = parent.radius * parent.radius * parent.radius;
        const minVol = (parent.type === 'player')
            ? (this.minPlayerRadius * this.minPlayerRadius * this.minPlayerRadius)
            : (this.minSplitRadius * this.minSplitRadius * this.minSplitRadius);

        // Ensure parent retains at least minVol
        let targetEjectVol = ejectRadius * ejectRadius * ejectRadius;
        if (parentOldVol - targetEjectVol < minVol) {
            targetEjectVol = Math.max(0, parentOldVol - minVol);
        }

        const effectiveEjectRadius = Math.cbrt(targetEjectVol);
        if (effectiveEjectRadius < this.minSplitRadius) {
            return null; // Not enough mass to eject
        }

        // Volume conservation for parent
        const parentNewVol = parentOldVol - targetEjectVol;
        const parentNewRadius = Math.cbrt(parentNewVol);

        // Determine trajectory
        let dir: Vector3;
        if (forwardDir && forwardDir.lengthSq() > 1e-6) {
            dir = forwardDir.normalize();
        } else if (parent.velocity.lengthSq() > 1e-6) {
            dir = parent.velocity.normalize();
        } else {
            dir = new Vector3(0, 0, -1); // Default rail-shooter forward
        }

        // Spawn child entity pushed out along trajectory
        const spawnDistance = parentNewRadius + effectiveEjectRadius + 0.1;
        const spawnPos = parent.position.add(dir.scale(spawnDistance));
        const spawnVel = parent.velocity.add(dir.scale(ejectSpeed));

        // Recoil reaction on parent (momentum conservation)
        const recoilMag = ejectSpeed * (targetEjectVol / Math.max(parentNewVol, 1e-4));
        parent.velocity = parent.velocity.sub(dir.scale(recoilMag));
        parent.radius = parentNewRadius;
        parent.mass = parentNewVol;

        const child = this.createEntity({
            type: childType,
            position: spawnPos,
            velocity: spawnVel,
            radius: effectiveEjectRadius,
            color: [...parent.color],
            blendRadius: Math.min(parent.blendRadius, effectiveEjectRadius * 0.7),
            mass: targetEjectVol,
            active: true
        });

        const event: SplitEvent = {
            parentId: parent.id,
            childId: child.id,
            parentOldRadius: Math.cbrt(parentOldVol),
            parentNewRadius,
            childRadius: effectiveEjectRadius,
            position: spawnPos.clone(),
            velocity: spawnVel.clone(),
            timestamp: Date.now()
        };

        if (this.onSplit) {
            this.onSplit(event);
        }

        return child;
    }

    /**
     * Splits or ejects mass from the active player entity.
     */
    public triggerPlayerSplit(ejectRadius: number = 0.45, forwardDir?: Vector3): SoftBodyEntity | null {
        const player = this.getPlayer();
        if (!player) return null;
        return this.splitEntity(player.id, ejectRadius, forwardDir, 20.0, 'projectile');
    }

    /**
     * Dash mechanic: ejects a small reverse propulsion pulse while accelerating forward.
     */
    public triggerPlayerDash(dashSpeed: number = 25.0, ejectRadius: number = 0.3): SoftBodyEntity | null {
        const player = this.getPlayer();
        if (!player) return null;

        const forward = new Vector3(0, 0, -1);
        const child = this.splitEntity(player.id, ejectRadius, forward.scale(-1.0), 12.0, 'blob');
        if (child) {
            player.velocity = player.velocity.add(forward.scale(dashSpeed));
        }
        return child;
    }

    /**
     * Advances simulation tick:
     * 1. Detects bonding and triggers merges.
     * 2. Resolves collisions between non-merged active entities.
     * 3. Integrates velocities and applies damping.
     */
    public update(dt: number): void {
        const clampedDt = Math.min(dt, 0.05);

        // 1. Check dynamic bonding and merge triggers
        this.checkMergeSplitTriggers();

        // 2. Resolve collisions between remaining active entities
        const activeList = this.getActiveEntities();
        const len = activeList.length;

        for (let i = 0; i < len; ++i) {
            const entA = activeList[i];
            if (!entA.active) continue;

            for (let j = i + 1; j < len; ++j) {
                const entB = activeList[j];
                if (!entB.active) continue;

                // Non-bonding or non-merged contact: apply separation and restitution impulse
                resolveCollision(entA, entB, this.defaultRestitution);
            }
        }

        // 3. Position integration and damping
        for (let i = 0; i < len; ++i) {
            const ent = activeList[i];
            if (!ent.active || ent.isStatic) continue;

            ent.position = ent.position.add(ent.velocity.scale(clampedDt));
            ent.velocity = ent.velocity.scale(Math.pow(this.globalDamping, clampedDt * 60.0));
        }

        // Keep global registry synchronized
        setActiveEntityRegistry(this.entities);
    }

    /**
     * Packs active entity data into Float32Arrays for GPU uniform buffer/array updates.
     * Supports up to 16 active entities.
     */
    public packEntityUniforms(): EntityUniformData {
        const activeList = this.getActiveEntities().slice(0, this.maxEntities);
        const count = activeList.length;
        const data = this.uniformData;

        data.entityCount = count;

        // Reset buffers
        data.positions.fill(0);
        data.radii.fill(0);
        data.colors.fill(0);
        data.blendRadii.fill(0);
        data.packedBuffer.fill(0);

        for (let i = 0; i < count; ++i) {
            const ent = activeList[i];

            // Separate arrays
            data.positions[i * 3 + 0] = ent.position.x;
            data.positions[i * 3 + 1] = ent.position.y;
            data.positions[i * 3 + 2] = ent.position.z;

            data.radii[i] = ent.radius;

            data.colors[i * 3 + 0] = ent.color[0];
            data.colors[i * 3 + 1] = ent.color[1];
            data.colors[i * 3 + 2] = ent.color[2];

            data.blendRadii[i] = ent.blendRadius;

            // Packed std140 layout:
            // vec4 (position.xyz, radius)
            data.packedBuffer[i * 8 + 0] = ent.position.x;
            data.packedBuffer[i * 8 + 1] = ent.position.y;
            data.packedBuffer[i * 8 + 2] = ent.position.z;
            data.packedBuffer[i * 8 + 3] = ent.radius;

            // vec4 (color.rgb, blendRadius)
            data.packedBuffer[i * 8 + 4] = ent.color[0];
            data.packedBuffer[i * 8 + 5] = ent.color[1];
            data.packedBuffer[i * 8 + 6] = ent.color[2];
            data.packedBuffer[i * 8 + 7] = ent.blendRadius;
        }

        return data;
    }

    /**
     * Queries and caches uniform locations for struct EntityData u_entities[16].
     */
    public static cacheUniformLocations(gl: WebGL2RenderingContext, program: WebGLProgram): CachedUniformLocations {
        const u_entityCount = gl.getUniformLocation(program, 'u_entityCount');
        const entities: {
            position: WebGLUniformLocation | null;
            radius: WebGLUniformLocation | null;
            color: WebGLUniformLocation | null;
            blendRadius: WebGLUniformLocation | null;
        }[] = [];

        for (let i = 0; i < 16; ++i) {
            entities.push({
                position: gl.getUniformLocation(program, `u_entities[${i}].position`),
                radius: gl.getUniformLocation(program, `u_entities[${i}].radius`),
                color: gl.getUniformLocation(program, `u_entities[${i}].color`),
                blendRadius: gl.getUniformLocation(program, `u_entities[${i}].blendRadius`)
            });
        }

        return { u_entityCount, entities };
    }

    /**
     * Uploads packed entity uniforms directly to the GPU program.
     */
    public uploadUniformsToProgram(
        gl: WebGL2RenderingContext,
        program: WebGLProgram,
        cachedLocations?: CachedUniformLocations
    ): void {
        gl.useProgram(program);
        const packed = this.packEntityUniforms();
        const locations = cachedLocations || SoftBodySystem.cacheUniformLocations(gl, program);

        if (locations.u_entityCount) {
            gl.uniform1i(locations.u_entityCount, packed.entityCount);
        }

        for (let i = 0; i < packed.entityCount; ++i) {
            const loc = locations.entities[i];
            if (loc.position) {
                gl.uniform3f(
                    loc.position,
                    packed.positions[i * 3 + 0],
                    packed.positions[i * 3 + 1],
                    packed.positions[i * 3 + 2]
                );
            }
            if (loc.radius) {
                gl.uniform1f(loc.radius, packed.radii[i]);
            }
            if (loc.color) {
                gl.uniform3f(
                    loc.color,
                    packed.colors[i * 3 + 0],
                    packed.colors[i * 3 + 1],
                    packed.colors[i * 3 + 2]
                );
            }
            if (loc.blendRadius) {
                gl.uniform1f(loc.blendRadius, packed.blendRadii[i]);
            }
        }
    }
}
