// HDR render pipeline: RGBA16F scene target with depth, dual-Kawase bloom,
// single-pass composite (zoom blur, chromatic aberration, ACES, vignette,
// grain, whiteout) to an sRGB output.
#ifndef CT_PIPELINE_H
#define CT_PIPELINE_H

#include <stdbool.h>
#include "raylib.h"
#include "shaderlib.h"
#include "core/params.h"

#define PIPE_MAX_MIPS 8

typedef struct PostSettings {
    bool  bloomOn, zoomBlurOn, caOn, tonemapOn, vignetteOn, grainOn, fxaaOn;
    float bloomThreshold, bloomKnee, bloomIntensity, bloomRadius;
    int   bloomMips;
    bool  bloomQuarterRes;
    float zoomBlur, zoomBlurAtBase, zoomBlurInner, zoomBlurOuter;   // zoomBlur: strength at full boost
    int   zoomTaps;
    float caPixels;
    float exposure;
    float vignette;
    float grain;
    float saturation;
    // Planar floor reflection (environments with a floor): 0 off, 1 quarter, 2 half resolution.
    int   reflectRes;
    float reflectBlur;           // vertical blur radius (texels of the reflection target)
} PostSettings;

typedef struct HdrTarget {
    unsigned int fbo;
    Texture2D    color;
    unsigned int depth;
    int          w, h;
} HdrTarget;

typedef struct PipelineTimings { double scene, bloom, composite; } PipelineTimings;

typedef struct Pipeline {
    ShaderLib *sl;
    int        shPrefilter, shDown, shUp, shComposite;
    HdrTarget  scene;
    HdrTarget  down[PIPE_MAX_MIPS], up[PIPE_MAX_MIPS];
    int        mipCount;         // allocated levels
    int        shReflBlur;
    HdrTarget  refl, reflTmp, reflOut;   // mirrored scene (with depth), blur ping-pong
    int        reflDiv;          // allocated resolution divisor, 0 = none
    int        w, h;
    bool       hdr16;
    bool       quarterAllocated;
    PipelineTimings ms;
    int        drawCalls;
} Pipeline;

void post_register_params(PostSettings *s, ParamRegistry *reg);

bool pipe_init(Pipeline *p, ShaderLib *sl, int w, int h, bool hdr16, bool quarterBloom);
void pipe_resize(Pipeline *p, int w, int h, bool quarterBloom);
void pipe_unload(Pipeline *p);

// Binds the scene target, clears it and loads the given matrices; depth test on.
void pipe_begin_scene(Pipeline *p, Matrix view, Matrix proj, Color clear);
void pipe_set_emissive(bool on);   // additive + depth-write off, or back to opaque state
void pipe_end_scene(Pipeline *p);

// Bloom + composite into dst (NULL = currently bound default framebuffer of size outW x outH).
void pipe_post(Pipeline *p, const PostSettings *s, RenderTexture2D *dst, int outW, int outH,
               Vector2 vanishingUV, float zoomStrength, float whiteout, float time);
// Draws a fullscreen quad in NDC (shader must use fullscreen.vs conventions).
void pipe_fullscreen_quad(void);
RenderTexture2D pipe_scene_rt(const Pipeline *p);

// Planar reflection: render the mirrored scene into a 1/div resolution target (allocated on
// demand), then blur it vertically. The result is sampled by the floor at its own screen uv.
void      pipe_begin_reflection(Pipeline *p, int div, Matrix view, Matrix proj);
void      pipe_end_reflection(Pipeline *p);
Texture2D pipe_blur_reflection(Pipeline *p, float radius);

#endif
