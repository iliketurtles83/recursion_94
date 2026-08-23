#include "environment.h"
#include "raymath.h"
#include <math.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    DISTRICT_MEMORY_CITY = 0,
    DISTRICT_CONDUIT_EXCHANGE,
    DISTRICT_PROCESSOR_CATHEDRAL,
    DISTRICT_ANTENNA_GARDEN,
    DISTRICT_OPEN_VOID,
    DISTRICT_THEME_COUNT
} DistrictTheme;

typedef enum {
    EMIT_DETAIL = 0,
    EMIT_CONNECTION,
    EMIT_STRUCTURAL,
    EMIT_CRITICAL
} EmitPriority;

typedef struct {
    int key;
    int districtIndex;
    int ordinal;
    int startSector;
    int length;
    DistrictTheme theme;
    float density;
    float openness;
    float heightScale;
} ZoneDescriptor;

// Every pattern sums to DISTRICT_SECTORS. This creates variable-length zones
// while keeping arbitrary sector lookup bounded, stateless, and allocation-free.
static const unsigned char ZONE_PATTERNS[][4] = {
    { 4, 4, 4, 4 },
    { 3, 5, 3, 5 },
    { 5, 3, 4, 4 },
    { 3, 4, 5, 4 },
    { 6, 3, 3, 4 },
    { 4, 5, 4, 3 }
};

#define ZONE_PATTERN_COUNT ((int)(sizeof(ZONE_PATTERNS) / sizeof(ZONE_PATTERNS[0])))
#define ENVIRONMENT_LATERAL_SCALE 1.5f
#define CORRIDOR_EDGE_X (15.0f * ENVIRONMENT_LATERAL_SCALE)
#define TERRAIN_CELL_WIDTH 8.0f
#define TERRAIN_BASE_Y -28.0f
#define FLANK_OUTWARD_SHIFT 8.0f
#define FLANK_SKIP_CHANCE 0.25f
#define FLANK_SKIP_LEFT_SEED 16001
#define FLANK_SKIP_RIGHT_SEED 16002

typedef struct {
    bool occupied;
    float topY;
    float widthScale;
} TerrainSupport;

static uint32_t g_environmentSeed = 94u;

static inline float ScaleEnvironmentX(float x) {
    return x * ENVIRONMENT_LATERAL_SCALE;
}

// Stateless integer hash functions for deterministic, high-entropy procedural generation
static inline unsigned int HashUint(unsigned int x) {
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = (x >> 16) ^ x;
    return x;
}

static inline uint32_t SeededHash(uint32_t value, uint32_t salt) {
    return HashUint(value ^ HashUint(g_environmentSeed + salt));
}

// Shared with gameplay/demoscene palette selection so one seed presents one
// coherent audiovisual identity even though the generation hashes differ.
static inline uint32_t PaletteHash(uint32_t value) {
    value ^= value >> 16;
    value *= UINT32_C(0x7feb352d);
    value ^= value >> 15;
    value *= UINT32_C(0x846ca68b);
    value ^= value >> 16;
    return value;
}

static inline float HashFloat(int sector, int seed) {
    // Cast before multiplication so wrapping is defined unsigned arithmetic.
    uint32_t h = HashUint((uint32_t)sector * UINT32_C(374761393) +
                          (uint32_t)seed * UINT32_C(668265263) +
                          HashUint(g_environmentSeed ^ UINT32_C(0x94d31a7b)) +
                          UINT32_C(1013904223));
    return (float)(h & UINT32_C(0x00ffffff)) / 16777215.0f;
}

static inline int FloorDiv(int value, int divisor) {
    int quotient = value / divisor;
    int remainder = value % divisor;
    if (remainder < 0) quotient--;
    return quotient;
}

static inline int FloorMod(int value, int divisor) {
    int result = value % divisor;
    return (result < 0) ? result + divisor : result;
}

