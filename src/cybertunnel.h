// Cyberspace tunnel module — public API.
//
// Typical use (module owns camera and post):
//   CtConfig cfg = ct_default_config(); cfg.seed = 42;
//   CtTunnel *ct = ct_create(&cfg);            // after InitWindow (needs a GL context)
//   loop:
//     ct_set_input(ct, &input);
//     ct_update(ct, GetFrameTime());
//     BeginDrawing(); ct_render(ct, NULL, w, h); EndDrawing();
//   ct_destroy(ct);                            // before CloseWindow
//
// World positions are double precision. Rendering happens in "render space" =
// world - renderOrigin (see ct_render_origin), which keeps float coordinates small.
#ifndef CYBERTUNNEL_H
#define CYBERTUNNEL_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "raylib.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CtTunnel CtTunnel;

enum { CT_ENV_VORTEX = 0, CT_ENV_TEMPLATE = 1 };

typedef struct CtConfig {
    uint64_t    seed;
    int         width, height;   // initial render size
    int         environment;     // CT_ENV_*
    bool        ownCamera;       // true: built-in chase camera; false: game calls ct_set_camera_world()
    bool        usePost;         // false: ct_render() leaves the HDR result in ct_scene_texture()
    bool        hdr16;           // RGBA16F scene target (falls back to RGBA8 if unsupported)
    bool        drawShip;        // draw the placeholder ship and trails
    const char *shaderDir;       // NULL = compiled-in default
    const char *presetDir;       // NULL = compiled-in default
} CtConfig;

typedef struct CtInput {
    float steerX, steerY;        // -1..1
    bool  boost;
    bool  rollLeft, rollRight;   // barrel roll triggers
} CtInput;

typedef struct CtPathSample {
    double  pos[3];              // world
    Vector3 tangent, normal, binormal;   // normal = up-ish, binormal = right
    float   radius, roll, curvature;
} CtPathSample;

typedef struct CtDebris {
    double center[3];            // world
    float  radius;               // bounding sphere
    double s;
} CtDebris;

typedef struct CtStats {
    float  frameMs;
    double sShip;
    float  speed, intensity;
    int    activeChunks, buildsThisFrame, uploadsThisFrame;
    int    vertices, drawCalls, gpuMeshes;
    double maxChunkBuildMs, lastChunkBuildMs;
    double maxStreamFrameMs, lastStreamFrameMs;   // build + upload time spent in one frame
    double renderOrigin[3];
    float  maxRenderCoord;       // largest |coordinate| passed to GL this run
    double msUpdate, msScene, msBloom, msComposite;
    const char *chunkLabel;
} CtStats;

CtConfig  ct_default_config(void);
CtTunnel *ct_create(const CtConfig *cfg);
void      ct_destroy(CtTunnel *ct);

// Environments
int         ct_environment_count(void);
const char *ct_environment_name(int id);
void        ct_set_environment(CtTunnel *ct, int id);
int         ct_get_environment(const CtTunnel *ct);

// Per frame
void ct_set_input(CtTunnel *ct, const CtInput *in);
void ct_update(CtTunnel *ct, float dt);
// target NULL = currently bound framebuffer (e.g. inside BeginDrawing) of size width x height.
void ct_render(CtTunnel *ct, const RenderTexture2D *target, int width, int height);
Texture2D ct_scene_texture(const CtTunnel *ct);  // HDR scene color (for usePost = false)

// Camera when cfg.ownCamera == false (world space, double precision).
void ct_set_camera_world(CtTunnel *ct, const double pos[3], const double target[3], Vector3 up, float fovyDeg);

// Queries
CtPathSample ct_path_sample(const CtTunnel *ct, double s);
float        ct_tunnel_radius_at(const CtTunnel *ct, double s);
int          ct_debris_in_range(const CtTunnel *ct, double s0, double s1, CtDebris *out, int max);
Vector2      ct_vanishing_point(const CtTunnel *ct);   // uv 0..1, y up
double       ct_ship_s(const CtTunnel *ct);
void         ct_ship_world(const CtTunnel *ct, double out[3]);
void         ct_render_origin(const CtTunnel *ct, double out[3]);
void         ct_get_stats(const CtTunnel *ct, CtStats *out);

// Control
void ct_set_intensity(CtTunnel *ct, float intensity);   // < 0 = automatic pacing
void ct_trigger_whiteout(CtTunnel *ct);
void ct_set_whiteout(CtTunnel *ct, float whiteout);     // manual override, < 0 = animation
void ct_jump_to(CtTunnel *ct, double s);
void ct_reseed(CtTunnel *ct, uint64_t seed);
uint64_t ct_get_seed(const CtTunnel *ct);
void ct_set_paused(CtTunnel *ct, bool paused);
void ct_set_time_scale(CtTunnel *ct, float scale);
void ct_rebuild(CtTunnel *ct);                           // regenerate path and chunks

// Parameters (names as listed in INTEGRATION.md)
bool  ct_param_set(CtTunnel *ct, const char *name, float value);
float ct_param_get(CtTunnel *ct, const char *name);
bool  ct_preset_save(CtTunnel *ct, const char *nameOrPath);
bool  ct_preset_load(CtTunnel *ct, const char *nameOrPath);
// Writes every registered parameter (group, name, type, default, min, max) as a Markdown table.
void  ct_params_write_markdown(CtTunnel *ct, FILE *out);

// Debug tooling (optional)
void ct_debug_handle_input(CtTunnel *ct);       // F1 panel, F2 free cam, F3 overlay, R, P, W, 1-9
void ct_debug_draw(CtTunnel *ct, int width, int height);
void ct_debug_show(CtTunnel *ct, bool panel, bool overlay);
void ct_reset_stats(CtTunnel *ct);              // clears max values (e.g. after warm-up)
bool ct_debug_wants_screenshot(CtTunnel *ct);   // F12 pressed this frame
void ct_screenshot_name(const CtTunnel *ct, char *buf, int size);

#ifdef __cplusplus
}
#endif

#endif
