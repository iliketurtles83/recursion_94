#ifndef ENVIRONMENT_H
#define ENVIRONMENT_H

#include "raylib.h"
#include <stdbool.h>
#include <stdint.h>

#define MAX_STRUCTURES 1024
#define SECTOR_DEPTH 16.0f
#define DRAW_DISTANCE 384.0f
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
} EnvironmentStructure;

typedef struct {
    int zonesChecked;
    int adjacentRepeatViolations;
    int landmarkSpacingViolations;
    int peakStructureCount;
    int droppedStructures;
} EnvironmentValidationReport;

typedef struct {
    uint32_t runSeed;
    Color primaryColor;
    Color secondaryColor;
    Color landmarkColor;

    Model floorModel;
    int floorScrollLoc;
    int floorTimeLoc;
    int floorIntensityLoc;
    int floorSeedLoc;
    int floorPrimaryLoc;
    int floorSecondaryLoc;

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
    int towerKindLoc;

    EnvironmentStructure structures[MAX_STRUCTURES];
    int structureCount;
    int droppedStructures;
    int emissionSector;
    int emissionSerial;
} EnvironmentSystem;

// Lifecycle & Update API
void InitEnvironment(EnvironmentSystem *env, uint32_t runSeed);
void UpdateEnvironment(EnvironmentSystem *env, float virtualPlayerZ, float time, float intensity);
void DrawEnvironment(const EnvironmentSystem *env, Camera3D camera, float virtualPlayerZ);
void UnloadEnvironment(EnvironmentSystem *env);
bool ValidateEnvironmentGenerator(EnvironmentValidationReport *report);

#endif // ENVIRONMENT_H