static inline float Smooth01(float value) {
    float t = Clamp(value, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Smooth deterministic noise for district-scale composition. Unlike raw hash
// jitter, this produces correlated rises and falls across adjacent districts.
static float ValueNoise1D(float position, int seed) {
    int cell = (int)floorf(position);
    float local = position - (float)cell;
    float a = HashFloat(cell, seed);
    float b = HashFloat(cell + 1, seed);
    return Lerp(a, b, Smooth01(local));
}

static TerrainSupport SampleTerrainSupport(int cellX, int sector) {
    TerrainSupport support = { 0 };
    int distanceFromCenter = cellX < 0 ? -cellX : cellX;
    uint32_t identity = (uint32_t)cellX * UINT32_C(0x9e3779b9) ^
                        (uint32_t)sector * UINT32_C(0x85ebca6b);
    float detail = (float)(SeededHash(identity, UINT32_C(0x4cf5ad43)) & 0xffffu) / 65535.0f;
    float longitudinal = ValueNoise1D((float)sector * 0.20f + (float)cellX * 0.11f, 14001);
    float broadRelief = floorf(longitudinal * 4.0f) * 1.25f;
    float localStep = floorf(detail * 3.0f) * 0.65f;
    float shoulder = powf((float)distanceFromCenter /
                          (4.0f * ENVIRONMENT_LATERAL_SCALE), 1.35f) * 5.2f;

    support.occupied = true;
    support.topY = -6.4f + shoulder + broadRelief + localStep;
    if (distanceFromCenter <= 1) support.topY -= 0.9f;
    if (support.topY > 2.0f) support.topY = 2.0f;
    support.widthScale = 1.03f;
    return support;
}

static int TerrainCellForX(float x) {
    return (int)floorf(x / TERRAIN_CELL_WIDTH + 0.5f);
}

static void GroundFlankStructures(EnvironmentSystem *env, float virtualPlayerZ) {
    for (int i = 0; i < env->structureCount; i++) {
        EnvironmentStructure *structure = &env->structures[i];
        if (fabsf(structure->position.x) < CORRIDOR_EDGE_X) continue;

        int cellX = TerrainCellForX(structure->position.x);
        int sector = (int)floorf((virtualPlayerZ - structure->position.z) / SECTOR_DEPTH);
        TerrainSupport support = SampleTerrainSupport(cellX, sector);
        structure->position.y += support.topY + 3.0f;
    }
}

static bool TerrainCellSupportsStructure(const EnvironmentSystem *env, float virtualPlayerZ,
                                         int cellX, int sector) {
    for (int i = 0; i < env->structureCount; i++) {
        const EnvironmentStructure *structure = &env->structures[i];
        if (fabsf(structure->position.x) < CORRIDOR_EDGE_X) continue;
        if (TerrainCellForX(structure->position.x) != cellX) continue;
        int structureSector = (int)floorf((virtualPlayerZ - structure->position.z) / SECTOR_DEPTH);
        if (structureSector == sector) return true;
    }
    for (int i = 0; i < env->farStructureCount; i++) {
        const EnvironmentStructure *structure = &env->farStructures[i];
        if (TerrainCellForX(structure->position.x) != cellX) continue;
        int structureSector = (int)floorf((virtualPlayerZ - structure->position.z) / SECTOR_DEPTH);
        if (structureSector == sector) return true;
    }
    return false;
}

static void GenerateTerrain(EnvironmentSystem *env, float virtualPlayerZ) {
    env->terrainCount = 0;
    int currentSector = (int)floorf(virtualPlayerZ / SECTOR_DEPTH) - 2;
    int visibleSectors = (int)(TERRAIN_DRAW_DISTANCE / SECTOR_DEPTH) + 4;

    for (int offset = 0; offset < visibleSectors; offset++) {
        int sector = currentSector + offset;
        float worldZ = (float)sector * SECTOR_DEPTH;
        float relativeZ = -(worldZ - virtualPlayerZ) - SECTOR_DEPTH * 0.5f;
        bool farTier = -relativeZ > DRAW_DISTANCE;

        for (int cellX = -12; cellX <= 12; cellX++) {
            TerrainSupport support = SampleTerrainSupport(cellX, sector);
            bool foundation = TerrainCellSupportsStructure(env, virtualPlayerZ, cellX, sector);
            if (farTier && (cellX & 1) != 0 && !foundation) continue;
            if (!support.occupied && !foundation) continue;
            if (env->terrainCount >= MAX_TERRAIN_INSTANCES) return;

            float topY = support.topY;
            float height = topY - TERRAIN_BASE_Y;
            float width = TERRAIN_CELL_WIDTH * support.widthScale * (farTier ? 1.95f : 1.0f);
            float depth = SECTOR_DEPTH * 1.03f;
            TerrainInstance *instance = &env->terrain[env->terrainCount++];
            instance->position = (Vector3){ (float)cellX * TERRAIN_CELL_WIDTH,
                                            TERRAIN_BASE_Y + height * 0.5f,
                                            relativeZ };
            instance->size = (Vector3){ width, height, depth };
        }
    }
}

static void GenerateFarSilhouettes(EnvironmentSystem *env, float virtualPlayerZ) {
    env->farStructureCount = 0;
    int currentSector = (int)floorf(virtualPlayerZ / SECTOR_DEPTH);
    int firstFarSector = currentSector + (int)(DRAW_DISTANCE / SECTOR_DEPTH);
    int lastFarSector = currentSector + (int)(TERRAIN_DRAW_DISTANCE / SECTOR_DEPTH) + 1;

    for (int sector = firstFarSector; sector <= lastFarSector; sector += 2) {
        if (HashFloat(sector, 15001) < 0.34f) continue;
        int side = HashFloat(sector, 15002) < 0.5f ? -1 : 1;
        int cellX = side * (4 + 2 * (int)(HashFloat(sector, 15003) * 3.0f));
        TerrainSupport support = SampleTerrainSupport(cellX, sector);
        float height = 13.0f + HashFloat(sector, 15004) * 24.0f;
        float width = 5.0f + HashFloat(sector, 15005) * 7.0f;
        float worldZ = (float)sector * SECTOR_DEPTH;

        if (env->farStructureCount >= MAX_FAR_STRUCTURES) return;
        EnvironmentStructure *structure = &env->farStructures[env->farStructureCount++];
        structure->type = HashFloat(sector, 15006) < 0.72f ? STRUCT_CACHE_TOWER
                                                            : STRUCT_MEMORY_SLAB;
        structure->position = (Vector3){ ScaleEnvironmentX((float)cellX * TERRAIN_CELL_WIDTH),
                                         support.topY - 0.35f + height * 0.5f,
                                         -(worldZ - virtualPlayerZ) - SECTOR_DEPTH * 0.5f };
        structure->size = (Vector3){ ScaleEnvironmentX(width), height, width * 0.82f };
        structure->compileScale = 1.0f;
        structure->active = true;
    }
}

static ZoneDescriptor DescribeZoneByKey(int zoneKey) {
    ZoneDescriptor zone = { 0 };
    zone.key = zoneKey;
    zone.districtIndex = FloorDiv(zoneKey, 4);
    zone.ordinal = FloorMod(zoneKey, 4);

    int patternIndex = (int)(SeededHash((uint32_t)zone.districtIndex, UINT32_C(0x7f4a7c15)) %
                             (uint32_t)ZONE_PATTERN_COUNT);
    int offset = 0;
    for (int i = 0; i < zone.ordinal; i++) offset += ZONE_PATTERNS[patternIndex][i];

    zone.startSector = zone.districtIndex * DISTRICT_SECTORS + offset;
    zone.length = ZONE_PATTERNS[patternIndex][zone.ordinal];
    uint32_t themeRoll = SeededHash((uint32_t)zone.districtIndex, UINT32_C(0xa511e9b3)) % 6u;
    zone.theme = themeRoll < 4u ? (DistrictTheme)themeRoll : DISTRICT_OPEN_VOID;

    float densityNoise = ValueNoise1D((float)zone.districtIndex * 0.37f, 3101);
    float heightNoise = ValueNoise1D((float)zone.districtIndex * 0.23f + 7.0f, 3102);
    float openNoise = ValueNoise1D((float)zone.districtIndex * 0.29f + 19.0f, 3103);
    zone.density = 0.34f + densityNoise * 0.56f;
    zone.openness = Clamp(0.80f - zone.density * 0.48f + openNoise * 0.32f, 0.18f, 0.88f);
    zone.heightScale = 0.72f + heightNoise * 0.88f;
    return zone;
}

static ZoneDescriptor DescribeZoneForSector(int sector) {
    int district = FloorDiv(sector, DISTRICT_SECTORS);
    int localSector = FloorMod(sector, DISTRICT_SECTORS);
    int patternIndex = (int)(SeededHash((uint32_t)district, UINT32_C(0x7f4a7c15)) %
                             (uint32_t)ZONE_PATTERN_COUNT);
    int offset = 0;

    for (int ordinal = 0; ordinal < 4; ordinal++) {
        int length = ZONE_PATTERNS[patternIndex][ordinal];
        if (localSector < offset + length) return DescribeZoneByKey(district * 4 + ordinal);
        offset += length;
    }

    return DescribeZoneByKey(district * 4 + 3);
}

static float LandmarkScore(int zoneKey) {
    return HashFloat(zoneKey, 9094);
}

static bool IsLandmarkZone(int zoneKey) {
    // Landmarks live in one parity class, guaranteeing they cannot repeat in
    // adjacent zones even after they override the normal archetype selection.
    if (FloorMod(zoneKey, 2) == 0) return false;

    float score = LandmarkScore(zoneKey);
    if (score < 0.55f) return false;
    for (int offset = -3; offset <= 3; offset++) {
        if (offset == 0) continue;
        if (LandmarkScore(zoneKey + offset) >= score) return false;
    }
    return true;
}

static bool IsGantryZone(int zoneKey) {
    if (IsLandmarkZone(zoneKey)) return false;
    float score = HashFloat(zoneKey, 9194);
    if (score < 0.52f) return false;
    for (int offset = -2; offset <= 2; offset++) {
        if (offset == 0) continue;
        if (HashFloat(zoneKey + offset, 9194) >= score) return false;
    }
    return true;
}

static void PickFlankPair(const ZoneDescriptor *zone, int *leftArchetype, int *rightArchetype) {
    // Even and odd zones use disjoint archetype families. Immediate repeats are
    // therefore impossible without recursively evaluating earlier zones.
    static const unsigned char choices[DISTRICT_THEME_COUNT][2][8] = {
        { { 0, 6, 2, 8, 8, 6, 4, 6 }, { 1, 1, 3, 5, 7, 5, 1, 3 } },
        { { 2, 6, 4, 6, 8, 6, 2, 4 }, { 5, 5, 3, 7, 1, 5, 3, 7 } },
        { { 2, 6, 4, 8, 0, 2, 4, 6 }, { 1, 3, 5, 1, 7, 3, 5, 1 } },
        { { 4, 6, 8, 4, 2, 6, 4, 6 }, { 7, 7, 3, 5, 1, 7, 3, 5 } },
        { { 6, 6, 0, 6, 6, 8, 4, 6 }, { 5, 7, 1, 5, 3, 7, 5, 1 } }
    };

    int parity = FloorMod(zone->key, 2);
    int leftIndex = (int)(SeededHash((uint32_t)zone->key, UINT32_C(0x53a9d21f)) & 7u);
    int rightIndex = (int)(SeededHash((uint32_t)zone->key, UINT32_C(0xc3e94791)) & 7u);
    *leftArchetype = choices[zone->theme][parity][leftIndex];
    *rightArchetype = choices[zone->theme][parity][rightIndex];

    // Compose the whole frame rather than allowing identical silhouettes on
    // both flanks. Rotate through the same themed family for the second role.
    for (int attempt = 0; attempt < 8 && *rightArchetype == *leftArchetype; attempt++) {
        rightIndex = (rightIndex + 1) & 7;
        *rightArchetype = choices[zone->theme][parity][rightIndex];
    }

    if (IsLandmarkZone(zone->key)) {
        if (HashFloat(zone->key, 9095) < 0.5f) *leftArchetype = 9;
        else *rightArchetype = 9;
    }
}

// Jitter multiplier in range [1.0 - maxVar, 1.0 + maxVar]
static inline float JitterMult(int zoneIdx, int seed, float maxVar) {
    return 1.0f + (HashFloat(zoneIdx, seed) * 2.0f - 1.0f) * maxVar;
}

static bool ShouldSkipFlank(int targetSector, float side) {
    int seed = side < 0.0f ? FLANK_SKIP_LEFT_SEED : FLANK_SKIP_RIGHT_SEED;
    return HashFloat(targetSector, seed) < FLANK_SKIP_CHANCE;
}

// Safely append a structure while reserving space for higher-value layers.
static inline void AddStructurePriority(EnvironmentSystem *env, StructureType type, Vector3 pos, Vector3 size,
                                        float compileScale, EmitPriority priority, float phaseOffset) {
    int serial = env->emissionSerial++;
    float typePhase = 0.0f;
    if (type == STRUCT_CACHE_TOWER) typePhase = 0.07f;
    else if (type == STRUCT_LANDMARK) typePhase = 0.12f;
    else if (type == STRUCT_BUS_CONDUIT) typePhase = 0.18f;
    float jitterPhase = HashFloat(env->emissionSector, 12000 + serial * 17) * 0.07f;
    float gate = Smooth01((compileScale - typePhase - phaseOffset - jitterPhase) / 0.30f);
    if (gate <= 0.001f) return;

    int limit = MAX_STRUCTURES;
    if (priority == EMIT_DETAIL) limit -= 192;
    else if (priority == EMIT_CONNECTION) limit -= 112;
    else if (priority == EMIT_STRUCTURAL) limit -= 32;

    if (env->structureCount >= limit) {
        env->droppedStructures++;
        return;
    }

    // Existing archetypes already apply the broad compile scale. This second,
    // type-aware gate creates staged base/body/conduit assembly and stays
    // anchored to each primitive's lower face.
    float baseY = pos.y - size.y * 0.5f;
    size.y *= gate;
    pos.y = baseY + size.y * 0.5f;

    pos.x = ScaleEnvironmentX(pos.x);
    size.x = ScaleEnvironmentX(size.x);

    int idx = env->structureCount++;
    env->structures[idx].type = type;
    env->structures[idx].position = pos;
    env->structures[idx].size = size;
    env->structures[idx].compileScale = gate;
    env->structures[idx].active = true;
}

static inline void AddStructure(EnvironmentSystem *env, StructureType type, Vector3 pos, Vector3 size, float compileScale) {
    AddStructurePriority(env, type, pos, size, compileScale, EMIT_STRUCTURAL, 0.0f);
}

static inline void AddCriticalStructure(EnvironmentSystem *env, StructureType type, Vector3 pos, Vector3 size,
                                        float compileScale) {
    AddStructurePriority(env, type, pos, size, compileScale, EMIT_CRITICAL, 0.0f);
}

static inline void AddDetailStructure(EnvironmentSystem *env, StructureType type, Vector3 pos, Vector3 size,
                                      float compileScale, float phaseOffset) {
    AddStructurePriority(env, type, pos, size, compileScale, EMIT_DETAIL, phaseOffset);
}

// Exact geometric socket connectors
static inline void AddConduitFeeder(EnvironmentSystem *env, float fromX, float toX, float y, float z, float widthZ, float compileScale) {
    float len = fabsf(toX - fromX);
    if (len < 0.2f) return;
    float midX = (fromX + toX) * 0.5f;
    AddStructurePriority(env, STRUCT_BUS_CONDUIT, (Vector3){ midX, y, z }, (Vector3){ len, 0.45f, widthZ },
                         compileScale, EMIT_CONNECTION, 0.0f);
}

static inline void AddConduitRiser(EnvironmentSystem *env, float x, float z, float fromY, float toY, float compileScale) {
    float h = fabsf(toY - fromY);
    if (h < 0.2f) return;
    float midY = (fromY + toY) * 0.5f;
    AddStructurePriority(env, STRUCT_BUS_CONDUIT, (Vector3){ x, midY, z }, (Vector3){ 0.55f, h, 0.55f },
                         compileScale, EMIT_CONNECTION, 0.0f);
}

static inline void AddConduitSpanZ(EnvironmentSystem *env, float x, float y, float fromZ, float toZ, float compileScale) {
    float lenZ = fabsf(toZ - fromZ);
    if (lenZ < 0.2f) return;
    float midZ = (fromZ + toZ) * 0.5f;
    AddStructurePriority(env, STRUCT_BUS_CONDUIT, (Vector3){ x, y, midZ }, (Vector3){ 0.55f, 0.50f, lenZ },
                         compileScale, EMIT_CONNECTION, 0.0f);
}

// A compact chamfered unit block replaces the razor-edged stock cube. The
// additional faces catch the procedural key light and make every instanced
// tower, slab and conduit read as manufactured hardware instead of placeholder
// boxes, while retaining the same unit bounds and generation data.
static void PushMeshTriangle(Mesh *mesh, int *cursor, Vector3 a, Vector3 b,
                             Vector3 c, Vector3 normal) {
    Vector3 cross = Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a));
    if (Vector3DotProduct(cross, normal) < 0.0f) {
        Vector3 swap = b;
        b = c;
        c = swap;
    }
    Vector3 points[3] = { a, b, c };
    for (int i = 0; i < 3; i++) {
        int vertex = (*cursor)++;
        mesh->vertices[vertex * 3 + 0] = points[i].x;
        mesh->vertices[vertex * 3 + 1] = points[i].y;
        mesh->vertices[vertex * 3 + 2] = points[i].z;
        mesh->normals[vertex * 3 + 0] = normal.x;
        mesh->normals[vertex * 3 + 1] = normal.y;
        mesh->normals[vertex * 3 + 2] = normal.z;
        mesh->texcoords[vertex * 2 + 0] = points[i].x + points[i].z + 0.5f;
        mesh->texcoords[vertex * 2 + 1] = points[i].y + 0.5f;
    }
}

static void PushMeshQuad(Mesh *mesh, int *cursor, Vector3 a, Vector3 b,
                         Vector3 c, Vector3 d, Vector3 normal) {
    PushMeshTriangle(mesh, cursor, a, b, c, normal);
    PushMeshTriangle(mesh, cursor, a, c, d, normal);
}

static Vector3 FacePoint(int axis, float sign, float u, float v) {
    if (axis == 0) return (Vector3){ sign * 0.5f, u, v };
    if (axis == 1) return (Vector3){ u, sign * 0.5f, v };
    return (Vector3){ u, v, sign * 0.5f };
}

static Mesh GenMeshChamferedCube(void) {
    const int capacity = 240;
    const float k = 0.5f;
    const float h = 0.405f;
    Mesh mesh = { 0 };
    mesh.vertices = (float *)MemAlloc((size_t)capacity * 3u * sizeof(float));
    mesh.normals = (float *)MemAlloc((size_t)capacity * 3u * sizeof(float));
    mesh.texcoords = (float *)MemAlloc((size_t)capacity * 2u * sizeof(float));
    int cursor = 0;

    const Vector2 ring[8] = {
        { -h, -k }, { h, -k }, { k, -h }, { k, h },
        { h, k }, { -h, k }, { -k, h }, { -k, -h }
    };
    for (int axis = 0; axis < 3; axis++) {
        for (int side = -1; side <= 1; side += 2) {
            Vector3 normal = { 0 };
            if (axis == 0) normal.x = (float)side;
            else if (axis == 1) normal.y = (float)side;
            else normal.z = (float)side;
            Vector3 center = FacePoint(axis, (float)side, 0.0f, 0.0f);
            for (int i = 0; i < 8; i++) {
                Vector3 a = FacePoint(axis, (float)side, ring[i].x, ring[i].y);
                Vector3 b = FacePoint(axis, (float)side,
                                      ring[(i + 1) & 7].x, ring[(i + 1) & 7].y);
                PushMeshTriangle(&mesh, &cursor, center, a, b, normal);
            }
        }
    }

    for (int sx = -1; sx <= 1; sx += 2) {
        for (int sy = -1; sy <= 1; sy += 2) {
            Vector3 n = Vector3Normalize((Vector3){ (float)sx, (float)sy, 0.0f });
            PushMeshQuad(&mesh, &cursor,
                         (Vector3){ sx * k, sy * h, -h },
                         (Vector3){ sx * h, sy * k, -h },
                         (Vector3){ sx * h, sy * k, h },
                         (Vector3){ sx * k, sy * h, h }, n);
        }
    }
    for (int sx = -1; sx <= 1; sx += 2) {
        for (int sz = -1; sz <= 1; sz += 2) {
            Vector3 n = Vector3Normalize((Vector3){ (float)sx, 0.0f, (float)sz });
            PushMeshQuad(&mesh, &cursor,
                         (Vector3){ sx * k, -h, sz * h },
                         (Vector3){ sx * h, -h, sz * k },
                         (Vector3){ sx * h, h, sz * k },
                         (Vector3){ sx * k, h, sz * h }, n);
        }
    }
    for (int sy = -1; sy <= 1; sy += 2) {
        for (int sz = -1; sz <= 1; sz += 2) {
            Vector3 n = Vector3Normalize((Vector3){ 0.0f, (float)sy, (float)sz });
            PushMeshQuad(&mesh, &cursor,
                         (Vector3){ -h, sy * k, sz * h },
                         (Vector3){ -h, sy * h, sz * k },
                         (Vector3){ h, sy * h, sz * k },
                         (Vector3){ h, sy * k, sz * h }, n);
        }
    }
    for (int sx = -1; sx <= 1; sx += 2) {
        for (int sy = -1; sy <= 1; sy += 2) {
            for (int sz = -1; sz <= 1; sz += 2) {
                Vector3 n = Vector3Normalize((Vector3){ (float)sx, (float)sy, (float)sz });
                PushMeshTriangle(&mesh, &cursor,
                                 (Vector3){ sx * k, sy * h, sz * h },
                                 (Vector3){ sx * h, sy * k, sz * h },
                                 (Vector3){ sx * h, sy * h, sz * k }, n);
            }
        }
    }

    mesh.vertexCount = cursor;
    mesh.triangleCount = cursor / 3;
    UploadMesh(&mesh, false);
    return mesh;
}

