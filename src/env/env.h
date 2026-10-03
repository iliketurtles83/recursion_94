// Environment interface. The core owns path, chunk lifetime, ship, camera and
// post-processing. An environment produces content for chunks, draws it and
// declares how the shared path behaves inside it.
#ifndef CT_ENV_H
#define CT_ENV_H

#include <stdint.h>
#include "raylib.h"
#include "core/path.h"
#include "core/chunks.h"
#include "core/params.h"

struct ShaderLib;

typedef struct RenderStats {
    int drawCalls;
    int vertices;
} RenderStats;

// Shared emissive look settings owned by the core (fog / fade / core tint).
typedef struct Atmosphere {
    float fogStart, fogEnd, fogToCore;
    float farFadeStart, farFadeEnd;
    float nearFadeStart, nearFadeEnd;
    float minPixelWidth;
    Vector3 coreColor;
} Atmosphere;

typedef struct FrameContext {
    double   time;           // seconds since start (sim time, scaled)
    float    shaderTime;     // time wrapped for shaders
    float    dt;
    double   sShip;          // ship arc-length position on path
    float    speed;          // units/s
    float    speedNorm;      // 0 at base speed, 1 at boost speed
    float    intensity;      // 0..1 global "energy"
    float    whiteout;       // 0..1
    float    flare;          // extra core flare 0..1
    Camera3D camera;         // in render space (origin-rebased)
    DVec3    renderOrigin;   // world offset of render space
    Matrix   view, proj;
    float    fovy;           // degrees
    int      width, height;
    float    pixelScale;     // world size of one pixel at distance 1
    Vector2  vanishingUV;    // 0..1, y up
    Atmosphere atmo;
    const PathSystem *path;
    struct ShaderLib *shaders;
    RenderStats *stats;
} FrameContext;

typedef struct Environment {
    const char *name;
    int         id;
    void       *state;
    PathStyle (*pathStyle)(const struct Environment *);
    void (*init)(struct Environment *, uint64_t worldSeed, struct ShaderLib *shaders);
    void (*registerParams)(struct Environment *, ParamRegistry *reg);
    void (*reseed)(struct Environment *, uint64_t worldSeed);
    // Pure CPU, deterministic: output depends only on (seed, params, chunkIndex, path). No GL calls.
    void (*buildChunk)(struct Environment *, const PathSystem *, int chunkIndex, ChunkCpu *out);
    void (*uploadChunk)(struct Environment *, ChunkCpu *in, ChunkGpu *out);   // GL upload only
    void (*releaseChunk)(struct Environment *, ChunkGpu *);
    // Pacing energy 0..1 the environment wants at arc length s (drives global intensity).
    float (*energyAt)(const struct Environment *, const PathSystem *, double s);
    void (*update)(struct Environment *, const FrameContext *);
    void (*drawBackground)(struct Environment *, const FrameContext *);
    void (*drawOpaque)(struct Environment *, const FrameContext *, const ChunkGpu *const *chunks, int n);
    void (*drawEmissive)(struct Environment *, const FrameContext *, const ChunkGpu *const *chunks, int n);
    const char *(*chunkLabel)(const struct Environment *, int archetype);
    void (*unload)(struct Environment *);
} Environment;

// Environment registry
enum { ENV_VORTEX = 0, ENV_TEMPLATE = 1, ENV_COUNT };
Environment env_vortex_create(void);
Environment env_template_create(void);

#endif
