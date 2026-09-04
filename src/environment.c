#include "environment.h"
#include "shader_sources.h"
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
#define CONDUIT_PLAYER_MAX_Y 7.0f
#define CONDUIT_PLAYER_MAX_X (10.0f * ENVIRONMENT_LATERAL_SCALE)
#define CONDUIT_CLEARANCE_MARGIN_Y 3.5f
#define CONDUIT_MIN_CROSSING_Y (CONDUIT_PLAYER_MAX_Y + CONDUIT_CLEARANCE_MARGIN_Y)
#define MAX_SITES_PER_SECTOR 9
#define MAX_CONDUIT_SPAN_DISTANCE 64.0f
#define TERRAIN_CELL_WIDTH 8.0f
#define TERRAIN_BASE_Y -28.0f
// Field site placement band, in unscaled X. Sites range from just outside the
// corridor edge to the outer edge of generated terrain (cellX +/-12 reaches 96.0f scaled).
#define FIELD_X_MIN 15.6f
#define FIELD_X_MAX 62.0f
#define FIELD_X_FLOOR 15.5f
// Effective breathing-zone rate after IsBreathingZone's local-minimum filter
// (which multiplies by ~1/3) is ~18%, inside the 15-20% target band.
#define FIELD_BREATHING_CHANCE 0.55f
#define FIELD_BREATHING_SEED 17001

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

static TerrainSupport SampleTerrainSupport(int cellX, int sector __attribute__((unused))) {
    TerrainSupport support = { 0 };
    int distanceFromCenter = cellX < 0 ? -cellX : cellX;
    
    support.occupied = true;
    
    // Clean geometric data trench
    support.topY = -4.0f;
    if (distanceFromCenter >= 2) support.topY = -2.0f;
    if (distanceFromCenter >= 5) support.topY = 0.0f;
    if (distanceFromCenter >= 8) support.topY = 2.0f;
    
    support.widthScale = 1.0f;
    return support;
}

static int TerrainCellForX(float x) {
    return (int)floorf(x / TERRAIN_CELL_WIDTH + 0.5f);
}

static inline float GetTerrainGroundOffset(float unscaledX, int sector) {
    int cellX = TerrainCellForX(ScaleEnvironmentX(unscaledX));
    TerrainSupport support = SampleTerrainSupport(cellX, sector);
    return support.topY + 3.0f;
}

static void GroundFlankStructures(EnvironmentSystem *env, double virtualPlayerZ) {
    for (int i = 0; i < env->structureCount; i++) {
        EnvironmentStructure *structure = &env->structures[i];
        if (structure->type == STRUCT_BUS_CONDUIT) continue;
        if (fabsf(structure->position.x) < CORRIDOR_EDGE_X) continue;

        int cellX = TerrainCellForX(structure->position.x);
        int sector = (int)floor((virtualPlayerZ - structure->position.z) / SECTOR_DEPTH);
        TerrainSupport support = SampleTerrainSupport(cellX, sector);
        structure->position.y += support.topY + 3.0f;
    }
}

static bool TerrainCellSupportsStructure(const EnvironmentSystem *env, double virtualPlayerZ,
                                         int cellX, int sector) {
    for (int i = 0; i < env->structureCount; i++) {
        const EnvironmentStructure *structure = &env->structures[i];
        if (fabsf(structure->position.x) < CORRIDOR_EDGE_X) continue;
        if (TerrainCellForX(structure->position.x) != cellX) continue;
        int structureSector = (int)floor((virtualPlayerZ - structure->position.z) / SECTOR_DEPTH);
        if (structureSector == sector) return true;
    }
    for (int i = 0; i < env->farStructureCount; i++) {
        const EnvironmentStructure *structure = &env->farStructures[i];
        if (TerrainCellForX(structure->position.x) != cellX) continue;
        int structureSector = (int)floor((virtualPlayerZ - structure->position.z) / SECTOR_DEPTH);
        if (structureSector == sector) return true;
    }
    return false;
}