static Mesh GenMeshCenteredPrism(int sides) {
    int capacity = sides * 12;
    Mesh mesh = { 0 };
    mesh.vertices = (float *)MemAlloc((size_t)capacity * 3u * sizeof(float));
    mesh.normals = (float *)MemAlloc((size_t)capacity * 3u * sizeof(float));
    mesh.texcoords = (float *)MemAlloc((size_t)capacity * 2u * sizeof(float));
    int cursor = 0;

    for (int side = 0; side < sides; side++) {
        float angleA = (float)side * 6.28318530718f / (float)sides;
        float angleB = (float)(side + 1) * 6.28318530718f / (float)sides;
        Vector3 bottomA = { cosf(angleA) * 0.5f, -0.5f, sinf(angleA) * 0.5f };
        Vector3 bottomB = { cosf(angleB) * 0.5f, -0.5f, sinf(angleB) * 0.5f };
        Vector3 topA = { bottomA.x, 0.5f, bottomA.z };
        Vector3 topB = { bottomB.x, 0.5f, bottomB.z };
        float middle = (angleA + angleB) * 0.5f;
        Vector3 normal = { cosf(middle), 0.0f, sinf(middle) };
        PushMeshQuad(&mesh, &cursor, bottomA, bottomB, topB, topA, normal);
        PushMeshTriangle(&mesh, &cursor, (Vector3){ 0.0f, 0.5f, 0.0f },
                         topA, topB, (Vector3){ 0.0f, 1.0f, 0.0f });
        PushMeshTriangle(&mesh, &cursor, (Vector3){ 0.0f, -0.5f, 0.0f },
                         bottomB, bottomA, (Vector3){ 0.0f, -1.0f, 0.0f });
    }

    mesh.vertexCount = cursor;
    mesh.triangleCount = cursor / 3;
    UploadMesh(&mesh, false);
    return mesh;
}

static void SetEnvironmentSeed(EnvironmentSystem *env, uint32_t runSeed) {
    static const Color palettes[][3] = {
        { { 0, 235, 255, 255 }, { 255, 35, 170, 255 }, { 255, 230, 70, 255 } },
        { { 80, 255, 150, 255 }, { 130, 75, 255, 255 }, { 255, 105, 55, 255 } },
        { { 100, 155, 255, 255 }, { 255, 70, 210, 255 }, { 120, 255, 245, 255 } },
        { { 255, 125, 45, 255 }, { 35, 225, 255, 255 }, { 255, 245, 120, 255 } },
        { { 185, 75, 255, 255 }, { 20, 255, 195, 255 }, { 255, 80, 120, 255 } },
        { { 70, 215, 255, 255 }, { 255, 80, 95, 255 }, { 190, 255, 70, 255 } }
    };
    env->runSeed = runSeed;
    g_environmentSeed = runSeed;
    int palette = (int)(PaletteHash(runSeed ^ UINT32_C(0x94d31a7b)) % 6u);
    env->primaryColor = palettes[palette][0];
    env->secondaryColor = palettes[palette][1];
    env->landmarkColor = palettes[palette][2];
}

void InitEnvironment(EnvironmentSystem *env, uint32_t runSeed) {
    SetEnvironmentSeed(env, runSeed);
    int shaderSeed = (int)runSeed;
    Vector3 primary = { env->primaryColor.r / 255.0f, env->primaryColor.g / 255.0f, env->primaryColor.b / 255.0f };
    Vector3 secondary = { env->secondaryColor.r / 255.0f, env->secondaryColor.g / 255.0f, env->secondaryColor.b / 255.0f };

    // Chamfered unit block in [-0.5, 0.5]; per-instance transforms still use
    // the exact dimensions emitted by the deterministic generator.
    env->unitCubeMesh = GenMeshChamferedCube();
    env->unitPrismMesh = GenMeshCenteredPrism(8);
    env->towerMaterial = LoadMaterialDefault();
    env->towerMaterial.shader = LoadShader("shaders/tower.vs", "shaders/tower.fs");
    env->towerScrollLoc = GetShaderLocation(env->towerMaterial.shader, "virtualPlayerZ");
    env->towerTimeLoc = GetShaderLocation(env->towerMaterial.shader, "uTime");
    env->towerIntensityLoc = GetShaderLocation(env->towerMaterial.shader, "uIntensity");
    env->towerSeedLoc = GetShaderLocation(env->towerMaterial.shader, "uRunSeed");
    env->towerPrimaryLoc = GetShaderLocation(env->towerMaterial.shader, "uPrimaryColor");
    env->towerSecondaryLoc = GetShaderLocation(env->towerMaterial.shader, "uSecondaryColor");
    env->towerAccentLoc = GetShaderLocation(env->towerMaterial.shader, "uAccentColor");
    env->towerBodyLoc = GetShaderLocation(env->towerMaterial.shader, "uBodyColor");
    env->towerKindLoc = GetShaderLocation(env->towerMaterial.shader, "uStructureKind");
    SetShaderValue(env->towerMaterial.shader, env->towerSeedLoc, &shaderSeed, SHADER_UNIFORM_INT);
    SetShaderValue(env->towerMaterial.shader, env->towerPrimaryLoc, &primary, SHADER_UNIFORM_VEC3);
    SetShaderValue(env->towerMaterial.shader, env->towerSecondaryLoc, &secondary, SHADER_UNIFORM_VEC3);

    env->terrainMaterial = LoadMaterialDefault();
    env->terrainMaterial.shader = LoadShader("shaders/terrain.vs", "shaders/terrain.fs");
    env->terrainTimeLoc = GetShaderLocation(env->terrainMaterial.shader, "uTime");
    env->terrainIntensityLoc = GetShaderLocation(env->terrainMaterial.shader, "uIntensity");
    env->terrainSeedLoc = GetShaderLocation(env->terrainMaterial.shader, "uRunSeed");
    env->terrainPrimaryLoc = GetShaderLocation(env->terrainMaterial.shader, "uPrimaryColor");
    env->terrainSecondaryLoc = GetShaderLocation(env->terrainMaterial.shader, "uSecondaryColor");
    SetShaderValue(env->terrainMaterial.shader, env->terrainSeedLoc, &shaderSeed, SHADER_UNIFORM_INT);
    SetShaderValue(env->terrainMaterial.shader, env->terrainPrimaryLoc, &primary, SHADER_UNIFORM_VEC3);
    SetShaderValue(env->terrainMaterial.shader, env->terrainSecondaryLoc, &secondary, SHADER_UNIFORM_VEC3);

    env->structureCount = 0;
    env->terrainCount = 0;
    env->farStructureCount = 0;
    env->droppedStructures = 0;
    env->emissionSector = 0;
    env->emissionSerial = 0;
    for (int i = 0; i < MAX_STRUCTURES; i++) {
        env->structures[i].active = false;
    }
}

void ReseedEnvironment(EnvironmentSystem *env, uint32_t runSeed) {
    SetEnvironmentSeed(env, runSeed);

    int shaderSeed = (int)runSeed;
    Vector3 primary = { env->primaryColor.r / 255.0f, env->primaryColor.g / 255.0f,
                        env->primaryColor.b / 255.0f };
    Vector3 secondary = { env->secondaryColor.r / 255.0f, env->secondaryColor.g / 255.0f,
                          env->secondaryColor.b / 255.0f };
    SetShaderValue(env->towerMaterial.shader, env->towerSeedLoc, &shaderSeed, SHADER_UNIFORM_INT);
    SetShaderValue(env->towerMaterial.shader, env->towerPrimaryLoc, &primary, SHADER_UNIFORM_VEC3);
    SetShaderValue(env->towerMaterial.shader, env->towerSecondaryLoc, &secondary, SHADER_UNIFORM_VEC3);
    SetShaderValue(env->terrainMaterial.shader, env->terrainSeedLoc, &shaderSeed, SHADER_UNIFORM_INT);
    SetShaderValue(env->terrainMaterial.shader, env->terrainPrimaryLoc, &primary, SHADER_UNIFORM_VEC3);
    SetShaderValue(env->terrainMaterial.shader, env->terrainSecondaryLoc, &secondary, SHADER_UNIFORM_VEC3);

    env->structureCount = 0;
    env->terrainCount = 0;
    env->farStructureCount = 0;
    env->droppedStructures = 0;
    env->emissionSector = 0;
    env->emissionSerial = 0;
}

