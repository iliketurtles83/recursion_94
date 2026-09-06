#ifndef ENVIRONMENT_H
#define ENVIRONMENT_H

#include "raylib.h"
#include <stdbool.h>
#include <stdint.h>

#define MAX_STRUCTURES 1024
#define MAX_TERRAIN_INSTANCES 1024
#define MAX_FAR_STRUCTURES 128
#define SECTOR_DEPTH 16.0f
#define DRAW_DISTANCE 384.0f
#define TERRAIN_DRAW_DISTANCE 640.0f
#define DISTRICT_SECTORS 16

typedef enum {
    STRUCT_VOID = 0,
    STRUCT_CACHE_TOWER,
    STRUCT_MEMORY_SLAB,
    STRUCT_BUS_CONDUIT,
    STRUCT_LANDMARK
} StructureType;

typedef struct {
    Vector3 position;
    Vector3 size;
    StructureType type;
    float compileScale; // 0.0 to 1.0 based on horizon proximity
    bool active;
    Vector3 edgeStart;
    Vector3 edgeEnd;
    float edgeLength;
    uint32_t edgeSeed;
} EnvironmentStructure;

typedef struct {
    Vector3 position;
    Vector3 size;
} TerrainInstance;

typedef struct {
    int zonesChecked;
    int adjacentRepeatViolations;
    int landmarkSpacingViolations;
    int peakStructureCount;
    int droppedStructures;
    int peakTerrainCount;
    int peakFarStructureCount;
    int unsupportedStructures;
    int unsupportedFlankStructures;
    int unsupportedRavineStructures;
    int unsupportedFarStructures;
    int fieldCoverageViolations;
    int crossingClearanceViolations;
    int conduitFlowViolations;
} EnvironmentValidationReport;

typedef struct {
    uint32_t runSeed;
    Color primaryColor;
    Color secondaryColor;
    Color landmarkColor;
    Color threatColor;
    Color shadowBodyColor;
    Color skyZenithColor;
    Color skyHorizonColor;

    Mesh unitCubeMesh;
    Mesh unitPrismMesh;
    Material towerMaterial;
    int towerScrollLoc;
    int towerTimeLoc;
    int towerIntensityLoc;
    int towerSeedLoc;
    int towerPrimaryLoc;
    int towerSecondaryLoc;
    int towerAccentLoc;
    int towerBodyLoc;
    int towerSkyLoc;
    int towerHotLoc;
    int towerKindLoc;
    int towerBeatPulseLoc;

    Material terrainMaterial;
    int terrainTimeLoc;
    int terrainIntensityLoc;
    int terrainSeedLoc;
    int terrainPrimaryLoc;
    int terrainSecondaryLoc;
    int terrainBodyLoc;
    int terrainSkyLoc;
    TerrainInstance terrain[MAX_TERRAIN_INSTANCES];
    int terrainCount;

    EnvironmentStructure farStructures[MAX_FAR_STRUCTURES];
    int farStructureCount;

    EnvironmentStructure structures[MAX_STRUCTURES];
    int structureCount;
    int droppedStructures;
    int emissionSector;
    int emissionSerial;
} EnvironmentSystem;

// Lifecycle & Update API
void InitEnvironment(EnvironmentSystem *env, uint32_t runSeed);
void ReseedEnvironment(EnvironmentSystem *env, uint32_t runSeed);
void UpdateEnvironment(EnvironmentSystem *env, double virtualPlayerZ, float time, float intensity, float beatPulse);
void DrawEnvironment(const EnvironmentSystem *env, Camera3D camera, double virtualPlayerZ);
void UnloadEnvironment(EnvironmentSystem *env);
bool ValidateEnvironmentGenerator(EnvironmentValidationReport *report);

#endif // ENVIRONMENT_H