static void GenerateTerrain(EnvironmentSystem *env, double virtualPlayerZ) {
    env->terrainCount = 0;
    int currentSector = (int)floor(virtualPlayerZ / SECTOR_DEPTH) - 2;
    int visibleSectors = (int)(TERRAIN_DRAW_DISTANCE / SECTOR_DEPTH) + 4;

    for (int offset = 0; offset < visibleSectors; offset++) {
        int sector = currentSector + offset;
        double worldZ = (double)sector * SECTOR_DEPTH;
        float relativeZ = (float)(-(worldZ - virtualPlayerZ) - SECTOR_DEPTH * 0.5);
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

static void GenerateFarSilhouettes(EnvironmentSystem *env, double virtualPlayerZ) {
    env->farStructureCount = 0;
    int currentSector = (int)floor(virtualPlayerZ / SECTOR_DEPTH);
    int firstFarSector = currentSector + (int)(DRAW_DISTANCE / SECTOR_DEPTH);
    int lastFarSector = currentSector + (int)(TERRAIN_DRAW_DISTANCE / SECTOR_DEPTH) + 1;

    for (int sector = firstFarSector; sector <= lastFarSector; sector += 2) {
        if (HashFloat(sector, 15001) < 0.34f) continue;
        int side = HashFloat(sector, 15002) < 0.5f ? -1 : 1;
        int cellX = side * (4 + 2 * (int)(HashFloat(sector, 15003) * 3.0f));
        TerrainSupport support = SampleTerrainSupport(cellX, sector);
        float height = 13.0f + HashFloat(sector, 15004) * 24.0f;
        float width = 5.0f + HashFloat(sector, 15005) * 7.0f;
        double worldZ = (double)sector * SECTOR_DEPTH;

        if (env->farStructureCount >= MAX_FAR_STRUCTURES) return;
        EnvironmentStructure *structure = &env->farStructures[env->farStructureCount++];
        structure->type = HashFloat(sector, 15006) < 0.72f ? STRUCT_CACHE_TOWER
                                                            : STRUCT_MEMORY_SLAB;
        structure->position = (Vector3){ ScaleEnvironmentX((float)cellX * TERRAIN_CELL_WIDTH),
                                         support.topY - 0.35f + height * 0.5f,
                                         (float)(-(worldZ - virtualPlayerZ) -
                                             SECTOR_DEPTH * 0.5) };
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

// Jitter multiplier in range [1.0 - maxVar, 1.0 + maxVar]
static inline float JitterMult(int zoneIdx, int seed, float maxVar) {
    return 1.0f + (HashFloat(zoneIdx, seed) * 2.0f - 1.0f) * maxVar;
}

// Zone-level "breathing room" roll: a whole zone drops to low density so some
// stretches of the field read as open. The local-minimum window guarantees two
// adjacent zones can never both be breathing, keeping the transition visible.
static bool IsBreathingZone(int zoneKey) {
    float score = HashFloat(zoneKey, FIELD_BREATHING_SEED);
    if (score > FIELD_BREATHING_CHANCE) return false;
    if (HashFloat(zoneKey - 1, FIELD_BREATHING_SEED) <= score) return false;
    if (HashFloat(zoneKey + 1, FIELD_BREATHING_SEED) <= score) return false;
    return true;
}

// Safely append a structure while reserving space for higher-value layers.
static inline int AddStructurePriority(EnvironmentSystem *env, StructureType type, Vector3 pos, Vector3 size,
                                       float compileScale, EmitPriority priority, float phaseOffset) {
    int serial = env->emissionSerial++;
    float typePhase = 0.0f;
    if (type == STRUCT_CACHE_TOWER) typePhase = 0.07f;
    else if (type == STRUCT_LANDMARK) typePhase = 0.12f;
    else if (type == STRUCT_BUS_CONDUIT) typePhase = 0.18f;
    float jitterPhase = HashFloat(env->emissionSector, 12000 + serial * 17) * 0.07f;
    float gate = Smooth01((compileScale - typePhase - phaseOffset - jitterPhase) / 0.30f);
    if (gate <= 0.001f) return -1;

    int limit = MAX_STRUCTURES;
    if (priority == EMIT_DETAIL) limit -= 192;
    else if (priority == EMIT_CONNECTION) limit -= 112;
    else if (priority == EMIT_STRUCTURAL) limit -= 32;

    if (env->structureCount >= limit) {
        env->droppedStructures++;
        return -1;
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
    env->structures[idx].edgeStart = (Vector3){ 0 };
    env->structures[idx].edgeEnd = (Vector3){ 0 };
    env->structures[idx].edgeLength = 0.0f;
    env->structures[idx].edgeSeed = 0;
    return idx;
}

static inline void AttachConduitEdgeData(EnvironmentSystem *env, int idx, Vector3 start, Vector3 end,
                                         float length, uint32_t seed) {
    if (idx >= 0 && idx < env->structureCount) {
        env->structures[idx].edgeStart = start;
        env->structures[idx].edgeEnd = end;
        env->structures[idx].edgeLength = length;
        env->structures[idx].edgeSeed = seed;
    }
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

// Geometric socket connectors with edge identity preservation
static inline int AddConduitFeederEx(EnvironmentSystem *env, float fromX, float toX, float y, float z,
                                     float sizeY, float sizeZ, float compileScale,
                                     Vector3 edgeStart, Vector3 edgeEnd, float edgeLength, uint32_t edgeSeed) {
    float len = fabsf(toX - fromX);
    if (len < 0.2f) return -1;
    float midX = (fromX + toX) * 0.5f;
    int idx = AddStructurePriority(env, STRUCT_BUS_CONDUIT, (Vector3){ midX, y, z },
                                   (Vector3){ len, sizeY, sizeZ }, compileScale, EMIT_CONNECTION, 0.0f);
    AttachConduitEdgeData(env, idx, edgeStart, edgeEnd, edgeLength, edgeSeed);
    return idx;
}

static inline int AddConduitRiserEx(EnvironmentSystem *env, float x, float z, float fromY, float toY,
                                    float sizeX, float sizeZ, float compileScale,
                                    Vector3 edgeStart, Vector3 edgeEnd, float edgeLength, uint32_t edgeSeed) {
    float h = fabsf(toY - fromY);
    if (h < 0.2f) return -1;
    float midY = (fromY + toY) * 0.5f;
    int idx = AddStructurePriority(env, STRUCT_BUS_CONDUIT, (Vector3){ x, midY, z },
                                   (Vector3){ sizeX, h, sizeZ }, compileScale, EMIT_CONNECTION, 0.0f);
    AttachConduitEdgeData(env, idx, edgeStart, edgeEnd, edgeLength, edgeSeed);
    return idx;
}

static inline int AddConduitSpanZEx(EnvironmentSystem *env, float x, float y, float fromZ, float toZ,
                                    float sizeX, float sizeY, float compileScale,
                                    Vector3 edgeStart, Vector3 edgeEnd, float edgeLength, uint32_t edgeSeed) {
    float lenZ = fabsf(toZ - fromZ);
    if (lenZ < 0.2f) return -1;
    float midZ = (fromZ + toZ) * 0.5f;
    int idx = AddStructurePriority(env, STRUCT_BUS_CONDUIT, (Vector3){ x, y, midZ },
                                   (Vector3){ sizeX, sizeY, lenZ }, compileScale, EMIT_CONNECTION, 0.0f);
    AttachConduitEdgeData(env, idx, edgeStart, edgeEnd, edgeLength, edgeSeed);
    return idx;
}

static inline void AddConduitFeeder(EnvironmentSystem *env, float fromX, float toX, float y, float z,
                                    float widthZ, float compileScale) {
    AddConduitFeederEx(env, fromX, toX, y, z, 0.45f, widthZ, compileScale, (Vector3){ 0 }, (Vector3){ 0 }, 0.0f, 0);
}

static inline void AddConduitRiser(EnvironmentSystem *env, float x, float z, float fromY, float toY, float compileScale) {
    AddConduitRiserEx(env, x, z, fromY, toY, 0.55f, 0.55f, compileScale, (Vector3){ 0 }, (Vector3){ 0 }, 0.0f, 0);
}

static inline void AddConduitSpanZ(EnvironmentSystem *env, float x, float y, float fromZ, float toZ, float compileScale) {
    AddConduitSpanZEx(env, x, y, fromZ, toZ, 0.55f, 0.50f, compileScale, (Vector3){ 0 }, (Vector3){ 0 }, 0.0f, 0);
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
    env->towerMaterial.shader = LoadShaderFromMemory(shader_tower_vs, shader_tower_fs);
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
    env->terrainMaterial.shader = LoadShaderFromMemory(shader_terrain_vs, shader_terrain_fs);
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

typedef struct {
    Vector3 position;
    Vector3 size;
    Vector3 socket;
    StructureType type;
} FieldSite;

static inline float Vec3Dist(Vector3 a, Vector3 b) {
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    float dz = a.z - b.z;
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

// Computes structure sites for a sector deterministically and returns site count.
static int CollectSectorFieldSites(int targetSector, float sectorCenterZ, FieldSite *outSites, int maxSites) {
    if (!outSites || maxSites <= 0) return 0;

    ZoneDescriptor zone = DescribeZoneForSector(targetSector);
    int zoneIdx = zone.key;
    float relativeZ = sectorCenterZ + (SECTOR_DEPTH * 0.5f);
    bool breathing = IsBreathingZone(zoneIdx);

    float effectiveDensity = zone.density * (1.0f - zone.openness * 0.45f);
    if (zone.theme == DISTRICT_OPEN_VOID) effectiveDensity *= 0.50f;
    if (breathing) effectiveDensity *= 0.35f;

    float gapRoll = HashFloat(targetSector, 13105);
    float gapThreshold = 0.05f + zone.openness * 0.15f;
    bool isGapSector = (gapRoll < gapThreshold) && !IsLandmarkZone(zoneIdx);

    float countRoll = 0.75f + HashFloat(targetSector, 13101) * 0.5f;
    float rawCount = isGapSector ? 1.0f : ((1.5f + effectiveDensity * 4.8f) * countRoll);
    int siteCount = (int)floorf(rawCount);
    if (HashFloat(targetSector, 13102) < (rawCount - (float)siteCount)) {
        siteCount++;
    }
    if (siteCount > maxSites) siteCount = maxSites;

    float corridorClearance = FIELD_X_MIN + zone.openness * 12.0f +
                              HashFloat(targetSector, 13125) * 6.0f;
    if (corridorClearance > 32.0f) corridorClearance = 32.0f;

    float layoutMode = HashFloat(targetSector, 13122);

    for (int i = 0; i < siteCount; i++) {
        float side;
        if (layoutMode < 0.36f) {
            side = (HashFloat(targetSector, 13124 + i * 7) < 0.80f) ? -1.0f : 1.0f;
        } else if (layoutMode < 0.72f) {
            side = (HashFloat(targetSector, 13124 + i * 7) < 0.80f) ? 1.0f : -1.0f;
        } else {
            side = (HashFloat(targetSector, 13124 + i * 7) < 0.50f) ? -1.0f : 1.0f;
        }

        // Stratified slot with jitter across width
        float slot = ((float)i + 0.5f + (HashFloat(targetSector, 13110 + i) - 0.5f) * 0.6f) / (float)siteCount;
        float t = Clamp(slot, 0.04f, 0.96f);

        // Power curve distributes structures across the full width while ensuring
        // healthy presence in the outer field past the flanks.
        float normX = powf(t, 0.85f);
        float magnitude = corridorClearance + normX * (FIELD_X_MAX - corridorClearance) +
                          (HashFloat(targetSector, 13130 + i) - 0.5f) * 2.0f;
        if (magnitude < FIELD_X_FLOOR) magnitude = FIELD_X_FLOOR;
        if (magnitude > FIELD_X_MAX) magnitude = FIELD_X_MAX;

        float siteX = side * magnitude;
        float siteZ = relativeZ - (0.15f + HashFloat(targetSector, 13140 + i) * 0.7f) * SECTOR_DEPTH;

        // Perspective-correct height profile
        float distFromCorridor = (magnitude - FIELD_X_MIN) / (FIELD_X_MAX - FIELD_X_MIN);
        float heightMax;
        if (distFromCorridor < 0.22f) {
            heightMax = Lerp(1.5f, 4.5f, distFromCorridor / 0.22f) * zone.heightScale;
        } else if (distFromCorridor < 0.58f) {
            heightMax = Lerp(4.5f, 20.0f, (distFromCorridor - 0.22f) / 0.36f) * zone.heightScale;
        } else {
            heightMax = Lerp(20.0f, 68.0f, (distFromCorridor - 0.58f) / 0.42f) * zone.heightScale;
        }

        float siteH = (0.40f + 0.60f * HashFloat(targetSector, 13150 + i)) * heightMax;
        float shapeRoll = HashFloat(targetSector, 13160 + i);
        float widthJitter = JitterMult(zoneIdx, 13180 + i, 0.18f);

        StructureType type;
        Vector3 size;
        Vector3 pos;

        if (distFromCorridor < 0.22f) {
            if (shapeRoll < 0.35f) {
                type = STRUCT_CACHE_TOWER;
                float spikeW = (0.9f + HashFloat(targetSector, 13170 + i) * 0.4f) * widthJitter;
                float spikeH = 3.0f + HashFloat(targetSector, 13150 + i) * 3.0f;
                size = (Vector3){ spikeW, spikeH, spikeW };
                pos = (Vector3){ siteX, -2.4f + spikeH * 0.5f, siteZ };
            } else {
                type = STRUCT_MEMORY_SLAB;
                float slabW = (2.5f + shapeRoll * 3.0f) * widthJitter;
                float slabH = 1.2f + HashFloat(targetSector, 13150 + i) * 1.8f;
                float slabD = (3.0f + HashFloat(targetSector, 13190 + i) * 3.5f) * widthJitter;
                size = (Vector3){ slabW, slabH, slabD };
                pos = (Vector3){ siteX, -2.4f + slabH * 0.5f, siteZ };
            }
        } else if (distFromCorridor > 0.60f && shapeRoll < 0.15f) {
            type = STRUCT_CACHE_TOWER;
            float spikeW = (1.2f + HashFloat(targetSector, 13170 + i) * 0.6f) * widthJitter;
            size = (Vector3){ spikeW, siteH, spikeW };
            pos = (Vector3){ siteX, -2.4f + siteH * 0.5f, siteZ };
        } else if (siteH > 7.0f) {
            type = STRUCT_CACHE_TOWER;
            float siteW = (4.0f + shapeRoll * 5.0f) * widthJitter;
            size = (Vector3){ siteW, siteH, siteW };
            pos = (Vector3){ siteX, -1.8f + siteH * 0.5f, siteZ };
        } else {
            type = STRUCT_MEMORY_SLAB;
            float siteW = (3.0f + shapeRoll * 4.5f) * widthJitter;
            float siteD = siteW * (0.75f + 0.5f * HashFloat(targetSector, 13190 + i));
            size = (Vector3){ siteW, siteH, siteD };
            pos = (Vector3){ siteX, -2.4f + siteH * 0.5f, siteZ };
        }

        float groundOffset = GetTerrainGroundOffset(siteX, targetSector);
        float socketH = fminf(siteH * 0.70f, 6.5f);
        if (socketH < 1.8f) socketH = 1.8f;

        outSites[i].type = type;
        outSites[i].size = size;
        outSites[i].position = pos;
        outSites[i].socket = (Vector3){ siteX, -2.4f + groundOffset + socketH, siteZ };
    }

    return siteCount;
}

// Emits field structures for this sector based on collected sites
static void GenerateFieldSites(EnvironmentSystem *env, const ZoneDescriptor *zone,
                               int sectorInZone, float sectorCenterZ, float compileScale) {
    int targetSector = zone->startSector + sectorInZone;
    int zoneIdx = zone->key;

    FieldSite sites[MAX_SITES_PER_SECTOR];
    int siteCount = CollectSectorFieldSites(targetSector, sectorCenterZ, sites, MAX_SITES_PER_SECTOR);

    for (int i = 0; i < siteCount; i++) {
        FieldSite *s = &sites[i];
        float shapeRoll = HashFloat(targetSector, 13160 + i);
        float magnitude = fabsf(s->position.x);
        float distFromCorridor = (magnitude - FIELD_X_MIN) / (FIELD_X_MAX - FIELD_X_MIN);

        if (distFromCorridor < 0.22f && shapeRoll < 0.35f) {
            float spikeW = s->size.x;
            float spikeH = s->size.y * compileScale;
            AddStructure(env, STRUCT_CACHE_TOWER,
                         (Vector3){ s->position.x, -2.4f + spikeH * 0.5f, s->position.z },
                         (Vector3){ spikeW, spikeH, spikeW }, compileScale);
            AddStructure(env, STRUCT_MEMORY_SLAB,
                         (Vector3){ s->position.x, -2.4f + 0.35f * compileScale, s->position.z },
                         (Vector3){ spikeW * 2.5f, 0.7f * compileScale, spikeW * 2.5f }, compileScale);
        } else if (distFromCorridor > 0.60f && shapeRoll < 0.15f) {
            float spikeW = s->size.x;
            float siteH = s->size.y * compileScale;
            AddStructure(env, STRUCT_CACHE_TOWER,
                         (Vector3){ s->position.x, -2.4f + siteH * 0.5f, s->position.z },
                         (Vector3){ spikeW, siteH, spikeW }, compileScale);
            AddStructure(env, STRUCT_MEMORY_SLAB,
                         (Vector3){ s->position.x, -2.4f + 0.4f * compileScale, s->position.z },
                         (Vector3){ spikeW * 2.6f, 0.8f * compileScale, spikeW * 2.6f }, compileScale);
        } else if (s->type == STRUCT_CACHE_TOWER) {
            float siteH = s->size.y * compileScale;
            AddStructure(env, STRUCT_CACHE_TOWER,
                         (Vector3){ s->position.x, -1.8f + siteH * 0.5f, s->position.z },
                         (Vector3){ s->size.x, siteH, s->size.z }, compileScale);
        } else {
            float siteH = s->size.y * compileScale;
            AddStructure(env, STRUCT_MEMORY_SLAB,
                         (Vector3){ s->position.x, -2.4f + siteH * 0.5f, s->position.z },
                         (Vector3){ s->size.x, siteH, s->size.z }, compileScale);
        }
    }

    if (IsLandmarkZone(zoneIdx)) {
        float side = HashFloat(zoneIdx, 9095) < 0.5f ? -1.0f : 1.0f;
        float jBeacon = JitterMult(zoneIdx, 1100, 0.10f);
        float beaconX = side * ((26.0f + zone->openness * 9.0f) * jBeacon);
        float beaconH = (36.0f + HashFloat(targetSector, 901) * 22.0f) * zone->heightScale * compileScale;
        float beaconW = (10.0f + HashFloat(targetSector, 902) * 4.0f) * compileScale;

        AddCriticalStructure(env, STRUCT_LANDMARK,
                             (Vector3){ beaconX, -1.2f + beaconH * 0.5f, sectorCenterZ },
                             (Vector3){ beaconW, beaconH, beaconW }, compileScale);

        float pylonH = beaconH * 0.45f;
        float pylonOff = beaconW * 0.85f;
        AddStructure(env, STRUCT_CACHE_TOWER,
                     (Vector3){ beaconX - pylonOff, -2.2f + pylonH * 0.5f, sectorCenterZ - pylonOff },
                     (Vector3){ 3.2f, pylonH, 3.2f }, compileScale);
        AddStructure(env, STRUCT_CACHE_TOWER,
                     (Vector3){ beaconX + pylonOff, -2.2f + pylonH * 0.5f, sectorCenterZ - pylonOff },
                     (Vector3){ 3.2f, pylonH, 3.2f }, compileScale);
        AddStructure(env, STRUCT_CACHE_TOWER,
                     (Vector3){ beaconX - pylonOff, -2.2f + pylonH * 0.5f, sectorCenterZ + pylonOff },
                     (Vector3){ 3.2f, pylonH, 3.2f }, compileScale);
        AddStructure(env, STRUCT_CACHE_TOWER,
                     (Vector3){ beaconX + pylonOff, -2.2f + pylonH * 0.5f, sectorCenterZ + pylonOff },
                     (Vector3){ 3.2f, pylonH, 3.2f }, compileScale);

        AddConduitFeeder(env, beaconX, beaconX - side * pylonOff,
                         -2.2f + 0.4f, sectorCenterZ, 1.2f, compileScale);
    }
}

// Emits an edge in the procedural conduit graph with multi-conduit bundle variation
static void EmitConduitEdge(EnvironmentSystem *env, Vector3 from, Vector3 to,
                            bool crossesCenterline, uint32_t edgeSeed, float compileScale) {
    float edgeLength = Vec3Dist(from, to);
    if (edgeLength < 1.0f) return;

    // Bundle style:
    // 0: Single thick conduit (~45%)
    // 1: Double parallel thinner conduits (~35%)
    // 2: Triple parallel thinner conduits (~20%)
    float styleRoll = HashFloat(edgeSeed, 2401);
    int bundleCount = (styleRoll < 0.45f) ? 1 : ((styleRoll < 0.80f) ? 2 : 3);
    float conduitSize;
    float offsets[3] = { 0.0f, 0.0f, 0.0f };

    if (bundleCount == 1) {
        conduitSize = 1.20f;
        offsets[0] = 0.0f;
    } else if (bundleCount == 2) {
        conduitSize = 0.72f;
        offsets[0] = -0.70f;
        offsets[1] =  0.70f;
    } else {
        conduitSize = 0.55f;
        offsets[0] = -0.90f;
        offsets[1] =  0.0f;
        offsets[2] =  0.90f;
    }

    // Socket coupler collars at connection ports on both buildings
    Vector3 couplerSize = (Vector3){ conduitSize * 1.8f, conduitSize * 1.8f, conduitSize * 1.8f };
    AddDetailStructure(env, STRUCT_MEMORY_SLAB, from, couplerSize, compileScale, 0.0f);
    AddDetailStructure(env, STRUCT_MEMORY_SLAB, to, couplerSize, compileScale, 0.0f);

    if (crossesCenterline) {
        // Crossing conduits: Connect from Building A (from) across to Building B (to)
        // High overhead crossing clearing CONDUIT_PLAYER_MAX_Y with safe margin
        float crossY = fmaxf(CONDUIT_MIN_CROSSING_Y, fmaxf(from.y, to.y) + 2.5f);

        for (int b = 0; b < bundleCount; b++) {
            float off = offsets[b];

            // 1. Vertical riser on Building A: rises from building socket up to crossY
            if (crossY > from.y + 0.3f) {
                AddConduitRiserEx(env, from.x, from.z + off, from.y, crossY,
                                  conduitSize, conduitSize, compileScale,
                                  from, to, edgeLength, edgeSeed);
            }

            // 2. Overhead span from Building A across corridor to Building B at crossY
            AddConduitFeederEx(env, from.x, to.x, crossY, from.z + off,
                               conduitSize, conduitSize, compileScale,
                               from, to, edgeLength, edgeSeed);

            // 3. Span along Z from from.z to to.z at Building B's X at crossY
            if (fabsf(to.z - from.z) > 0.8f) {
                AddConduitSpanZEx(env, to.x + off, crossY, from.z, to.z,
                                  conduitSize, conduitSize, compileScale,
                                  from, to, edgeLength, edgeSeed);
            }

            // 4. Vertical riser on Building B: descends from crossY down into Building B socket
            if (crossY > to.y + 0.3f) {
                AddConduitRiserEx(env, to.x, to.z + off, to.y, crossY,
                                  conduitSize, conduitSize, compileScale,
                                  from, to, edgeLength, edgeSeed);
            }
        }
    } else {
        // Same-side elevated aerial span between Building A and Building B
        float spanY = fmaxf(from.y, to.y);

        for (int b = 0; b < bundleCount; b++) {
            float off = offsets[b];

            // Riser at Building A up to spanY if needed
            if (fabsf(spanY - from.y) > 0.3f) {
                AddConduitRiserEx(env, from.x, from.z + off, fminf(from.y, spanY), fmaxf(from.y, spanY),
                                  conduitSize, conduitSize, compileScale,
                                  from, to, edgeLength, edgeSeed);
            }

            // X-feeder between from.x and to.x at spanY
            if (fabsf(to.x - from.x) > 0.6f) {
                AddConduitFeederEx(env, from.x, to.x, spanY, from.z + off,
                                   conduitSize, conduitSize, compileScale,
                                   from, to, edgeLength, edgeSeed);
            }

            // Z-span between from.z and to.z at to.x, spanY
            if (fabsf(to.z - from.z) > 0.6f) {
                AddConduitSpanZEx(env, to.x + off, spanY, from.z, to.z,
                                  conduitSize, conduitSize, compileScale,
                                  from, to, edgeLength, edgeSeed);
            }

            // Riser at Building B down to to.y if needed
            if (fabsf(spanY - to.y) > 0.3f) {
                AddConduitRiserEx(env, to.x, to.z + off, fminf(to.y, spanY), fmaxf(to.y, spanY),
                                  conduitSize, conduitSize, compileScale,
                                  from, to, edgeLength, edgeSeed);
            }
        }
    }
}

// Builds the procedural conduit graph connecting structure sites across sectors
static void GenerateConduitGraph(EnvironmentSystem *env, int targetSector, float sectorCenterZ, float compileScale) {
    FieldSite currSites[MAX_SITES_PER_SECTOR];
    int currCount = CollectSectorFieldSites(targetSector, sectorCenterZ, currSites, MAX_SITES_PER_SECTOR);
    if (currCount == 0) return;

    FieldSite prevSites[MAX_SITES_PER_SECTOR];
    int prevCount = CollectSectorFieldSites(targetSector - 1, sectorCenterZ + SECTOR_DEPTH, prevSites, MAX_SITES_PER_SECTOR);

    ZoneDescriptor zone = DescribeZoneForSector(targetSector);

    for (int i = 0; i < currCount; i++) {
        FieldSite *siteA = &currSites[i];
        uint32_t siteSeed = (uint32_t)targetSector * 100u + (uint32_t)i;

        // Candidate 1: Intra-sector same-side neighbor
        int bestJ = -1;
        float bestDistJ = 9999.0f;
        for (int j = i + 1; j < currCount; j++) {
            FieldSite *siteB = &currSites[j];
            if (siteA->position.x * siteB->position.x > 0.0f) {
                float dist = Vec3Dist(siteA->socket, siteB->socket);
                if (dist < bestDistJ) {
                    bestDistJ = dist;
                    bestJ = j;
                }
            }
        }
        if (bestJ >= 0 && bestDistJ <= MAX_CONDUIT_SPAN_DISTANCE) {
            float prob = 0.80f * (1.0f - bestDistJ / MAX_CONDUIT_SPAN_DISTANCE) * (1.0f - zone.openness * 0.25f);
            if (HashFloat(targetSector, 14100 + i * 19 + bestJ) < prob) {
                uint32_t edgeSeed = HashUint(siteSeed ^ ((uint32_t)bestJ * 31u));
                EmitConduitEdge(env, siteA->socket, currSites[bestJ].socket, false, edgeSeed, compileScale);
            }
        }

        // Candidate 2: Inter-sector same-side neighbor (pipeline across sectors)
        if (prevCount > 0) {
            int bestK = -1;
            float bestDist = 9999.0f;
            for (int k = 0; k < prevCount; k++) {
                if (prevSites[k].position.x * siteA->position.x > 0.0f) {
                    float dist = Vec3Dist(siteA->socket, prevSites[k].socket);
                    if (dist < bestDist) {
                        bestDist = dist;
                        bestK = k;
                    }
                }
            }
            if (bestK >= 0 && bestDist <= MAX_CONDUIT_SPAN_DISTANCE) {
                float prob = 0.80f * (1.0f - bestDist / MAX_CONDUIT_SPAN_DISTANCE) * (1.0f - zone.openness * 0.25f);
                if (HashFloat(targetSector, 14300 + i * 23 + bestK) < prob) {
                    uint32_t edgeSeed = HashUint(siteSeed ^ ((uint32_t)bestK * 73u) ^ 0x5a5a5a5au);
                    EmitConduitEdge(env, siteA->socket, prevSites[bestK].socket, false, edgeSeed, compileScale);
                }
            }
        }

        // Candidate 3: Cross-corridor candidate (overhead crossing connecting left to right)
        float crossRoll = HashFloat(targetSector, 14500 + i * 37);
        float crossChance = 0.28f + (zone.theme == DISTRICT_CONDUIT_EXCHANGE ? 0.32f : 0.0f);
        if (crossRoll < crossChance) {
            FieldSite *crossTarget = NULL;
            float bestCrossDist = 9999.0f;
            for (int j = 0; j < currCount; j++) {
                if (currSites[j].position.x * siteA->position.x < 0.0f) {
                    float d = Vec3Dist(siteA->socket, currSites[j].socket);
                    if (d < bestCrossDist) {
                        bestCrossDist = d;
                        crossTarget = &currSites[j];
                    }
                }
            }
            if (!crossTarget && prevCount > 0) {
                for (int k = 0; k < prevCount; k++) {
                    if (prevSites[k].position.x * siteA->position.x < 0.0f) {
                        float d = Vec3Dist(siteA->socket, prevSites[k].socket);
                        if (d < bestCrossDist) {
                            bestCrossDist = d;
                            crossTarget = &prevSites[k];
                        }
                    }
                }
            }
            if (crossTarget && bestCrossDist <= 85.0f) {
                uint32_t edgeSeed = HashUint(siteSeed ^ 0xa5a5a5a5u);
                EmitConduitEdge(env, siteA->socket, crossTarget->socket, true, edgeSeed, compileScale);
            }
        }
    }
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

static void GenerateEnvironmentStructures(EnvironmentSystem *env, double virtualPlayerZ) {
    env->structureCount = 0;
    env->droppedStructures = 0;

    int currentSector = (int)floor(virtualPlayerZ / SECTOR_DEPTH);
    int visibleSectors = (int)(DRAW_DISTANCE / SECTOR_DEPTH) + 2;

    for (int i = 0; i < visibleSectors; i++) {
        int targetSector = currentSector + i;
        double worldZ = (double)targetSector * SECTOR_DEPTH;
        float relativeZ = (float)(-(worldZ - virtualPlayerZ)); // Negative Z is forward

        if (relativeZ > 10.0f || relativeZ < -DRAW_DISTANCE - 16.0f) continue;

        // JIT Horizon scaling factor (0.0 at far clip, 1.0 approaching player)
        float depthFactor = 1.0f - (-relativeZ / DRAW_DISTANCE);
        float compileScale = Clamp(depthFactor * 1.5f, 0.0f, 1.0f);
        if (compileScale <= 0.01f) continue;

        float sectorCenterZ = relativeZ - (SECTOR_DEPTH * 0.5f);
        env->emissionSector = targetSector;
        env->emissionSerial = 0;

        // =========================================================================
        // 1. 2D FIELD STRUCTURE SITES (continuous full-width distribution)
        // =========================================================================
        ZoneDescriptor zone = DescribeZoneForSector(targetSector);
        int sectorInZone = targetSector - zone.startSector;
        GenerateFieldSites(env, &zone, sectorInZone, sectorCenterZ, compileScale);
        GenerateRavineObject(env, targetSector, sectorCenterZ, compileScale);

        // =========================================================================
        // 2. PROCEDURAL CONDUIT GRAPH (replaces disconnected highway pipes)
        // =========================================================================
        GenerateConduitGraph(env, targetSector, sectorCenterZ, compileScale);

        // =========================================================================
        // 3. BLUE-NOISE GANTRY ARCHWAYS (Local maxima, not periodic spacing)
        // =========================================================================
        if (sectorInZone == zone.length / 2 && IsGantryZone(zone.key)) {
            float archH = fmaxf(13.5f * zone.heightScale, 13.5f);
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

            // Overhead Cross Conduit Beam (clears CONDUIT_PLAYER_MAX_Y with safe margin)
            AddCriticalStructure(env, STRUCT_BUS_CONDUIT, (Vector3){ 0.0f, archTop, sectorCenterZ },
                                 (Vector3){ 26.0f, 1.2f, 1.2f }, compileScale);
        }
    }
}

void UpdateEnvironment(EnvironmentSystem *env, double virtualPlayerZ, float time, float intensity) {
    float shaderScroll = (float)virtualPlayerZ;
    SetShaderValue(env->towerMaterial.shader, env->towerScrollLoc, &shaderScroll, SHADER_UNIFORM_FLOAT);
    SetShaderValue(env->towerMaterial.shader, env->towerTimeLoc, &time, SHADER_UNIFORM_FLOAT);
    SetShaderValue(env->towerMaterial.shader, env->towerIntensityLoc, &intensity, SHADER_UNIFORM_FLOAT);
    SetShaderValue(env->terrainMaterial.shader, env->terrainTimeLoc, &time, SHADER_UNIFORM_FLOAT);
    SetShaderValue(env->terrainMaterial.shader, env->terrainIntensityLoc, &intensity, SHADER_UNIFORM_FLOAT);
    GenerateEnvironmentStructures(env, virtualPlayerZ);
    GroundFlankStructures(env, virtualPlayerZ);
    GenerateFarSilhouettes(env, virtualPlayerZ);
    GenerateTerrain(env, virtualPlayerZ);
}

static bool HasTerrainFoundation(const EnvironmentSystem *env, double virtualPlayerZ,
                                 const EnvironmentStructure *structure) {
    int objectCell = TerrainCellForX(structure->position.x);
    int objectSector = (int)floor((virtualPlayerZ - structure->position.z) / SECTOR_DEPTH);
    for (int i = 0; i < env->terrainCount; i++) {
        const TerrainInstance *terrain = &env->terrain[i];
        if (TerrainCellForX(terrain->position.x) != objectCell) continue;
        int terrainSector = (int)floor((virtualPlayerZ - terrain->position.z) / SECTOR_DEPTH);
        if (terrainSector == objectSector) return true;
    }
    return false;
}

// Computes local conduit flow direction and continuous corner phase offset
static void ComputeConduitFlow(const EnvironmentStructure *s, Vector3 *outDir, float *outPhase) {
    float sx = s->size.x;
    float sy = s->size.y;
    float sz = s->size.z;

    uint32_t seed = s->edgeSeed;
    if (seed == 0) {
        seed = HashUint((uint32_t)lroundf(fabsf(s->position.x) * 100.0f) ^
                        (uint32_t)lroundf(fabsf(s->position.z) * 100.0f));
    }
    float basePhase = HashFloat(seed, 8191) * 6.2831853f;

    Vector3 dir = { 0 };
    float waveScale = 0.08f;
    float pathStartDist = 0.0f;
    float segLen = 1.0f;

    if (s->edgeLength > 0.1f) {
        Vector3 A = s->edgeStart;
        Vector3 B = s->edgeEnd;

        bool crosses = (A.x * B.x < 0.0f);
        float spanY = crosses ? fmaxf(CONDUIT_MIN_CROSSING_Y, fmaxf(A.y, B.y) + 2.5f)
                              : fmaxf(A.y, B.y);

        float h1 = fmaxf(0.0f, spanY - A.y);
        float lenX = fabsf(B.x - A.x);
        float lenZ = fabsf(B.z - A.z);

        if (sx >= sy && sx >= sz) {
            // Feeder span along X: flows from A.x towards B.x
            dir.x = (B.x >= A.x) ? 1.0f : -1.0f;
            dir.y = 0.0f;
            dir.z = 0.0f;
            pathStartDist = h1;
            segLen = sx;
        } else if (sz >= sx && sz >= sy) {
            // Z-span: flows from A.z towards B.z
            dir.x = 0.0f;
            dir.y = 0.0f;
            dir.z = (B.z <= A.z) ? 1.0f : -1.0f;
            pathStartDist = h1 + lenX;
            segLen = sz;
        } else {
            // Riser along Y
            float dStartSq = (s->position.x - A.x) * (s->position.x - A.x) +
                             (s->position.z - A.z) * (s->position.z - A.z);
            float dEndSq = (s->position.x - B.x) * (s->position.x - B.x) +
                           (s->position.z - B.z) * (s->position.z - B.z);
            dir.x = 0.0f;
            dir.z = 0.0f;
            segLen = sy;
            if (dStartSq <= dEndSq) {
                // Origin riser: rises from A.y up to spanY (+Y)
                dir.y = 1.0f;
                pathStartDist = 0.0f;
            } else {
                // Target riser: descends from spanY down into B.y (-Y)
                dir.y = -1.0f;
                pathStartDist = h1 + lenX + lenZ;
            }
        }

        *outPhase = basePhase + (pathStartDist + 0.5f * segLen) * waveScale;
    } else {
        // Fallback for conduits without explicit edge endpoint linkage
        if (sx >= sy && sx >= sz) {
            dir.x = (s->position.x >= 0.0f) ? 1.0f : -1.0f;
            segLen = sx;
        } else if (sz >= sx && sz >= sy) {
            dir.z = 1.0f;
            segLen = sz;
        } else {
            dir.y = 1.0f;
            segLen = sy;
        }
        *outPhase = basePhase + 0.5f * segLen * waveScale;
    }

    *outDir = dir;
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
    report->fieldCoverageViolations = 0;
    report->crossingClearanceViolations = 0;
    report->conduitFlowViolations = 0;

    int previousLandmark = -1000000;
    for (int zoneKey = -1024; zoneKey < 1024; zoneKey++) {
        // The field has no per-zone archetype anymore; the macro-composition
        // invariant is that breathing zones never sit next to each other. This
        // guards IsBreathingZone's local-minimum construction: weakening that
        // roll so adjacent zones can both breathe surfaces here.
        if (IsBreathingZone(zoneKey) && IsBreathingZone(zoneKey - 1)) report->adjacentRepeatViolations++;

        if (IsLandmarkZone(zoneKey)) {
            if (zoneKey - previousLandmark <= 3) report->landmarkSpacingViolations++;
            previousLandmark = zoneKey;
        }

        report->zonesChecked++;
    }

    static const uint32_t validationSeeds[] = {
        0u, 1u, 94u, UINT32_C(0x7fffffff), UINT32_C(0x9e3779b9), UINT32_MAX
    };
    uint32_t originalSeed = g_environmentSeed;
    EnvironmentSystem probe = { 0 };

    // Field X-coverage histogram (scaled |x|), per side. Guards against the
    // old failure mode where all mass clustered into two narrow X bands: every
    // side must reach both the corridor-adjacent and the far-reach bins, and
    // no single bin may dominate the distribution.
    enum { FIELD_BIN_COUNT = 5 };
    const float fieldBinStart = CORRIDOR_EDGE_X;
    const float fieldBinEnd = FIELD_X_MAX * ENVIRONMENT_LATERAL_SCALE;
    int fieldBins[2][FIELD_BIN_COUNT] = { 0 };
    int fieldTotal = 0;
    float minConduitPhase = 1e9f;
    float maxConduitPhase = -1e9f;
    int totalConduitsChecked = 0;
    for (int seedIndex = 0; seedIndex < (int)(sizeof(validationSeeds) / sizeof(validationSeeds[0]));
         seedIndex++) {
        g_environmentSeed = validationSeeds[seedIndex];
        for (int sector = 0; sector < 4096; sector += 31) {
            double virtualPlayerZ = (double)sector * SECTOR_DEPTH + 3.25;
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
                if (fabsf(structure->position.x) >= CORRIDOR_EDGE_X) {
                    int sideBin = structure->position.x < 0.0f ? 0 : 1;
                    float binF = (fabsf(structure->position.x) - fieldBinStart) /
                                 (fieldBinEnd - fieldBinStart);
                    int bin = (int)floorf(binF * (float)FIELD_BIN_COUNT);
                    if (bin < 0) bin = 0;
                    if (bin >= FIELD_BIN_COUNT) bin = FIELD_BIN_COUNT - 1;
                    fieldBins[sideBin][bin]++;
                    fieldTotal++;
                }
                if (structure->type == STRUCT_BUS_CONDUIT) {
                    float leftX = structure->position.x - structure->size.x * 0.5f;
                    float rightX = structure->position.x + structure->size.x * 0.5f;
                    if (leftX < CONDUIT_PLAYER_MAX_X && rightX > -CONDUIT_PLAYER_MAX_X) {
                        if (bottom <= CONDUIT_PLAYER_MAX_Y) {
                            report->crossingClearanceViolations++;
                        }
                    }

                    Vector3 flowDir = { 0 };
                    float flowPhase = 0.0f;
                    ComputeConduitFlow(structure, &flowDir, &flowPhase);
                    float dirLen = Vector3Length(flowDir);
                    if (fabsf(dirLen - 1.0f) > 0.02f) {
                        report->conduitFlowViolations++;
                    }
                    if (structure->size.x > structure->size.y && structure->size.x > structure->size.z) {
                        if (fabsf(fabsf(flowDir.x) - 1.0f) > 0.02f) report->conduitFlowViolations++;
                    } else if (structure->size.z > structure->size.x && structure->size.z > structure->size.y) {
                        if (fabsf(fabsf(flowDir.z) - 1.0f) > 0.02f) report->conduitFlowViolations++;
                    } else if (structure->size.y > structure->size.x && structure->size.y > structure->size.z) {
                        if (fabsf(fabsf(flowDir.y) - 1.0f) > 0.02f) report->conduitFlowViolations++;
                    }
                    if (!isfinite(flowPhase)) {
                        report->conduitFlowViolations++;
                    }
                    totalConduitsChecked++;
                    if (flowPhase < minConduitPhase) minConduitPhase = flowPhase;
                    if (flowPhase > maxConduitPhase) maxConduitPhase = flowPhase;
                }
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

    if (totalConduitsChecked > 10 && (maxConduitPhase - minConduitPhase < 0.5f)) {
        report->conduitFlowViolations++;
    }

    if (fieldTotal == 0) {
        report->fieldCoverageViolations++;
    } else {
        for (int side = 0; side < 2; side++) {
            if (fieldBins[side][0] == 0) report->fieldCoverageViolations++;
            int farReach = fieldBins[side][FIELD_BIN_COUNT - 2] + fieldBins[side][FIELD_BIN_COUNT - 1];
            if (farReach == 0) report->fieldCoverageViolations++;
        }
        for (int bin = 0; bin < FIELD_BIN_COUNT; bin++) {
            int binTotal = fieldBins[0][bin] + fieldBins[1][bin];
            if (binTotal * 100 > fieldTotal * 60) report->fieldCoverageViolations++;
        }
    }

    return report->adjacentRepeatViolations == 0 &&
           report->landmarkSpacingViolations == 0 &&
           report->fieldCoverageViolations == 0 &&
           report->crossingClearanceViolations == 0 &&
           report->conduitFlowViolations == 0 &&
           report->droppedStructures == 0 &&
           report->unsupportedStructures == 0;
}

// Batches active structures by type and issues one instanced draw call per type
static void DrawStructureBatch(const EnvironmentSystem *env, StructureType type, Color accent, Color body,
                               double virtualPlayerZ) {
    static Matrix transforms[MAX_STRUCTURES];
    int count = 0;

    for (int i = 0; i < env->structureCount; i++) {
        const EnvironmentStructure *s = &env->structures[i];
        if (!s->active || s->type != type) continue;

        Vector3 renderPos = (Vector3){ s->position.x, s->position.y, s->position.z };
        Matrix mat = MatrixMultiply(MatrixScale(s->size.x, s->size.y, s->size.z),
                                    MatrixTranslate(renderPos.x, renderPos.y, renderPos.z));
        if (type == STRUCT_BUS_CONDUIT) {
            Vector3 flowDir = { 0 };
            float phaseOffset = 0.0f;
            ComputeConduitFlow(s, &flowDir, &phaseOffset);
            mat.m3 = flowDir.x * s->size.x;
            mat.m7 = flowDir.y * s->size.y;
            mat.m11 = flowDir.z * s->size.z;
            mat.m15 = phaseOffset;
        } else if (type == STRUCT_MEMORY_SLAB) {
            bool isPedestal = (s->size.y < 0.65f);
            if (!isPedestal) {
                int32_t worldXKey = (int32_t)lroundf(s->position.x * 16.0f);
                int64_t worldZKey = llround((virtualPlayerZ - s->position.z) * 8.0);
                uint64_t worldZBits = (uint64_t)worldZKey;
                uint32_t foldedWorldZ = (uint32_t)worldZBits ^ (uint32_t)(worldZBits >> 32);
                uint32_t slabHash = PaletteHash(env->runSeed ^
                                                ((uint32_t)worldXKey * UINT32_C(0x9e3779b9)) ^
                                                (foldedWorldZ * UINT32_C(0x85ebca6b)));
                // Slabs have active matrix text flow on designated sides
                // ~65% of memory slabs carry active data rain
                if ((slabHash % 100u) < 65u) {
                    uint32_t faceRoll = (slabHash >> 8) % 10u;
                    // 1.0 = corridor-facing inner side, 2.0 = oncoming front face (+Z), 3.0 = both
                    float faceMode = (faceRoll < 4u) ? 1.0f : ((faceRoll < 7u) ? 2.0f : 3.0f);
                    float phaseOffset = (float)(slabHash & 2047u) * 0.00306796f;
                    mat.m3 = faceMode;
                    mat.m7 = phaseOffset;
                    mat.m11 = s->size.y;
                    mat.m15 = (faceMode >= 1.5f && faceMode < 2.5f) ? s->size.x : s->size.z;
                }
            }
        }
        transforms[count++] = mat;
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
                                        double virtualPlayerZ) {
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
        int64_t worldZKey = llround((virtualPlayerZ - s->position.z) * 8.0);
        uint64_t worldZBits = (uint64_t)worldZKey;
        uint32_t foldedWorldZ = (uint32_t)worldZBits ^ (uint32_t)(worldZBits >> 32);
        uint32_t identity = (uint32_t)worldXKey * UINT32_C(0x9e3779b9) ^
                    foldedWorldZ * UINT32_C(0x85ebca6b) ^
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
            if (fabsf(position.x) < CORRIDOR_EDGE_X) continue;
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
            if (fabsf(position.x) < CORRIDOR_EDGE_X) continue;
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

void DrawEnvironment(const EnvironmentSystem *env, Camera3D camera, double virtualPlayerZ) {
    (void)camera;
    (void)virtualPlayerZ;

    DrawTerrainBatch(env);

    DrawFarStructureBatch(env, STRUCT_CACHE_TOWER, env->primaryColor, (Color){ 4, 8, 18, 255 });
    DrawFarStructureBatch(env, STRUCT_MEMORY_SLAB, env->secondaryColor, (Color){ 3, 9, 13, 255 });

    // Pass 2: Cyberspace monolithic architecture, batched by type (unified cyan-blue dominant palette)
    DrawStructureBatch(env, STRUCT_CACHE_TOWER, env->primaryColor, (Color){ 5, 10, 25, 255 }, virtualPlayerZ);
    DrawStructureBatch(env, STRUCT_MEMORY_SLAB, env->secondaryColor, (Color){ 3, 12, 16, 255 }, virtualPlayerZ);
    DrawStructureBatch(env, STRUCT_BUS_CONDUIT, env->primaryColor, (Color){ 3, 10, 24, 255 }, virtualPlayerZ);
    DrawStructureBatch(env, STRUCT_LANDMARK, env->landmarkColor, (Color){ 24, 4, 18, 255 }, virtualPlayerZ);

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