// Procedurally generate a single flank (left or right) for a sector based on its macro-zone archetype
static void GenerateFlank(EnvironmentSystem *env, float side, int targetSector, const ZoneDescriptor *zone,
                          int sectorInZone, int archetype, float relativeZ, float sectorCenterZ,
                          float compileScale) {
    if (ShouldSkipFlank(targetSector, side)) return;

    int zoneIdx = zone->key;
    float hwyX = side * 14.1f; // Outermost line of the 3-lane trench highway
    float flankShift = zone->openness * 9.0f;
    float heightScale = zone->heightScale;

    switch (archetype) {
        // =========================================================================
        // ARCHETYPE 0: DIMM RAM BANKS (Dense rows of modular memory sticks)
        // =========================================================================
        case 0: {
            float jBank = JitterMult(zoneIdx, 1010, 0.08f);
            float jWidth = JitterMult(zoneIdx, 1011, 0.15f);
            float bankX = side * ((18.5f + FLANK_OUTWARD_SHIFT + flankShift) * jBank);
            float socketW = 8.5f * jWidth;
            
            // Continuous motherboard socket rail along the base
            AddStructure(env, STRUCT_MEMORY_SLAB, (Vector3){ bankX, -2.6f, sectorCenterZ },
                         (Vector3){ socketW, 0.55f, SECTOR_DEPTH }, compileScale);

            // 4 vertical memory modules per sector (forms 16 aligned sticks across the 4-sector zone)
            for (int m = 0; m < 4; m++) {
                float modZ = relativeZ - (m * 4.0f + 2.0f);
                float stickH = (3.8f + HashFloat(zoneIdx * 10 + sectorInZone, 100 + m) * 2.2f) *
                               heightScale * compileScale;
                float stickW = 7.0f * jWidth;
                float stickD = 1.1f * JitterMult(zoneIdx, 1012 + m, 0.18f);

                AddStructure(env, STRUCT_MEMORY_SLAB, (Vector3){ bankX, -2.3f + stickH * 0.5f, modZ },
                             (Vector3){ stickW, stickH, stickD }, compileScale);

                // Feeder conduit from highway into stick base socket
                AddConduitFeeder(env, hwyX, bankX - side * (stickW * 0.5f), -2.5f, modZ, 0.6f, compileScale);

                // Rear feeder conduit connecting stick to rear bus
                float rearBusX = bankX + side * (5.0f * jBank);
                AddConduitFeeder(env, bankX + side * (stickW * 0.5f), rearBusX, -2.5f, modZ, 0.6f, compileScale);
            }

            // Rear parallel bus conduit
            AddStructure(env, STRUCT_BUS_CONDUIT, (Vector3){ bankX + side * (5.0f * jBank), -2.6f, sectorCenterZ },
                         (Vector3){ 0.55f, 0.55f, SECTOR_DEPTH }, compileScale);

            // Overhead daisy-chain conduit linking the module peaks
            AddConduitSpanZ(env, bankX, -2.3f + 3.8f * compileScale, relativeZ - 2.0f, relativeZ - 14.0f, compileScale);
            break;
        }

        // =========================================================================
        // ARCHETYPE 1: MAINFRAME SERVER RACKS (Chassis cabinets with heat-sink crowns)
        // =========================================================================
        case 1: {
            float jRack = JitterMult(zoneIdx, 1020, 0.10f);
            float rackX = side * ((20.0f + flankShift) * jRack);
            float rackW = 8.5f * JitterMult(zoneIdx, 1021, 0.15f);
            float rackD = 5.2f * JitterMult(zoneIdx, 1022, 0.18f);

            for (int m = 0; m < 2; m++) {
                float rackZ = relativeZ - (m * 8.0f + 4.0f);
                float rackH = (5.5f + HashFloat(targetSector, 200 + m) * 3.5f) * heightScale * compileScale;

                // Main server chassis
                AddStructure(env, STRUCT_MEMORY_SLAB, (Vector3){ rackX, -1.8f + rackH * 0.5f, rackZ },
                             (Vector3){ rackW, rackH, rackD }, compileScale);

                // Heat sink crown on top of chassis
                AddStructure(env, STRUCT_CACHE_TOWER, (Vector3){ rackX, -1.8f + rackH + (0.6f * compileScale), rackZ },
                             (Vector3){ rackW * 0.8f, 1.2f * compileScale, rackD * 0.8f }, compileScale);

                // Feeder conduit from trench highway to front face of server rack
                float frontFaceX = rackX - side * (rackW * 0.5f);
                AddConduitFeeder(env, hwyX, frontFaceX, -2.4f, rackZ, 1.2f, compileScale);

                // Vertical riser conduit ascending rack front face
                AddConduitRiser(env, frontFaceX - side * 0.3f, rackZ, -2.4f, -1.8f + rackH * 0.75f, compileScale);
            }

            // Daisy-chain conduit connecting the two chassis mid-height
            AddConduitSpanZ(env, rackX, -1.8f + 2.5f * compileScale, relativeZ - 4.0f, relativeZ - 12.0f, compileScale);
            break;
        }

        // =========================================================================
        // ARCHETYPE 2: INTEGRATED PROCESSOR DIE (Large square BGA package & traces)
        // =========================================================================
        case 2: {
            float jDie = JitterMult(zoneIdx, 1030, 0.08f);
            float dieX = side * ((23.0f + flankShift) * jDie);
            float dieSize = 13.5f * JitterMult(zoneIdx, 1031, 0.15f);

            float zonePhase = ((float)sectorInZone + 0.5f) / (float)zone->length;
            if (zonePhase > 0.20f && zonePhase < 0.80f) {
                // Central microchip die
                float substrateH = 1.4f * heightScale * compileScale;
                float coreH = 3.0f * heightScale * compileScale;
                float coreSize = dieSize * 0.55f;

                // Substrate base
                AddStructure(env, STRUCT_MEMORY_SLAB, (Vector3){ dieX, -2.4f + substrateH * 0.5f, sectorCenterZ },
                             (Vector3){ dieSize, substrateH, dieSize }, compileScale);

                // Silicon core die on top
                AddStructure(env, STRUCT_CACHE_TOWER, (Vector3){ dieX, -2.4f + substrateH + coreH * 0.5f, sectorCenterZ },
                             (Vector3){ coreSize, coreH, coreSize }, compileScale);

                // 4 Corner Decoupling Capacitors
                float capH = 4.0f * heightScale * compileScale;
                float capOff = dieSize * 0.38f;
                AddStructure(env, STRUCT_MEMORY_SLAB, (Vector3){ dieX - capOff, -2.4f + capH * 0.5f, sectorCenterZ - capOff },
                             (Vector3){ 2.2f, capH, 2.2f }, compileScale);
                AddStructure(env, STRUCT_MEMORY_SLAB, (Vector3){ dieX + capOff, -2.4f + capH * 0.5f, sectorCenterZ - capOff },
                             (Vector3){ 2.2f, capH, 2.2f }, compileScale);
                AddStructure(env, STRUCT_MEMORY_SLAB, (Vector3){ dieX - capOff, -2.4f + capH * 0.5f, sectorCenterZ + capOff },
                             (Vector3){ 2.2f, capH, 2.2f }, compileScale);
                AddStructure(env, STRUCT_MEMORY_SLAB, (Vector3){ dieX + capOff, -2.4f + capH * 0.5f, sectorCenterZ + capOff },
                             (Vector3){ 2.2f, capH, 2.2f }, compileScale);

                // 3 Radiating pin bus traces from die to trench highway
                AddConduitFeeder(env, hwyX, dieX - side * (dieSize * 0.5f), -2.5f, sectorCenterZ - 3.5f, 0.7f, compileScale);
                AddConduitFeeder(env, hwyX, dieX - side * (dieSize * 0.5f), -2.5f, sectorCenterZ,        0.9f, compileScale);
                AddConduitFeeder(env, hwyX, dieX - side * (dieSize * 0.5f), -2.5f, sectorCenterZ + 3.5f, 0.7f, compileScale);

                // Outer bus connection
                AddConduitFeeder(env, dieX + side * (dieSize * 0.5f), side * 34.0f, -2.5f, sectorCenterZ, 0.9f, compileScale);
            } else {
                // Lead-in / lead-out auxiliary trace corridor
                AddStructure(env, STRUCT_BUS_CONDUIT, (Vector3){ dieX, -2.6f, sectorCenterZ },
                             (Vector3){ 0.6f, 0.6f, SECTOR_DEPTH }, compileScale);
                AddStructure(env, STRUCT_MEMORY_SLAB, (Vector3){ side * ((19.0f + FLANK_OUTWARD_SHIFT + flankShift) * jDie), -2.0f, sectorCenterZ },
                             (Vector3){ 6.0f, 1.2f * compileScale, 5.0f }, compileScale);
                AddConduitFeeder(env, hwyX, side * ((16.0f + FLANK_OUTWARD_SHIFT + flankShift) * jDie), -2.5f, sectorCenterZ, 0.8f, compileScale);
            }
            break;
        }

        // =========================================================================
        // ARCHETYPE 3: TRANSFORMER / CAPACITOR SUBSTATION (Power grid matrix)
        // =========================================================================
        case 3: {
            float jDist = JitterMult(zoneIdx, 1040, 0.10f);
            float node0X = side * ((16.5f + FLANK_OUTWARD_SHIFT + flankShift) * jDist);
            float node0Z = relativeZ - 3.5f;
            float node0H = (5.5f + HashFloat(targetSector, 301) * 2.5f) * heightScale * compileScale;
            float node0W = 3.6f * JitterMult(zoneIdx, 1041, 0.18f);
            AddStructure(env, STRUCT_CACHE_TOWER, (Vector3){ node0X, -1.8f + node0H * 0.5f, node0Z },
                         (Vector3){ node0W, node0H, node0W }, compileScale);

            float node1X = side * ((22.5f + flankShift) * jDist);
            float node1Z = relativeZ - 8.0f;
            float node1H = (8.0f + HashFloat(targetSector, 302) * 3.5f) * heightScale * compileScale;
            float node1W = 4.5f * JitterMult(zoneIdx, 1042, 0.18f);
            AddStructure(env, STRUCT_CACHE_TOWER, (Vector3){ node1X, -1.8f + node1H * 0.5f, node1Z },
                         (Vector3){ node1W, node1H, node1W }, compileScale);

            float node2X = side * ((17.0f + FLANK_OUTWARD_SHIFT + flankShift) * jDist);
            float node2Z = relativeZ - 12.5f;
            float node2H = (5.0f + HashFloat(targetSector, 303) * 2.0f) * heightScale * compileScale;
            float node2W = 3.6f * JitterMult(zoneIdx, 1043, 0.18f);
            AddStructure(env, STRUCT_CACHE_TOWER, (Vector3){ node2X, -1.8f + node2H * 0.5f, node2Z },
                         (Vector3){ node2W, node2H, node2W }, compileScale);

            // Elevated bus conduits linking node 0 -> 1 -> 2
            AddConduitFeeder(env, node0X, node1X, -1.8f + node0H * 0.85f, (node0Z + node1Z) * 0.5f, 0.8f, compileScale);
            AddConduitFeeder(env, node1X, node2X, -1.8f + node2H * 0.85f, (node1Z + node2Z) * 0.5f, 0.8f, compileScale);

            // Feeder conduits connecting nodes to highway
            AddConduitFeeder(env, hwyX, node0X - side * (node0W * 0.5f), -2.5f, node0Z, 0.6f, compileScale);
            AddConduitFeeder(env, hwyX, node2X - side * (node2W * 0.5f), -2.5f, node2Z, 0.6f, compileScale);
            break;
        }

        // =========================================================================
        // ARCHETYPE 4: MONOLITH MEGATOWER DISTRICT (Sky-scraping towers & skybridges)
        // =========================================================================
        case 4: {
            float jTow = JitterMult(zoneIdx, 1050, 0.10f);
            float towerX = side * ((36.0f + flankShift) * jTow + HashFloat(targetSector, 401) * 10.0f);
            float towerH = (32.0f + HashFloat(targetSector, 402) * 36.0f) * heightScale * compileScale;
            float towerW = 9.0f * JitterMult(zoneIdx, 1051, 0.18f);

            // Outer Mega-Monolith
            AddStructure(env, STRUCT_CACHE_TOWER, (Vector3){ towerX, -1.5f + towerH * 0.5f, sectorCenterZ },
                         (Vector3){ towerW, towerH, towerW }, compileScale);

            // Midground relay pillar
            float midX = side * ((22.5f + flankShift) * jTow);
            float midH = (12.0f + HashFloat(targetSector, 403) * 8.0f) * heightScale * compileScale;
            float midW = 5.5f * JitterMult(zoneIdx, 1052, 0.18f);
            AddStructure(env, STRUCT_CACHE_TOWER, (Vector3){ midX, -1.8f + midH * 0.5f, sectorCenterZ },
                         (Vector3){ midW, midH, midW }, compileScale);

            // Inner pedestal block
            float innerX = side * ((16.0f + FLANK_OUTWARD_SHIFT + flankShift) * jTow);
            float innerH = (3.5f + HashFloat(targetSector, 404) * 2.5f) * heightScale * compileScale;
            float innerW = 4.2f * JitterMult(zoneIdx, 1053, 0.18f);
            AddStructure(env, STRUCT_MEMORY_SLAB, (Vector3){ innerX, -2.2f + innerH * 0.5f, sectorCenterZ },
                         (Vector3){ innerW, innerH, innerW }, compileScale);

            // High-altitude skybridge from monolith to midground relay
            AddConduitFeeder(env, midX, towerX, -1.8f + midH * 0.85f, sectorCenterZ, 1.2f, compileScale);

            // Lower bridge from midground relay to inner pedestal
            AddConduitFeeder(env, innerX, midX, -2.2f + innerH * 0.85f, sectorCenterZ, 0.9f, compileScale);

            // Feeder from inner pedestal into trench highway
            AddConduitFeeder(env, hwyX, innerX - side * (innerW * 0.5f), -2.5f, sectorCenterZ, 0.8f, compileScale);
            break;
        }

        // =========================================================================
        // ARCHETYPE 5: CONDUIT EXCHANGE (5-lane multi-ribbon routing interchange)
        // =========================================================================
        case 5: {
            float jCon = JitterMult(zoneIdx, 1060, 0.08f);
            float line1X = side * ((15.6f + FLANK_OUTWARD_SHIFT + flankShift) * jCon);
            float line2X = side * ((17.2f + FLANK_OUTWARD_SHIFT + flankShift) * jCon);

            // Parallel high-speed data ribbons
            AddStructure(env, STRUCT_BUS_CONDUIT, (Vector3){ line1X, -2.6f, sectorCenterZ },
                         (Vector3){ 0.50f, 0.50f, SECTOR_DEPTH }, compileScale);
            AddStructure(env, STRUCT_BUS_CONDUIT, (Vector3){ line2X, -2.6f, sectorCenterZ },
                         (Vector3){ 0.50f, 0.50f, SECTOR_DEPTH }, compileScale);

            // Central switching logic hub block
            float hubX = side * ((19.8f + FLANK_OUTWARD_SHIFT + flankShift) * jCon);
            float hubW = 6.5f * JitterMult(zoneIdx, 1061, 0.18f);
            AddStructure(env, STRUCT_MEMORY_SLAB, (Vector3){ hubX, -2.2f, sectorCenterZ },
                         (Vector3){ hubW, 1.2f * compileScale, 4.0f }, compileScale);

            // Intersecting diagonal bus crossing between highway and hub
            AddConduitFeeder(env, hwyX, hubX, -2.4f, sectorCenterZ - 3.5f, 0.7f, compileScale);
            AddConduitFeeder(env, hwyX, hubX, -2.4f, sectorCenterZ + 3.5f, 0.7f, compileScale);
            break;
        }

        // =========================================================================
        // ARCHETYPE 6: SPARSE CORRIDOR (Visual negative space & telemetry nodes)
        // =========================================================================
        case 6: {
            float jSparse = JitterMult(zoneIdx, 1070, 0.10f);
            float nodeX = side * ((17.5f + FLANK_OUTWARD_SHIFT + flankShift) * jSparse);
            float nodeW = 3.0f * JitterMult(zoneIdx, 1071, 0.20f);
            float nodeH = (1.8f + HashFloat(targetSector, 601) * 1.5f) * heightScale * compileScale;
            AddStructure(env, STRUCT_MEMORY_SLAB, (Vector3){ nodeX, -2.4f + nodeH * 0.5f, sectorCenterZ },
                         (Vector3){ nodeW, nodeH, nodeW }, compileScale);

            // Ground tap line to highway
            AddConduitFeeder(env, hwyX, nodeX - side * (nodeW * 0.5f), -2.5f, sectorCenterZ, 0.6f, compileScale);
            break;
        }

        // =========================================================================
        // ARCHETYPE 7: ANTENNA SPIKE FIELD (Thin, tall spires & grounding slabs)
        // =========================================================================
        case 7: {
            float jSpike = JitterMult(zoneIdx, 1080, 0.10f);
            int spikeCount = (zone->density > 0.62f) ? 3 : 2;
            for (int s = 0; s < spikeCount; s++) {
                float spkZ = relativeZ - (s * 5.0f + 2.5f);
                float spkX = side * (17.0f + FLANK_OUTWARD_SHIFT + flankShift + s * 4.0f + HashFloat(targetSector, 710 + s) * 3.0f) * jSpike;
                float spkH = (16.0f + HashFloat(targetSector, 720 + s) * 20.0f) * heightScale * compileScale;
                float spkW = (1.2f + HashFloat(targetSector, 730 + s) * 0.5f) * compileScale;

                // Spire needle
                AddStructure(env, STRUCT_CACHE_TOWER, (Vector3){ spkX, -2.4f + spkH * 0.5f, spkZ },
                             (Vector3){ spkW, spkH, spkW }, compileScale);

                // Base collar slab
                AddStructure(env, STRUCT_MEMORY_SLAB, (Vector3){ spkX, -2.4f + 0.4f * compileScale, spkZ },
                             (Vector3){ spkW * 2.6f, 0.8f * compileScale, spkW * 2.6f }, compileScale);

                // Ground feeder line to highway
                AddConduitFeeder(env, hwyX, spkX - side * (spkW * 1.3f), -2.5f, spkZ, 0.5f, compileScale);
            }
            break;
        }

        // =========================================================================
        // ARCHETYPE 8: SERVER CRATE MAZE (Dense modular computing blocks)
        // =========================================================================
        case 8: {
            float jCrate = JitterMult(zoneIdx, 1090, 0.10f);
            float baseRackX = side * (18.0f + FLANK_OUTWARD_SHIFT + flankShift) * jCrate;
            int crateColumns = (zone->density > 0.70f) ? 2 : 1;
            for (int cx = 0; cx < crateColumns; cx++) {
                for (int cz = 0; cz < 3; cz++) {
                    float cZ = relativeZ - (cz * 5.0f + 2.5f);
                    float cX = baseRackX + side * (cx * 5.2f * jCrate);
                    float cH = (2.2f + HashFloat(targetSector, 810 + cx * 3 + cz) * 3.6f) * heightScale * compileScale;
                    float cW = (3.4f + HashFloat(targetSector, 830 + cx * 3 + cz) * 1.0f) * compileScale;
                    float cD = (3.4f + HashFloat(targetSector, 850 + cx * 3 + cz) * 1.0f) * compileScale;

                    AddStructure(env, STRUCT_MEMORY_SLAB, (Vector3){ cX, -2.4f + cH * 0.5f, cZ },
                                 (Vector3){ cW, cH, cD }, compileScale);

                    // Cap tower accent on taller blocks
                    if (cH > 3.8f * compileScale) {
                        AddStructure(env, STRUCT_CACHE_TOWER, (Vector3){ cX, -2.4f + cH + 0.35f * compileScale, cZ },
                                     (Vector3){ cW * 0.65f, 0.7f * compileScale, cD * 0.65f }, compileScale);
                    }
                }
            }
            AddConduitFeeder(env, hwyX, baseRackX, -2.5f, sectorCenterZ, 0.8f, compileScale);
            break;
        }

        // =========================================================================
        // ARCHETYPE 9: SUSPENDED LANDMARK BEACON (Rare magenta monolith showpiece)
        // =========================================================================
        case 9: {
            float jBeacon = JitterMult(zoneIdx, 1100, 0.10f);
            float beaconX = side * ((26.0f + flankShift) * jBeacon);
            float beaconH = (36.0f + HashFloat(targetSector, 901) * 22.0f) * heightScale * compileScale;
            float beaconW = (10.0f + HashFloat(targetSector, 902) * 4.0f) * compileScale;

            // Core Monolithic Landmark (STRUCT_LANDMARK - magenta accent)
            AddCriticalStructure(env, STRUCT_LANDMARK, (Vector3){ beaconX, -1.2f + beaconH * 0.5f, sectorCenterZ },
                                 (Vector3){ beaconW, beaconH, beaconW }, compileScale);

            // Surrounding stabilizer pylons (STRUCT_CACHE_TOWER)
            float pylonH = beaconH * 0.45f;
            float pylonOff = beaconW * 0.85f;
            AddStructure(env, STRUCT_CACHE_TOWER, (Vector3){ beaconX - pylonOff, -2.2f + pylonH * 0.5f, sectorCenterZ - pylonOff },
                         (Vector3){ 3.2f, pylonH, 3.2f }, compileScale);
            AddStructure(env, STRUCT_CACHE_TOWER, (Vector3){ beaconX + pylonOff, -2.2f + pylonH * 0.5f, sectorCenterZ - pylonOff },
                         (Vector3){ 3.2f, pylonH, 3.2f }, compileScale);
            AddStructure(env, STRUCT_CACHE_TOWER, (Vector3){ beaconX - pylonOff, -2.2f + pylonH * 0.5f, sectorCenterZ + pylonOff },
                         (Vector3){ 3.2f, pylonH, 3.2f }, compileScale);
            AddStructure(env, STRUCT_CACHE_TOWER, (Vector3){ beaconX + pylonOff, -2.2f + pylonH * 0.5f, sectorCenterZ + pylonOff },
                         (Vector3){ 3.2f, pylonH, 3.2f }, compileScale);

            // High-altitude skybridge from landmark to trench highway
            AddConduitFeeder(env, hwyX, beaconX - side * (beaconW * 0.5f), -2.0f + 2.5f * compileScale, sectorCenterZ, 1.4f, compileScale);
            break;
        }

        default:
            break;
    }
}

