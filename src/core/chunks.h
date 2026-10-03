// Chunk streaming. Chunk k covers s in [k*C, (k+1)*C). Lifecycle:
// EMPTY -> BUILT (CPU, pure) -> UPLOADED (GPU, visible) -> released back to EMPTY.
// Fixed-size pool; CPU buffers are reused so steady state does not allocate.
#ifndef CT_CHUNKS_H
#define CT_CHUNKS_H

#include <stdint.h>
#include <stdbool.h>
#include "dvec.h"
#include "render/linegeo.h"

#define CHUNK_LEN          48.0
#define CHUNK_POOL         24
#define CHUNK_BATCHES      8     // material slots per chunk (meaning defined by the environment)
#define CHUNK_MAX_MESHES   32
#define CHUNK_UNIFORMS     96    // vec4 slots of per-chunk uniform data (layout defined by the environment)
#define CHUNK_MAX_DEBRIS   16

typedef struct DebrisInfo {
    DVec3  center;           // world space
    float  radius;
    double s;
} DebrisInfo;

typedef struct ChunkCpu {
    int        index;
    DVec3      origin;       // world position of path at chunk start; meshes are relative to it
    double     s0, s1;
    LineGeo    geo[CHUNK_BATCHES];
    float      uniforms[CHUNK_UNIFORMS][4];
    DebrisInfo debris[CHUNK_MAX_DEBRIS];
    int        debrisCount;
    int        archetype;    // environment-defined tag (debug display)
} ChunkCpu;

typedef struct ChunkGpu {
    int        index;
    DVec3      origin;
    double     s0, s1;
    Mesh       meshes[CHUNK_MAX_MESHES];
    int        meshBatch[CHUNK_MAX_MESHES];
    int        meshCount;
    int        vertexCount;
    float      uniforms[CHUNK_UNIFORMS][4];
    DebrisInfo debris[CHUNK_MAX_DEBRIS];
    int        debrisCount;
    int        archetype;
    double     fadeStart;    // time when it became visible
} ChunkGpu;

typedef enum ChunkState { CHUNK_EMPTY = 0, CHUNK_BUILT, CHUNK_UPLOADED } ChunkState;

typedef struct ChunkSlot {
    ChunkState state;
    int        index;
    ChunkCpu   cpu;
    ChunkGpu   gpu;
} ChunkSlot;

typedef struct ChunkStats {
    int    active, builtThisFrame, uploadedThisFrame;
    int    gpuMeshes, vertexCount;
    double lastBuildMs, maxBuildMs, lastUploadMs, maxUploadMs;
    double frameMs, maxFrameMs;   // streaming time within one chunks_update call (budgeted calls only)
} ChunkStats;

struct Environment;
struct PathSystem;

typedef struct ChunkManager {
    ChunkSlot  slots[CHUNK_POOL];
    int        first, last;  // desired window [first, last]
    ChunkStats stats;
    const ChunkGpu *visible[CHUNK_POOL];
    int        visibleCount;
} ChunkManager;

void chunks_init(ChunkManager *cm);
void chunks_release_all(ChunkManager *cm, struct Environment *env);
void chunks_free(ChunkManager *cm, struct Environment *env);
// Streams the window around sShip with the given per-frame budget (<= 0 means unlimited).
void chunks_update(ChunkManager *cm, struct Environment *env, const struct PathSystem *path,
                   double sShip, double viewDist, double time, int buildBudget, int uploadBudget);
// Visible chunks sorted far -> near.
const ChunkGpu *const *chunks_visible(const ChunkManager *cm, int *count);

int  chunk_index_at(double s);

// Generic GPU upload of every LineGeo batch (environments can call this from uploadChunk).
void chunk_upload_default(ChunkCpu *in, ChunkGpu *out);
void chunk_release_default(ChunkGpu *gpu);
// Reset CPU chunk for (re)building.
void chunk_cpu_begin(ChunkCpu *c, const struct PathSystem *path, int index);

#endif
