#include "streaks.h"
#include "env/env_common.h"
#include "render/rmath.h"
#include <math.h>
#include <stddef.h>

static void build_mesh(StreakSystem *s)
{
    LineGeo *g = &s->geo;
    lg_clear(g);
    for (int i = 0; i < s->count; i++) {
        int b = lg_reserve(g, 4, 6);
        Vector2 id = { (float)i, 0.0f };
        Vector3 z = { 0 };
        Vector4 t = { 0 };
        lg_vertex(g, z, (Vector2){ 0, -1 }, id, z, t, WHITE);
        lg_vertex(g, z, (Vector2){ 0, 1 }, id, z, t, WHITE);
        lg_vertex(g, z, (Vector2){ 1, 1 }, id, z, t, WHITE);
        lg_vertex(g, z, (Vector2){ 1, -1 }, id, z, t, WHITE);
        lg_quad(g, b, 0, 1, 2, 3);
    }
    s->mesh = lg_upload_mesh(g, 0, false);
    s->uploaded = true;
}

void streaks_init(StreakSystem *s, ShaderLib *sl, int count)
{
    s->sl = sl;
    s->count = count;
    if (!sl) return;
    s->sh = sl_load(sl, "streak.vs", "streak.fs");
    s->mat = LoadMaterialDefault();
    build_mesh(s);
}

void streaks_draw(StreakSystem *s, const StreakParams *p, const FrameContext *ctx)
{
    if (!s->uploaded) return;
    ShaderLib *sl = s->sl;
    int sh = s->sh;
    // Path frames along the streak volume so streaks follow the path's curvature.
    enum { PATH_N = 16 };
    float pp[PATH_N][4], pb[PATH_N][4], pn[PATH_N][4];
    double step = (double)p->zRange / (PATH_N - 1);
    for (int i = 0; i < PATH_N; i++) {
        PathSample q = path_sample(ctx->path, ctx->sShip - p->zBehind + step * i);
        DVec3 o = dv_sub(q.pos, ctx->renderOrigin);
        pp[i][0] = (float)o.x; pp[i][1] = (float)o.y; pp[i][2] = (float)o.z; pp[i][3] = (float)q.radius;
        pb[i][0] = (float)q.B.x; pb[i][1] = (float)q.B.y; pb[i][2] = (float)q.B.z; pb[i][3] = 0.0f;
        pn[i][0] = (float)q.N.x; pn[i][1] = (float)q.N.y; pn[i][2] = (float)q.N.z; pn[i][3] = 0.0f;
    }
    // Travel wrapped so quantized multipliers (k/8) stay seamless: period = zRange * 8 * 8.
    double period = (double)p->zRange * 64.0;
    float travel = (float)fmod(ctx->sShip, period);
    float energy = 0.6f + 0.8f * ctx->intensity;
    env_set_common_uniforms(sl, sh, ctx);
    sl_vec4v(sl, sh, "uPathP", &pp[0][0], PATH_N);
    sl_vec4v(sl, sh, "uPathB", &pb[0][0], PATH_N);
    sl_vec4v(sl, sh, "uPathN", &pn[0][0], PATH_N);
    sl_f1(sl, sh, "uPathStep", (float)step);
    sl_f1(sl, sh, "uTravel", travel);
    sl_f1(sl, sh, "uSpeed", ctx->speed * p->speedMul * (0.7f + 0.6f * ctx->intensity) * (1.0f + ctx->speedNorm));
    sl_f1(sl, sh, "uZRange", p->zRange);
    sl_f1(sl, sh, "uZBehind", p->zBehind);
    sl_f1(sl, sh, "uDensity", p->density * (0.55f + 0.45f * ctx->intensity));
    sl_f1(sl, sh, "uLenMul", p->lenMul);
    sl_f2(sl, sh, "uWidth", p->wMin, p->wMax);
    sl_f1(sl, sh, "uHelixFrac", p->helixFrac);
    sl_f1(sl, sh, "uHelixRate", p->helixRate);
    sl_f1(sl, sh, "uCometFrac", p->cometFrac);
    sl_f1(sl, sh, "uPurpleFrac", p->accentFrac);
    sl_f1(sl, sh, "uStreakIntensity", p->intensity * energy);
    sl_v3(sl, sh, "uColWhite", ct_srgb_to_linear(p->colMain));
    sl_v3(sl, sh, "uColPurple", ct_srgb_to_linear(p->colAccent));
    sl_f1(sl, sh, "uVolume", (float)p->volume);
    sl_f3(sl, sh, "uSlab", p->slabHalfWidth, p->slabYMin, p->slabYMax);
    sl_f1(sl, sh, "uFallTilt", p->fallTilt);
    s->mat.shader = sl_shader(sl, sh);
    DrawMesh(s->mesh, s->mat, MatrixIdentity());
    ctx->stats->drawCalls++;
    ctx->stats->vertices += s->mesh.vertexCount;
}

void streaks_unload(StreakSystem *s)
{
    if (s->uploaded) lg_unload_mesh(s->mesh);
    s->uploaded = false;
    lg_free(&s->geo);
    if (s->sl) { s->mat.shader.id = rlGetShaderIdDefault(); UnloadMaterial(s->mat); }
    s->sl = NULL;
}