// A compact composable grammar layered over the district archetype. Foundations,
// stacked bodies, crowns, attachments, and network links are independently
// derived, producing many silhouettes without adding meshes or asset data.
static void GenerateModularSilhouette(EnvironmentSystem *env, float side, const ZoneDescriptor *zone,
                                      int sectorInZone, float sectorCenterZ, float compileScale) {
    if (sectorInZone != zone->length / 2) return;

    int sideSeed = (side < 0.0f) ? 4101 : 4202;
    float spawnChance = 0.18f + zone->density * 0.42f;
    if (HashFloat(zone->key, sideSeed) > spawnChance) return;

    float baseX = side * (27.0f + zone->openness * 14.0f + HashFloat(zone->key, sideSeed + 1) * 8.0f);
    float baseW = 6.5f + HashFloat(zone->key, sideSeed + 2) * 6.0f;
    float baseD = 5.5f + HashFloat(zone->key, sideSeed + 3) * 7.0f;
    float foundationH = 0.8f * compileScale;

    AddStructurePriority(env, STRUCT_MEMORY_SLAB,
                         (Vector3){ baseX, -2.45f + foundationH * 0.5f, sectorCenterZ },
                         (Vector3){ baseW * 1.18f, foundationH, baseD * 1.18f },
                         compileScale, EMIT_STRUCTURAL, 0.0f);

    int bodySegments = 1 + (int)(HashFloat(zone->key, sideSeed + 4) * 3.0f);
    if (bodySegments > 3) bodySegments = 3;
    float totalHeight = (9.0f + HashFloat(zone->key, sideSeed + 5) * 18.0f) *
                        zone->heightScale * compileScale;
    float segmentH = totalHeight / (float)bodySegments;
    float bodyBottom = -2.45f + foundationH;
    StructureType bodyType = (zone->theme == DISTRICT_MEMORY_CITY || zone->theme == DISTRICT_OPEN_VOID)
                                 ? STRUCT_MEMORY_SLAB
                                 : STRUCT_CACHE_TOWER;

    for (int segment = 0; segment < bodySegments; segment++) {
        float taper = 1.0f - 0.12f * (float)segment;
        float twist = (HashFloat(zone->key, sideSeed + 20 + segment) - 0.5f) * baseW * 0.24f;
        float y = bodyBottom + segmentH * ((float)segment + 0.5f);
        AddStructurePriority(env, bodyType,
                             (Vector3){ baseX + side * twist, y, sectorCenterZ },
                             (Vector3){ baseW * taper, segmentH * 0.92f, baseD * taper },
                             compileScale, EMIT_STRUCTURAL, 0.04f + 0.045f * (float)segment);
    }

    // Crown profile: broad heat sink, narrow antenna, or offset processing cap.
    int crown = (int)(HashFloat(zone->key, sideSeed + 6) * 3.0f);
    float crownY = bodyBottom + totalHeight;
    if (crown == 0) {
        AddDetailStructure(env, STRUCT_CACHE_TOWER, (Vector3){ baseX, crownY + 0.45f, sectorCenterZ },
                           (Vector3){ baseW * 1.12f, 0.9f, baseD * 0.78f }, compileScale, 0.16f);
    } else if (crown == 1) {
        AddDetailStructure(env, STRUCT_CACHE_TOWER, (Vector3){ baseX, crownY + 3.0f, sectorCenterZ },
                           (Vector3){ 0.75f, 6.0f, 0.75f }, compileScale, 0.18f);
    } else {
        AddDetailStructure(env, STRUCT_MEMORY_SLAB,
                           (Vector3){ baseX + side * baseW * 0.25f, crownY + 0.65f, sectorCenterZ },
                           (Vector3){ baseW * 0.62f, 1.3f, baseD * 0.62f }, compileScale, 0.17f);
    }

    if (zone->density > 0.58f) {
        float ribH = totalHeight * 0.42f;
        float ribY = bodyBottom + ribH * 0.5f;
        AddDetailStructure(env, STRUCT_MEMORY_SLAB,
                           (Vector3){ baseX - side * baseW * 0.64f, ribY, sectorCenterZ },
                           (Vector3){ 0.65f, ribH, baseD * 0.75f }, compileScale, 0.10f);
        AddDetailStructure(env, STRUCT_MEMORY_SLAB,
                           (Vector3){ baseX + side * baseW * 0.64f, ribY, sectorCenterZ },
                           (Vector3){ 0.65f, ribH, baseD * 0.75f }, compileScale, 0.12f);
    }

    AddConduitFeeder(env, side * 14.1f, baseX - side * baseW * 0.6f, -2.5f,
                     sectorCenterZ, 0.75f, compileScale);
}

static void GenerateRavineObject(EnvironmentSystem *env, int targetSector,
                                 float sectorCenterZ, float compileScale) {
    if (HashFloat(targetSector, 7701) < 0.80f) return;

    int firstCell = -1 + (int)(HashFloat(targetSector, 7702) * 3.0f);
    int supportCell = 100;
    TerrainSupport support = { 0 };
    for (int offset = 0; offset < 3; offset++) {
        int cellX = -1 + FloorMod(firstCell + 1 + offset, 3);
        TerrainSupport candidate = SampleTerrainSupport(cellX, targetSector);
        if (!candidate.occupied) continue;
        supportCell = cellX;
        support = candidate;
        break;
    }
    if (supportCell == 100) return;

    float height = (2.0f + HashFloat(targetSector, 7703) * 5.0f) * compileScale;
    float width = (2.4f + HashFloat(targetSector, 7704) * 3.2f) * compileScale;
    StructureType type = HashFloat(targetSector, 7705) < 0.64f ? STRUCT_MEMORY_SLAB
                                                               : STRUCT_CACHE_TOWER;
    AddDetailStructure(env, type,
                       (Vector3){ (float)supportCell * TERRAIN_CELL_WIDTH,
                                  support.topY - 0.3f + height * 0.5f,
                                  sectorCenterZ },
                       (Vector3){ width, height, width * 0.84f }, compileScale, 0.04f);
}

static void GenerateEnvironmentStructures(EnvironmentSystem *env, float virtualPlayerZ) {
    env->structureCount = 0;
    env->droppedStructures = 0;

    int currentSector = (int)floorf(virtualPlayerZ / SECTOR_DEPTH);
    int visibleSectors = (int)(DRAW_DISTANCE / SECTOR_DEPTH) + 2;

    for (int i = 0; i < visibleSectors; i++) {
        int targetSector = currentSector + i;
        float worldZ = (float)targetSector * SECTOR_DEPTH;
        float relativeZ = -(worldZ - virtualPlayerZ); // Negative Z is forward

        if (relativeZ > 10.0f || relativeZ < -DRAW_DISTANCE - 16.0f) continue;

        // JIT Horizon scaling factor (0.0 at far clip, 1.0 approaching player)
        float depthFactor = 1.0f - (-relativeZ / DRAW_DISTANCE);
        float compileScale = Clamp(depthFactor * 1.5f, 0.0f, 1.0f);
        if (compileScale <= 0.01f) continue;

        float sectorCenterZ = relativeZ - (SECTOR_DEPTH * 0.5f);
        env->emissionSector = targetSector;
        env->emissionSerial = 0;

        // =========================================================================
        // 1. CONTINUOUS 3-LANE PARALLEL HIGHWAY CONDUITS (Corridor Spine)
        // =========================================================================
        float leftInnerY = SampleTerrainSupport(TerrainCellForX(ScaleEnvironmentX(-11.5f)), targetSector).topY + 0.25f;
        float leftMiddleY = SampleTerrainSupport(TerrainCellForX(ScaleEnvironmentX(-12.8f)), targetSector).topY + 0.25f;
        float leftOuterY = SampleTerrainSupport(TerrainCellForX(ScaleEnvironmentX(-14.1f)), targetSector).topY + 0.20f;
        float rightInnerY = SampleTerrainSupport(TerrainCellForX(ScaleEnvironmentX(11.5f)), targetSector).topY + 0.25f;
        float rightMiddleY = SampleTerrainSupport(TerrainCellForX(ScaleEnvironmentX(12.8f)), targetSector).topY + 0.25f;
        float rightOuterY = SampleTerrainSupport(TerrainCellForX(ScaleEnvironmentX(14.1f)), targetSector).topY + 0.20f;

        // Left parallel highway lines
        AddCriticalStructure(env, STRUCT_BUS_CONDUIT, (Vector3){ -11.5f, leftInnerY, sectorCenterZ }, (Vector3){ 0.50f, 0.50f, SECTOR_DEPTH }, compileScale);
        AddCriticalStructure(env, STRUCT_BUS_CONDUIT, (Vector3){ -12.8f, leftMiddleY, sectorCenterZ }, (Vector3){ 0.50f, 0.50f, SECTOR_DEPTH }, compileScale);
        AddCriticalStructure(env, STRUCT_BUS_CONDUIT, (Vector3){ -14.1f, leftOuterY, sectorCenterZ }, (Vector3){ 0.40f, 0.40f, SECTOR_DEPTH }, compileScale);

        // Right parallel highway lines
        AddCriticalStructure(env, STRUCT_BUS_CONDUIT, (Vector3){  11.5f, rightInnerY, sectorCenterZ }, (Vector3){ 0.50f, 0.50f, SECTOR_DEPTH }, compileScale);
        AddCriticalStructure(env, STRUCT_BUS_CONDUIT, (Vector3){  12.8f, rightMiddleY, sectorCenterZ }, (Vector3){ 0.50f, 0.50f, SECTOR_DEPTH }, compileScale);
        AddCriticalStructure(env, STRUCT_BUS_CONDUIT, (Vector3){  14.1f, rightOuterY, sectorCenterZ }, (Vector3){ 0.40f, 0.40f, SECTOR_DEPTH }, compileScale);

        // =========================================================================
        // 2. DISTRICT-COMPOSED PROCEDURAL FLANKS
        // =========================================================================
        ZoneDescriptor zone = DescribeZoneForSector(targetSector);
        int sectorInZone = targetSector - zone.startSector;
        int leftArchetype = 0;
        int rightArchetype = 0;
        PickFlankPair(&zone, &leftArchetype, &rightArchetype);

        GenerateFlank(env, -1.0f, targetSector, &zone, sectorInZone, leftArchetype,
                      relativeZ, sectorCenterZ, compileScale);
        GenerateFlank(env,  1.0f, targetSector, &zone, sectorInZone, rightArchetype,
                      relativeZ, sectorCenterZ, compileScale);
        if (!ShouldSkipFlank(targetSector, -1.0f)) {
            GenerateModularSilhouette(env, -1.0f, &zone, sectorInZone, sectorCenterZ, compileScale);
        }
        if (!ShouldSkipFlank(targetSector, 1.0f)) {
            GenerateModularSilhouette(env, 1.0f, &zone, sectorInZone, sectorCenterZ, compileScale);
        }
        GenerateRavineObject(env, targetSector, sectorCenterZ, compileScale);

        // =========================================================================
        // 3. CONTEXT-AWARE ZONE BOUNDARY COUPLERS
        // =========================================================================
        if (sectorInZone == 0) {
            ZoneDescriptor previous = DescribeZoneByKey(zone.key - 1);
            float transitionOpen = (previous.openness + zone.openness) * 0.5f;
            float anchorX = 16.0f + transitionOpen * 7.0f;
            AddConduitSpanZ(env, -anchorX, -2.4f, relativeZ + 2.0f, relativeZ - 2.0f, compileScale);
            AddConduitFeeder(env, -14.1f, -anchorX, -2.5f, relativeZ, 0.7f, compileScale);

            AddConduitSpanZ(env,  anchorX, -2.4f, relativeZ + 2.0f, relativeZ - 2.0f, compileScale);
            AddConduitFeeder(env,  14.1f,  anchorX, -2.5f, relativeZ, 0.7f, compileScale);

            if (HashFloat(zone.key, 16101) < 0.30f) {
                float crossAnchorX = 25.0f + transitionOpen * 8.0f;
                float crossY = 7.0f + zone.heightScale * 4.0f;
                AddConduitFeeder(env, -crossAnchorX, crossAnchorX, crossY,
                                 relativeZ, 0.9f, compileScale);
            }
        }

        if (zone.ordinal == 0 && sectorInZone == 0 &&
            HashFloat(zone.districtIndex, 16201) < 0.68f) {
            float trunkSide = HashFloat(zone.districtIndex, 16202) < 0.5f ? -1.0f : 1.0f;
            float trunkX = trunkSide * (28.0f + zone.openness * 10.0f +
                                        HashFloat(zone.districtIndex, 16203) * 6.0f);
            AddConduitSpanZ(env, trunkX, -2.45f, relativeZ,
                            relativeZ - DISTRICT_SECTORS * SECTOR_DEPTH, compileScale);
            AddConduitFeeder(env, trunkSide * 14.1f, trunkX, -2.5f,
                             relativeZ, 0.8f, compileScale);
        }

        // =========================================================================
        // 4. BLUE-NOISE GANTRY ARCHWAYS (Local maxima, not periodic spacing)
        // =========================================================================
        if (sectorInZone == zone.length / 2 && IsGantryZone(zone.key)) {
            float archH = 12.5f * zone.heightScale * compileScale;
            float pillarW = 1.4f;
            float archTop = -2.5f + archH;
            float leftSupportY = SampleTerrainSupport(TerrainCellForX(-13.0f), targetSector).topY;
            float rightSupportY = SampleTerrainSupport(TerrainCellForX(13.0f), targetSector).topY;
            float leftPillarH = fmaxf(archTop - leftSupportY, 0.5f);
            float rightPillarH = fmaxf(archTop - rightSupportY, 0.5f);

            // Left and Right Upright Support Columns
            AddCriticalStructure(env, STRUCT_CACHE_TOWER,
                                 (Vector3){ -13.0f, leftSupportY + leftPillarH * 0.5f, sectorCenterZ },
                                 (Vector3){ pillarW, leftPillarH, pillarW }, compileScale);
            AddCriticalStructure(env, STRUCT_CACHE_TOWER,
                                 (Vector3){ 13.0f, rightSupportY + rightPillarH * 0.5f, sectorCenterZ },
                                 (Vector3){ pillarW, rightPillarH, pillarW }, compileScale);

            // Overhead Cross Conduit Beam
            AddCriticalStructure(env, STRUCT_BUS_CONDUIT, (Vector3){ 0.0f, archTop, sectorCenterZ },
                                 (Vector3){ 26.0f, 1.2f, 1.2f }, compileScale);
        }
    }
}

void UpdateEnvironment(EnvironmentSystem *env, float virtualPlayerZ, float time, float intensity) {
    SetShaderValue(env->towerMaterial.shader, env->towerScrollLoc, &virtualPlayerZ, SHADER_UNIFORM_FLOAT);
    SetShaderValue(env->towerMaterial.shader, env->towerTimeLoc, &time, SHADER_UNIFORM_FLOAT);
    SetShaderValue(env->towerMaterial.shader, env->towerIntensityLoc, &intensity, SHADER_UNIFORM_FLOAT);
    SetShaderValue(env->terrainMaterial.shader, env->terrainTimeLoc, &time, SHADER_UNIFORM_FLOAT);
    SetShaderValue(env->terrainMaterial.shader, env->terrainIntensityLoc, &intensity, SHADER_UNIFORM_FLOAT);
    GenerateEnvironmentStructures(env, virtualPlayerZ);
    GroundFlankStructures(env, virtualPlayerZ);
    GenerateFarSilhouettes(env, virtualPlayerZ);
    GenerateTerrain(env, virtualPlayerZ);
}

static bool HasTerrainFoundation(const EnvironmentSystem *env, float virtualPlayerZ,
                                 const EnvironmentStructure *structure) {
    int objectCell = TerrainCellForX(structure->position.x);
    int objectSector = (int)floorf((virtualPlayerZ - structure->position.z) / SECTOR_DEPTH);
    for (int i = 0; i < env->terrainCount; i++) {
        const TerrainInstance *terrain = &env->terrain[i];
        if (TerrainCellForX(terrain->position.x) != objectCell) continue;
        int terrainSector = (int)floorf((virtualPlayerZ - terrain->position.z) / SECTOR_DEPTH);
        if (terrainSector == objectSector) return true;
    }
    return false;
}

bool ValidateEnvironmentGenerator(EnvironmentValidationReport *report) {
    if (!report) return false;

    report->zonesChecked = 0;
    report->adjacentRepeatViolations = 0;
    report->landmarkSpacingViolations = 0;
    report->peakStructureCount = 0;
    report->droppedStructures = 0;
    report->peakTerrainCount = 0;
    report->peakFarStructureCount = 0;
    report->unsupportedStructures = 0;
    report->unsupportedFlankStructures = 0;
    report->unsupportedRavineStructures = 0;
    report->unsupportedFarStructures = 0;

    int previousLeft = -1;
    int previousRight = -1;
    int previousLandmark = -1000000;
    for (int zoneKey = -1024; zoneKey < 1024; zoneKey++) {
        ZoneDescriptor zone = DescribeZoneByKey(zoneKey);
        int left = 0;
        int right = 0;
        PickFlankPair(&zone, &left, &right);
        if (left == previousLeft || right == previousRight) report->adjacentRepeatViolations++;

        if (IsLandmarkZone(zoneKey)) {
            if (zoneKey - previousLandmark <= 3) report->landmarkSpacingViolations++;
            previousLandmark = zoneKey;
        }

        previousLeft = left;
        previousRight = right;
        report->zonesChecked++;
    }

    static const uint32_t validationSeeds[] = {
        0u, 1u, 94u, UINT32_C(0x7fffffff), UINT32_C(0x9e3779b9), UINT32_MAX
    };
    uint32_t originalSeed = g_environmentSeed;
    EnvironmentSystem probe = { 0 };
    for (int seedIndex = 0; seedIndex < (int)(sizeof(validationSeeds) / sizeof(validationSeeds[0]));
         seedIndex++) {
        g_environmentSeed = validationSeeds[seedIndex];
        for (int sector = 0; sector < 4096; sector += 31) {
            float virtualPlayerZ = (float)sector * SECTOR_DEPTH + 3.25f;
            GenerateEnvironmentStructures(&probe, virtualPlayerZ);
            GroundFlankStructures(&probe, virtualPlayerZ);
            GenerateFarSilhouettes(&probe, virtualPlayerZ);
            GenerateTerrain(&probe, virtualPlayerZ);
            if (probe.structureCount > report->peakStructureCount) {
                report->peakStructureCount = probe.structureCount;
            }
            if (probe.terrainCount > report->peakTerrainCount) {
                report->peakTerrainCount = probe.terrainCount;
            }
            if (probe.farStructureCount > report->peakFarStructureCount) {
                report->peakFarStructureCount = probe.farStructureCount;
            }
            report->droppedStructures += probe.droppedStructures;

            for (int i = 0; i < probe.structureCount; i++) {
                const EnvironmentStructure *structure = &probe.structures[i];
                float bottom = structure->position.y - structure->size.y * 0.5f;
                if (fabsf(structure->position.x) < CORRIDOR_EDGE_X && bottom >= -4.0f) continue;
                if (!HasTerrainFoundation(&probe, virtualPlayerZ, structure)) {
                    report->unsupportedStructures++;
                    if (fabsf(structure->position.x) >= CORRIDOR_EDGE_X) {
                        report->unsupportedFlankStructures++;
                    } else {
                        report->unsupportedRavineStructures++;
                    }
                }
            }
            for (int i = 0; i < probe.farStructureCount; i++) {
                if (!HasTerrainFoundation(&probe, virtualPlayerZ, &probe.farStructures[i])) {
                    report->unsupportedStructures++;
                    report->unsupportedFarStructures++;
                }
            }
        }
    }
    g_environmentSeed = originalSeed;

    return report->adjacentRepeatViolations == 0 &&
           report->landmarkSpacingViolations == 0 &&
           report->droppedStructures == 0 &&
           report->unsupportedStructures == 0;
}

// Batches active structures by type and issues one instanced draw call per type
static void DrawStructureBatch(const EnvironmentSystem *env, StructureType type, Color accent, Color body) {
    static Matrix transforms[MAX_STRUCTURES];
    int count = 0;

    for (int i = 0; i < env->structureCount; i++) {
        const EnvironmentStructure *s = &env->structures[i];
        if (!s->active || s->type != type) continue;

        Vector3 renderPos = (Vector3){ s->position.x, s->position.y, s->position.z };
        transforms[count++] = MatrixMultiply(MatrixScale(s->size.x, s->size.y, s->size.z),
                                              MatrixTranslate(renderPos.x, renderPos.y, renderPos.z));
    }

    if (count == 0) return;

    // Alpha channel doubles as the shader's conduit-stream flag (tower.fs: uAccentColor.a > 1.5)
    float accentAlpha = (type == STRUCT_BUS_CONDUIT) ? 2.0f : 1.0f;
    Vector4 accentVec = { accent.r / 255.0f, accent.g / 255.0f, accent.b / 255.0f, accentAlpha };
    Vector3 bodyVec = { body.r / 255.0f, body.g / 255.0f, body.b / 255.0f };
    SetShaderValue(env->towerMaterial.shader, env->towerAccentLoc, &accentVec, SHADER_UNIFORM_VEC4);
    SetShaderValue(env->towerMaterial.shader, env->towerBodyLoc, &bodyVec, SHADER_UNIFORM_VEC3);
    // Landmarks trade the generic obsidian body for a raymarched fractal core
    // (tower.fs kind 11) so the rare beacon reads as a demoscene set-piece.
    int structureKind = (type == STRUCT_LANDMARK) ? 11 : (int)type;
    SetShaderValue(env->towerMaterial.shader, env->towerKindLoc, &structureKind, SHADER_UNIFORM_INT);

    Mesh mesh = (type == STRUCT_CACHE_TOWER || type == STRUCT_LANDMARK)
        ? env->unitPrismMesh : env->unitCubeMesh;
    DrawMeshInstanced(mesh, env->towerMaterial, transforms, count);
}

static void DrawTerrainBatch(const EnvironmentSystem *env) {
    static Matrix transforms[MAX_TERRAIN_INSTANCES];
    for (int i = 0; i < env->terrainCount; i++) {
        const TerrainInstance *instance = &env->terrain[i];
        transforms[i] = MatrixMultiply(MatrixScale(instance->size.x, instance->size.y,
                                                   instance->size.z),
                                       MatrixTranslate(instance->position.x, instance->position.y,
                                                       instance->position.z));
    }
    if (env->terrainCount > 0) {
        DrawMeshInstanced(env->unitCubeMesh, env->terrainMaterial, transforms, env->terrainCount);
    }
}

static void DrawFarStructureBatch(const EnvironmentSystem *env, StructureType type,
                                  Color accent, Color body) {
    static Matrix transforms[MAX_FAR_STRUCTURES];
    int count = 0;
    for (int i = 0; i < env->farStructureCount; i++) {
        const EnvironmentStructure *structure = &env->farStructures[i];
        if (!structure->active || structure->type != type) continue;
        transforms[count++] = MatrixMultiply(MatrixScale(structure->size.x, structure->size.y,
                                                         structure->size.z),
                                             MatrixTranslate(structure->position.x,
                                                             structure->position.y,
                                                             structure->position.z));
    }
    if (count == 0) return;

    Vector4 accentVec = { accent.r / 255.0f, accent.g / 255.0f, accent.b / 255.0f, 1.0f };
    Vector3 bodyVec = { body.r / 255.0f, body.g / 255.0f, body.b / 255.0f };
    int structureKind = (int)type;
    SetShaderValue(env->towerMaterial.shader, env->towerAccentLoc, &accentVec, SHADER_UNIFORM_VEC4);
    SetShaderValue(env->towerMaterial.shader, env->towerBodyLoc, &bodyVec, SHADER_UNIFORM_VEC3);
    SetShaderValue(env->towerMaterial.shader, env->towerKindLoc, &structureKind, SHADER_UNIFORM_INT);
    Mesh mesh = type == STRUCT_CACHE_TOWER ? env->unitPrismMesh : env->unitCubeMesh;
    DrawMeshInstanced(mesh, env->towerMaterial, transforms, count);
}

// Secondary detail is derived entirely from the already-generated structures.
// It therefore adds authored density without changing generation, collision,
// pool pressure or validation results. Each layer remains one instanced call.
static void DrawArchitectureDetailBatch(const EnvironmentSystem *env, int detailMode,
                                        float virtualPlayerZ) {
    static Matrix transforms[MAX_STRUCTURES];
    int count = 0;

    for (int i = 0; i < env->structureCount && count < MAX_STRUCTURES; i++) {
        const EnvironmentStructure *s = &env->structures[i];
        if (!s->active || s->type == STRUCT_BUS_CONDUIT || s->type == STRUCT_VOID) continue;
        if (detailMode >= 6 && s->position.z < -175.0f) continue;

        // Reconstruct stable world coordinates from the treadmill-relative Z.
        // Hashing the array index or relative Z made details re-roll as sectors
        // entered the pool and as the player advanced, which looked like roof
        // flicker even though the primary building itself was stable.
        int32_t worldXKey = (int32_t)lroundf(s->position.x * 16.0f);
        int32_t worldZKey = (int32_t)lroundf((virtualPlayerZ - s->position.z) * 8.0f);
        uint32_t identity = (uint32_t)worldXKey * UINT32_C(0x9e3779b9) ^
                            (uint32_t)worldZKey * UINT32_C(0x85ebca6b) ^
                            (uint32_t)s->type * UINT32_C(0xc2b2ae35);
        uint32_t hash = PaletteHash(env->runSeed ^ identity);
        Vector3 position = s->position;
        Vector3 size = { 0 };

        if (detailMode == 0) {
            // Mechanical penthouse/cap: a broad, low mass that gives each
            // tower a designed roofline without adding luminous decoration.
            if (s->size.y < 3.2f || (hash & 3u) == 0u) continue;
            float capHeight = 0.18f + (float)((hash >> 7) & 3u) * 0.055f;
            position.y += s->size.y * 0.5f + capHeight * 0.35f;
            size = (Vector3){ s->size.x * (1.02f + (float)((hash >> 4) & 3u) * 0.025f),
                              capHeight,
                              s->size.z * (1.02f + (float)((hash >> 9) & 3u) * 0.025f) };
        } else if (detailMode == 1) {
            // Twin load-bearing pilasters project from the corridor façade.
            // Their shadow and parallax provide detail, not emissive light.
            if (s->size.y < 5.0f || ((hash >> 2) & 7u) == 0u) continue;
            float corridorSide = s->position.x < 0.0f ? 1.0f : -1.0f;
            float ribOffset = s->size.z * 0.28f;
            position.x += corridorSide * (s->size.x * 0.5f + 0.10f);
            position.z += ribOffset;
            size = (Vector3){ 0.18f + (float)((hash >> 17) & 3u) * 0.035f,
                              Clamp(s->size.y * 0.70f, 2.8f, 18.0f),
                              Clamp(s->size.z * 0.13f, 0.28f, 0.90f) };
        } else if (detailMode == 2) {
            // One recessed glazing/service bay per selected building. The
            // shader supplies dark mullions and only a few dim occupied panes.
            if (s->size.y < 5.5f || hash % 4u == 0u || fabsf(s->position.x) < 10.0f) continue;
            float corridorSide = s->position.x < 0.0f ? 1.0f : -1.0f;
            position.x += corridorSide * (s->size.x * 0.5f + 0.055f);
            position.y += ((float)((hash >> 21) & 3u) - 1.5f) * s->size.y * 0.075f;
            size = (Vector3){ 0.085f, Clamp(s->size.y * 0.17f, 0.85f, 2.65f),
                              Clamp(s->size.z * 0.62f, 1.10f, 4.60f) };
        } else if (detailMode == 3) {
            // Antenna needles and crown transmitters add fine skyline detail.
            if (s->size.y < 8.5f || hash % 6u != 0u) continue;
            float height = 1.7f + (float)((hash >> 8) & 7u) * 0.42f;
            position.x += ((float)((hash >> 16) & 3u) - 1.5f) * s->size.x * 0.13f;
            position.z += ((float)((hash >> 19) & 3u) - 1.5f) * s->size.z * 0.13f;
            position.y += s->size.y * 0.5f + 0.38f + height * 0.5f;
            size = (Vector3){ 0.09f, height, 0.09f };
        } else if (detailMode == 4) {
            // A single structural service floor divides tall façades into a
            // believable base, shaft and crown rather than repeated collars.
            if (s->size.y < 8.0f || (hash & 3u) == 0u) continue;
            position.y += ((float)((hash >> 5) & 7u) / 7.0f - 0.34f) * s->size.y * 0.42f;
            size = (Vector3){ s->size.x * 1.055f,
                              0.15f + (float)((hash >> 13) & 3u) * 0.035f,
                              s->size.z * 1.055f };
        } else if (detailMode == 5) {
            // A wider podium anchors large structures into the ground plane.
            if (s->size.y < 4.5f) continue;
            float podiumHeight = Clamp(s->size.y * 0.085f, 0.42f, 1.25f);
            position.y = s->position.y - s->size.y * 0.5f + podiumHeight * 0.5f;
            size = (Vector3){ s->size.x * 1.17f, podiumHeight, s->size.z * 1.13f };
        } else if (detailMode == 6) {
            // A stepped external service/elevator annex changes the silhouette
            // of large towers and gives the main shaft a functional companion.
            if (s->size.y < 9.0f || s->size.x < 2.8f || (hash & 3u) == 0u) continue;
            float outerSide = s->position.x < 0.0f ? -1.0f : 1.0f;
            float annexWidth = Clamp(s->size.x * 0.28f, 0.75f, 2.8f);
            float annexHeight = s->size.y * (0.54f + (float)((hash >> 8) & 3u) * 0.055f);
            position.x += outerSide * (s->size.x * 0.5f + annexWidth * 0.38f);
            position.y = s->position.y - s->size.y * 0.5f + annexHeight * 0.5f;
            position.z += ((hash >> 12) & 1u) ? s->size.z * 0.12f : -s->size.z * 0.12f;
            size = (Vector3){ annexWidth, annexHeight, s->size.z * 0.58f };
        } else if (detailMode == 7) {
            // Occupied mechanical penthouse above the roof cap.
            if (s->size.y < 8.0f || hash % 3u == 0u) continue;
            float roomHeight = Clamp(s->size.y * 0.065f, 0.58f, 1.85f);
            position.x += ((hash >> 15) & 1u) ? s->size.x * 0.13f : -s->size.x * 0.13f;
            position.z += ((hash >> 16) & 1u) ? s->size.z * 0.11f : -s->size.z * 0.11f;
            position.y += s->size.y * 0.5f + roomHeight * 0.5f + 0.34f;
            size = (Vector3){ Clamp(s->size.x * 0.48f, 0.75f, 3.6f), roomHeight,
                              Clamp(s->size.z * 0.52f, 0.75f, 3.6f) };
        } else if (detailMode == 8) {
            // A projecting maintenance balcony with a deep underside.
            if (s->size.y < 7.0f || s->size.z < 2.2f || (hash & 7u) > 4u) continue;
            float corridorSide = s->position.x < 0.0f ? 1.0f : -1.0f;
            position.x += corridorSide * (s->size.x * 0.5f + 0.30f);
            position.y += ((float)((hash >> 5) & 7u) / 7.0f - 0.20f) * s->size.y * 0.42f;
            size = (Vector3){ 0.62f, 0.16f, Clamp(s->size.z * 0.68f, 1.5f, 5.0f) };
        } else if (detailMode == 9) {
            // Large louvered ventilation bank, rendered as dark recessed slats.
            if (s->size.y < 6.5f || s->size.z < 2.0f || hash % 5u > 2u) continue;
            float corridorSide = s->position.x < 0.0f ? 1.0f : -1.0f;
            position.x += corridorSide * (s->size.x * 0.5f + 0.060f);
            position.y -= s->size.y * (0.12f + (float)((hash >> 10) & 3u) * 0.035f);
            size = (Vector3){ 0.095f, Clamp(s->size.y * 0.14f, 0.75f, 2.4f),
                              Clamp(s->size.z * 0.54f, 1.0f, 4.2f) };
        } else if (detailMode == 10) {
            // Ground-level loading/entry aperture gives the podium human scale.
            if (s->size.y < 5.0f || s->size.z < 1.8f || (hash & 3u) == 0u) continue;
            float corridorSide = s->position.x < 0.0f ? 1.0f : -1.0f;
            float entryHeight = Clamp(s->size.y * 0.10f, 0.72f, 1.65f);
            position.x += corridorSide * (s->size.x * 0.5f + 0.065f);
            position.y = s->position.y - s->size.y * 0.5f + entryHeight * 0.57f;
            size = (Vector3){ 0.11f, entryHeight,
                              Clamp(s->size.z * 0.32f, 0.72f, 2.6f) };
        } else if (detailMode == 11) {
            // Paired rooftop HVAC housings create a believable equipment deck.
            if (s->size.y < 9.5f || s->size.x < 2.5f || hash % 4u != 1u) continue;
            float unitHeight = 0.38f + (float)((hash >> 13) & 3u) * 0.08f;
            float penthouseSide = ((hash >> 16) & 1u) ? 1.0f : -1.0f;
            position.x += s->size.x * 0.25f;
            position.z -= penthouseSide * s->size.z * 0.30f;
            position.y += s->size.y * 0.5f + unitHeight * 0.5f + 0.36f;
            size = (Vector3){ Clamp(s->size.x * 0.18f, 0.44f, 1.10f), unitHeight,
                              Clamp(s->size.z * 0.18f, 0.44f, 1.10f) };
        } else if (detailMode == 13) {
            // Sparse plasma accent panel: a demoscene sine-plasma "screen"
            // set into a fraction of tall facades, well clear of glazing bays.
            if (s->size.y < 6.5f || hash % 9u != 3u) continue;
            float corridorSide = s->position.x < 0.0f ? 1.0f : -1.0f;
            position.x += corridorSide * (s->size.x * 0.5f + 0.045f);
            position.y += ((float)((hash >> 6) & 3u) - 1.5f) * s->size.y * 0.10f;
            size = (Vector3){ 0.075f, Clamp(s->size.y * 0.22f, 1.1f, 3.2f),
                              Clamp(s->size.z * 0.34f, 0.9f, 2.6f) };
        } else if (detailMode == 14) {
            // Falling binary/matrix data-rain placard on a different sparse
            // subset of facades than the plasma panel or corridor signage.
            if (s->size.y < 6.0f || hash % 11u != 5u) continue;
            float corridorSide = s->position.x < 0.0f ? -1.0f : 1.0f;
            position.x += corridorSide * (s->size.x * 0.5f + 0.045f);
            position.y += ((float)((hash >> 9) & 3u) - 1.5f) * s->size.y * 0.08f;
            size = (Vector3){ 0.075f, Clamp(s->size.y * 0.30f, 1.3f, 3.6f),
                              Clamp(s->size.z * 0.40f, 1.0f, 3.0f) };
        } else {
            // A second, offset glazing bay appears only on tall nearby towers.
            if (s->size.y < 12.0f || s->size.z < 2.0f || (hash & 3u) != 2u) continue;
            float corridorSide = s->position.x < 0.0f ? 1.0f : -1.0f;
            position.x += corridorSide * (s->size.x * 0.5f + 0.056f);
            position.y += s->size.y * 0.24f;
            size = (Vector3){ 0.086f, Clamp(s->size.y * 0.14f, 1.05f, 2.45f),
                              Clamp(s->size.z * 0.56f, 1.15f, 4.4f) };
        }

        transforms[count++] = MatrixMultiply(MatrixScale(size.x, size.y, size.z),
                                              MatrixTranslate(position.x, position.y, position.z));
        if (detailMode == 1 && count < MAX_STRUCTURES) {
            position.z = s->position.z - s->size.z * 0.28f;
            transforms[count++] = MatrixMultiply(MatrixScale(size.x, size.y, size.z),
                                                  MatrixTranslate(position.x, position.y, position.z));
        } else if (detailMode == 11 && count < MAX_STRUCTURES) {
            position.x = s->position.x - (position.x - s->position.x);
            transforms[count++] = MatrixMultiply(MatrixScale(size.x, size.y, size.z),
                                                  MatrixTranslate(position.x, position.y, position.z));
        }
    }
    if (count == 0) return;

    bool glazing = detailMode == 2 || detailMode == 12;
    bool plasma = detailMode == 13;
    bool dataRain = detailMode == 14;
    Color accent = glazing ? env->secondaryColor
                  : plasma ? env->primaryColor
                  : dataRain ? env->secondaryColor
                  : env->primaryColor;
    Color body = glazing ? (Color){ 5, 9, 14, 255 }
                 : (plasma || dataRain) ? (Color){ 3, 5, 10, 255 }
                 : (detailMode == 9 || detailMode == 10
                    ? (Color){ 10, 13, 17, 255 } : (Color){ 18, 22, 28, 255 });
    Vector4 accentVec = { accent.r / 255.0f, accent.g / 255.0f,
                          accent.b / 255.0f, 1.0f };
    Vector3 bodyVec = { body.r / 255.0f, body.g / 255.0f, body.b / 255.0f };
    int structureKind = glazing ? 7 : plasma ? 6 : dataRain ? 5
                       : (detailMode == 9 ? 9 : (detailMode == 10 ? 10 : 8));
    SetShaderValue(env->towerMaterial.shader, env->towerAccentLoc, &accentVec, SHADER_UNIFORM_VEC4);
    SetShaderValue(env->towerMaterial.shader, env->towerBodyLoc, &bodyVec, SHADER_UNIFORM_VEC3);
    SetShaderValue(env->towerMaterial.shader, env->towerKindLoc, &structureKind, SHADER_UNIFORM_INT);
    DrawMeshInstanced(env->unitCubeMesh, env->towerMaterial, transforms, count);
}

// Conduits become believable infrastructure when elevated spans have supports
// and manufactured couplers. These are derived draw-only details and never
// enter the generator or collision pools.
static void DrawInfrastructureDetailBatch(const EnvironmentSystem *env, int detailMode) {
    static Matrix transforms[MAX_STRUCTURES];
    int count = 0;
    for (int i = 0; i < env->structureCount && count < MAX_STRUCTURES; i++) {
        const EnvironmentStructure *s = &env->structures[i];
        if (!s->active || s->type != STRUCT_BUS_CONDUIT || s->position.z < -190.0f) continue;
        bool alongX = s->size.x > s->size.z && s->size.x > s->size.y;
        bool alongZ = s->size.z >= s->size.x && s->size.z > s->size.y;
        Vector3 position = s->position;
        Vector3 size = { 0 };

        if (detailMode == 0) {
            if (s->position.y <= -2.15f || s->size.y > 1.2f || (!alongX && !alongZ)) continue;
            float top = s->position.y - s->size.y * 0.5f;
            float height = top + 2.86f;
            if (height < 0.5f) continue;
            position.y = -2.86f + height * 0.5f;
            size = (Vector3){ 0.22f, height, 0.22f };
            if (alongX && s->size.x > 5.0f) position.x -= s->size.x * 0.28f;
            else if (alongZ && s->size.z > 5.0f) position.z -= s->size.z * 0.28f;
        } else if (detailMode == 2) {
            // Data-rain strip on the conduit's top face: literal binary/matrix
            // "data streams in conduits" per the demoscene feedback pass.
            float longest = fmaxf(s->size.x, fmaxf(s->size.y, s->size.z));
            if (longest < 3.0f || (!alongX && !alongZ)) continue;
            position.y = s->position.y + s->size.y * 0.5f + 0.045f;
            if (alongX) size = (Vector3){ s->size.x * 0.92f, 0.05f, s->size.z * 0.5f };
            else size = (Vector3){ s->size.x * 0.5f, 0.05f, s->size.z * 0.92f };
        } else {
            float longest = fmaxf(s->size.x, fmaxf(s->size.y, s->size.z));
            if (longest < 2.0f) continue;
            if (alongX) size = (Vector3){ 0.30f, s->size.y * 1.30f, s->size.z * 1.22f };
            else if (alongZ) size = (Vector3){ s->size.x * 1.22f, s->size.y * 1.30f, 0.30f };
            else size = (Vector3){ s->size.x * 1.28f, 0.26f, s->size.z * 1.28f };
        }

        transforms[count++] = MatrixMultiply(MatrixScale(size.x, size.y, size.z),
                                              MatrixTranslate(position.x, position.y, position.z));
        if (detailMode == 0 && count < MAX_STRUCTURES) {
            if (alongX && s->size.x > 5.0f) position.x += s->size.x * 0.56f;
            else if (alongZ && s->size.z > 5.0f) position.z += s->size.z * 0.56f;
            else continue;
            transforms[count++] = MatrixMultiply(MatrixScale(size.x, size.y, size.z),
                                                  MatrixTranslate(position.x, position.y, position.z));
        }
    }
    if (count == 0) return;

    bool dataRain = detailMode == 2;
    Vector4 accentVec = { env->primaryColor.r / 255.0f, env->primaryColor.g / 255.0f,
                          env->primaryColor.b / 255.0f, 1.0f };
    Vector3 bodyVec = dataRain ? (Vector3){ 0.012f, 0.020f, 0.040f }
                              : (Vector3){ 0.060f, 0.074f, 0.090f };
    int structureKind = dataRain ? 5 : 8;
    SetShaderValue(env->towerMaterial.shader, env->towerAccentLoc, &accentVec, SHADER_UNIFORM_VEC4);
    SetShaderValue(env->towerMaterial.shader, env->towerBodyLoc, &bodyVec, SHADER_UNIFORM_VEC3);
    SetShaderValue(env->towerMaterial.shader, env->towerKindLoc, &structureKind, SHADER_UNIFORM_INT);
    DrawMeshInstanced(env->unitCubeMesh, env->towerMaterial, transforms, count);
}

void DrawEnvironment(const EnvironmentSystem *env, Camera3D camera, float virtualPlayerZ) {
    (void)camera;
    (void)virtualPlayerZ;

    DrawTerrainBatch(env);

    DrawFarStructureBatch(env, STRUCT_CACHE_TOWER, env->primaryColor, (Color){ 4, 8, 18, 255 });
    DrawFarStructureBatch(env, STRUCT_MEMORY_SLAB, env->secondaryColor, (Color){ 3, 9, 13, 255 });

    // Pass 2: Cyberspace monolithic architecture, batched by type (unified cyan-blue dominant palette)
    DrawStructureBatch(env, STRUCT_CACHE_TOWER, env->primaryColor, (Color){ 5, 10, 25, 255 });
    DrawStructureBatch(env, STRUCT_MEMORY_SLAB, env->secondaryColor, (Color){ 3, 12, 16, 255 });
    DrawStructureBatch(env, STRUCT_BUS_CONDUIT, env->primaryColor, (Color){ 3, 10, 24, 255 });
    DrawStructureBatch(env, STRUCT_LANDMARK, env->landmarkColor, (Color){ 24, 4, 18, 255 });

    // Architectural fidelity comes from physical massing and shadow: podiums,
    // pilasters, a single service floor, roof plant and sparse antennas. The
    // recessed glazing remains deliberately dim and never becomes signage.
    DrawArchitectureDetailBatch(env, 5, virtualPlayerZ);
    DrawArchitectureDetailBatch(env, 4, virtualPlayerZ);
    DrawArchitectureDetailBatch(env, 1, virtualPlayerZ);
    DrawArchitectureDetailBatch(env, 2, virtualPlayerZ);
    DrawArchitectureDetailBatch(env, 12, virtualPlayerZ);
    DrawArchitectureDetailBatch(env, 0, virtualPlayerZ);
    DrawArchitectureDetailBatch(env, 6, virtualPlayerZ);
    DrawArchitectureDetailBatch(env, 7, virtualPlayerZ);
    DrawArchitectureDetailBatch(env, 8, virtualPlayerZ);
    DrawArchitectureDetailBatch(env, 9, virtualPlayerZ);
    DrawArchitectureDetailBatch(env, 10, virtualPlayerZ);
    DrawArchitectureDetailBatch(env, 11, virtualPlayerZ);
    DrawArchitectureDetailBatch(env, 3, virtualPlayerZ);
    DrawArchitectureDetailBatch(env, 13, virtualPlayerZ);
    DrawArchitectureDetailBatch(env, 14, virtualPlayerZ);
    DrawInfrastructureDetailBatch(env, 0);
    DrawInfrastructureDetailBatch(env, 1);
    DrawInfrastructureDetailBatch(env, 2);
}

void UnloadEnvironment(EnvironmentSystem *env) {
    UnloadMesh(env->unitCubeMesh);
    UnloadMesh(env->unitPrismMesh);
    UnloadMaterial(env->terrainMaterial);
    UnloadMaterial(env->towerMaterial);
}
