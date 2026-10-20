// Cyberspace tunnel module: owns path, chunk streaming, flight/camera, render
// pipeline and environments. See cybertunnel.h for the public API.
#include "ct_internal.h"
#include "env/env_common.h"
#include "render/rmath.h"
#include "render/shipmodel.h"
#include "rlgl.h"
#include "raymath.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CT_SHADER_DIR
#define CT_SHADER_DIR "src/render/shaders"
#endif
#ifndef CT_PRESET_DIR
#define CT_PRESET_DIR "presets"
#endif

#define SIM_DT (1.0 / FLIGHT_HZ)

CtConfig ct_default_config(void)
{
    CtConfig c;
    memset(&c, 0, sizeof(c));
    c.seed = 1;
    c.width = 1280;
    c.height = 720;
    c.environment = CT_ENV_VORTEX;
    c.ownCamera = true;
    c.usePost = true;
    c.hdr16 = true;
    c.drawShip = true;
    return c;
}

int ct_environment_count(void) { return ENV_COUNT; }

const char *ct_environment_name(int id)
{
    switch (id) {
    case ENV_VORTEX: return "Data Vortex";
    case ENV_TEMPLATE: return "Template";
    case ENV_CITY: return "Neon Grid City";
    case ENV_CORRIDOR: return "Server Corridor";
    case ENV_GREYBOX: return "Greybox";
    }
    return "?";
}

// ---------------------------------------------------------------------------
// Parameters

static void register_core_params(CtTunnel *ct)
{
    ParamRegistry *reg = &ct->reg;
    CoreParams *c = &ct->core;
    const char *g = "Sim";
    params_float(reg, g, "timeScale", &c->timeScale, 1.0f, 0.0f, 4.0f);
    params_float(reg, g, "intensityOverride", &c->intensityOverride, -1.0f, -1.0f, 1.0f);
    params_float(reg, g, "intensityResponse", &c->intensityResponse, 0.6f, 0.05f, 10.0f);
    params_float(reg, g, "whiteoutDuration", &c->whiteoutDuration, 1.8f, 0.2f, 10.0f);
    params_int(reg, g, "buildBudget", &c->buildBudget, 1, 1, 8);
    params_int(reg, g, "uploadBudget", &c->uploadBudget, 1, 1, 8);
    params_bool(reg, g, "showShip", &c->showShip, true);
    params_bool(reg, g, "shipDart", &ct->fp.dartShip, false);   // original placeholder dart instead of the courier
    params_bool(reg, g, "showPathDebug", &c->showPathDebug, false);
    params_float(reg, g, "rebaseDist", &c->rebaseDist, (float)CT_REBASE_DIST, 1.0f, 100000.0f);
    params_float(reg, g, "engineIntensity", &c->engineIntensity, 10.0f, 0.0f, 40.0f);
    params_float(reg, g, "trailIntensity", &c->trailIntensity, 2.2f, 0.0f, 20.0f);
    params_float(reg, g, "trailWidth", &c->trailWidth, 0.035f, 0.005f, 0.5f);

    flight_register_params(&ct->fp, reg);

    g = "Fog";
    Atmosphere *a = &ct->atmo;
    params_float(reg, g, "fogStart", &a->fogStart, 60.0f, 0.0f, 600.0f);
    params_float(reg, g, "fogEnd", &a->fogEnd, 300.0f, 1.0f, 800.0f);
    params_float(reg, g, "fogToCore", &a->fogToCore, 0.6f, 0.0f, 1.0f);
    params_float(reg, g, "farFadeStart", &a->farFadeStart, 220.0f, 0.0f, 800.0f);
    params_float(reg, g, "farFadeEnd", &a->farFadeEnd, 390.0f, 1.0f, 900.0f);
    params_float(reg, g, "nearFadeStart", &a->nearFadeStart, 0.5f, 0.0f, 10.0f);
    params_float(reg, g, "nearFadeEnd", &a->nearFadeEnd, 2.5f, 0.0f, 20.0f);
    params_float(reg, g, "minPixelWidth", &a->minPixelWidth, 0.7f, 0.0f, 4.0f);
    params_float(reg, g, "coreR", &c->coreR, 0.91f, 0.0f, 1.0f);
    params_float(reg, g, "coreG", &c->coreG, 0.98f, 0.0f, 1.0f);
    params_float(reg, g, "coreB", &c->coreB, 1.0f, 0.0f, 1.0f);

    post_register_params(&ct->post, reg);
}

static void register_env_params(CtTunnel *ct)
{
    ct->reg.count = ct->envParamStart;
    if (ct->env->registerParams) ct->env->registerParams(ct->env, &ct->reg);
}

// ---------------------------------------------------------------------------
// Detailed courier ship, articulated nozzles and volumetric jet meshes.

static void upload_static(Mesh *m, LineGeo *g, bool dynamic)
{
    *m = lg_upload_mesh(g, 0, dynamic);
}

// The original placeholder dart (shipDart): flat dark hull, cyan edges, two engine glows.
static void build_dart(CtTunnel *ct)
{
    // Ship local: x right, y up, z forward.
    Vector3 nose = { 0.0f, 0.0f, 0.95f };
    Vector3 tl = { -0.62f, -0.02f, -0.5f }, tr = { 0.62f, -0.02f, -0.5f };
    Vector3 ridge = { 0.0f, 0.17f, -0.38f }, belly = { 0.0f, -0.1f, -0.4f };
    Vector3 back = { 0.0f, 0.04f, -0.62f };
    Color hull = { 0x10, 0x16, 0x24, 255 };
    LineAttr fa = { hull, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    LineGeo g = { 0 };
    Vector3 tris[8][3] = {
        { nose, ridge, tl }, { nose, tr, ridge }, { nose, tl, belly }, { nose, belly, tr },
        { tl, ridge, back }, { ridge, tr, back }, { tl, back, belly }, { back, tr, belly },
    };
    for (int i = 0; i < 8; i++) {
        Vector3 n = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(tris[i][1], tris[i][0]), Vector3Subtract(tris[i][2], tris[i][0])));
        lg_fill_tri(&g, tris[i][0], tris[i][1], tris[i][2], n, fa);
    }
    upload_static(&ct->dartHull, &g, false);

    lg_clear(&g);
    LineAttr ea = { (Color){ 0x4F, 0xEA, 0xF5, 255 }, 2.5f, 0.0f, 0.0f, -1.0f, 0.0f };
    Vector3 edges[][2] = { { nose, tl }, { nose, tr }, { nose, ridge }, { tl, back }, { tr, back }, { ridge, back } };
    for (int i = 0; i < 6; i++) lg_facing_segment(&g, edges[i][0], edges[i][1], 0.012f, 0.012f, ea.intensity, ea.intensity, ea);
    Vector3 eng[2];
    flight_engine_offsets(true, eng);
    // Engine glow intensity is baked at build time (engineIntensity).
    float gi = ct->core.engineIntensity;
    LineAttr ga = { (Color){ 0xE8, 0xFB, 0xFF, 255 }, gi, 0.0f, 0.0f, -1.0f, 0.0f };
    for (int i = 0; i < 2; i++) {
        Vector3 a = eng[i], b = { eng[i].x, eng[i].y, eng[i].z - 0.08f };
        lg_facing_segment(&g, a, b, 0.09f, 0.09f, gi, gi, ga);
    }
    upload_static(&ct->dartEdges, &g, false);
    lg_free(&g);
}

static void build_ship(CtTunnel *ct)
{
    char textureFile[1024];
    snprintf(textureFile,sizeof(textureFile),"%s/ship-metal-v1.png",ct->assetDir);
    if(FileExists(textureFile)) {
        ct->shipSkin=LoadTexture(textureFile);
        if(ct->shipSkin.id){
            GenTextureMipmaps(&ct->shipSkin);
            SetTextureFilter(ct->shipSkin,TEXTURE_FILTER_TRILINEAR);
            SetTextureFilter(ct->shipSkin,TEXTURE_FILTER_ANISOTROPIC_16X);
            SetTextureWrap(ct->shipSkin,TEXTURE_WRAP_REPEAT);
            printf("ship texture: %dx%d, mipmapped aerospace metal\n",ct->shipSkin.width,ct->shipSkin.height);
        }
    }
    shipmodel_build(&ct->shipHullGeo, &ct->shipEdgesGeo, ct->core.engineIntensity);
    upload_static(&ct->shipHull, &ct->shipHullGeo, false);
    upload_static(&ct->shipEdges, &ct->shipEdgesGeo, false);
    shipmodel_nozzles(&ct->shipNozzleGeo,0);
    upload_static(&ct->shipNozzles,&ct->shipNozzleGeo,true);
    shipmodel_flames(&ct->shipFlameGeo);
    upload_static(&ct->shipFlames,&ct->shipFlameGeo,false);
    build_dart(ct);

    // Trail mesh: fixed capacity, rewritten each frame.
    LineGeo *t = &ct->trailGeo;
    lg_clear(t);
    LineAttr ta = { WHITE, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f };
    for (int i = 0; i < 2 * FLIGHT_TRAIL_LEN; i++) lg_facing_segment(t, (Vector3){ 0 }, (Vector3){ 0 }, 0, 0, 0, 0, ta);
    upload_static(&ct->trailMesh, t, true);
}

static Matrix basis_matrix(Vector3 right, Vector3 up, Vector3 fwd, Vector3 pos)
{
    Matrix m = { 0 };
    m.m0 = right.x; m.m1 = right.y; m.m2 = right.z;
    m.m4 = up.x; m.m5 = up.y; m.m6 = up.z;
    m.m8 = fwd.x; m.m9 = fwd.y; m.m10 = fwd.z;
    m.m12 = pos.x; m.m13 = pos.y; m.m14 = pos.z;
    m.m15 = 1.0f;
    return m;
}

static void update_nozzles(CtTunnel *ct)
{
    shipmodel_nozzles(&ct->shipNozzleGeo,flight_speed_norm(&ct->fp,ct->view.speed));
    LineGeo *g=&ct->shipNozzleGeo;int n=g->vcount;
    UpdateMeshBuffer(ct->shipNozzles,0,g->pos,n*3*(int)sizeof(float),0);
    UpdateMeshBuffer(ct->shipNozzles,2,g->nrm,n*3*(int)sizeof(float),0);
}

static void update_trail_mesh(CtTunnel *ct)
{
    const FlightState *st = &ct->cur;
    LineGeo *t = &ct->trailGeo;
    lg_clear(t);
    Color cyan = { 0x9F, 0xF4, 0xFF, 255 };
    LineAttr ta = { cyan, 1.0f, 0.0f, 0.0f, -1.0f, 0.0f };
    Vector3 eng[2];
    const FlightView *v = &ct->view;
    float boost=flight_speed_norm(&ct->fp,v->speed);
    if (ct->fp.dartShip) flight_engine_offsets(true, eng);
    else for(int e=0;e<2;e++)eng[e]=flight_exhaust_offset(e,v->gimbal[e],.48f+2.6f*boost);
    float w0 = ct->core.trailWidth*(1+.8f*boost), i0 = ct->core.trailIntensity*(.5f+.5f*boost);
    for (int e = 0; e < 2; e++) {
        DVec3 head = dv_add(v->shipPos, dv_add(dv_add(dv_scale(v->shipRight, eng[e].x), dv_scale(v->shipUp, eng[e].y)), dv_scale(v->shipFwd, eng[e].z)));
        Vector3 prev = dv_to_v3(dv_sub(head, ct->renderOrigin));
        float tPrev = 0.0f;
        for (int k = 0; k < FLIGHT_TRAIL_LEN; k++) {
            // The head uses the interpolated flame tip. Start history one
            // physics step earlier so the wake never folds forward into it.
            // The dart's engines are fixed, so its trail starts at the newest sample.
            int skip = ct->fp.dartShip ? 0 : 1;
            if (k < st->trailCount - skip) {
                int idx = (st->trailHead - 1 - skip - k + 2 * FLIGHT_TRAIL_LEN) % FLIGHT_TRAIL_LEN;
                Vector3 p = dv_to_v3(dv_sub(st->trail[e][idx], ct->renderOrigin));
                float tk = (float)(k + 1) / (float)FLIGHT_TRAIL_LEN;
                float fa = 1.0f - tPrev, fb = 1.0f - tk;
                lg_facing_segment(t, prev, p, w0 * (0.4f + 0.6f * fa), w0 * (0.4f + 0.6f * fb), i0 * fa * fa, i0 * fb * fb, ta);
                prev = p;
                tPrev = tk;
            } else {
                lg_facing_segment(t, prev, prev, 0, 0, 0, 0, ta);
            }
        }
    }
    int n = t->vcount;
    UpdateMeshBuffer(ct->trailMesh, 0, t->pos, n * 3 * (int)sizeof(float), 0);
    UpdateMeshBuffer(ct->trailMesh, 1, t->uv, n * 2 * (int)sizeof(float), 0);
    UpdateMeshBuffer(ct->trailMesh, 2, t->nrm, n * 3 * (int)sizeof(float), 0);
    UpdateMeshBuffer(ct->trailMesh, 3, t->col, n * 4, 0);
    UpdateMeshBuffer(ct->trailMesh, 4, t->tan, n * 4 * (int)sizeof(float), 0);
    UpdateMeshBuffer(ct->trailMesh, 5, t->uv2, n * 2 * (int)sizeof(float), 0);
}

static void draw_mesh(CtTunnel *ct, int sh, Mesh m, Matrix xf)
{
    ct->mat.shader = sl_shader(&ct->sl, sh);
    DrawMesh(m, ct->mat, xf);
    ct->rstats.drawCalls++;
    ct->rstats.vertices += m.vertexCount;
}

static void draw_ship_opaque(CtTunnel *ct, const FrameContext *ctx)
{
    const FlightView *v = &ct->view;
    Matrix xf = basis_matrix(dv_to_v3(v->shipRight), dv_to_v3(v->shipUp), dv_to_v3(v->shipFwd), dv_to_v3(dv_sub(v->shipPos, ct->renderOrigin)));
    if (ct->fp.dartShip) {
        sl_v3(&ct->sl, ct->shShipDart, "uCamPos", ctx->camera.position);
        sl_f3(&ct->sl, ct->shShipDart, "uRimColor", 0.25f, 0.85f, 1.0f);
        sl_v3(&ct->sl, ct->shShipDart, "uFillDir", Vector3Negate(dv_to_v3(v->shipFwd)));
        draw_mesh(ct, ct->shShipDart, ct->dartHull, xf);
        return;
    }
    sl_v3(&ct->sl, ct->shShip, "uCamPos", ctx->camera.position);
    sl_f3(&ct->sl, ct->shShip, "uRimColor", 0.25f, 0.85f, 1.0f);
    sl_v3(&ct->sl, ct->shShip, "uFillDir", Vector3Negate(dv_to_v3(v->shipFwd)));
    sl_v3(&ct->sl,ct->shShip,"uKeyDir",dv_to_v3(dv_add(dv_add(dv_scale(v->shipUp,.9),dv_scale(v->shipRight,-.55)),dv_scale(v->shipFwd,-.35))));
    sl_i1(&ct->sl,ct->shShip,"uHasSkin",ct->shipSkin.id?1:0);
    sl_f1(&ct->sl, ct->shShip, "uTime", ctx->shaderTime);
    sl_f1(&ct->sl, ct->shShip, "uBoost", ctx->speedNorm);
    sl_f2(&ct->sl,ct->shShip,"uSteer",v->controlX,v->controlY);
    sl_f1(&ct->sl,ct->shShip,"uDamage",ct->shipDamage);
    sl_f2(&ct->sl,ct->shShip,"uGimbalLeft",v->gimbal[0].x,v->gimbal[0].y);
    sl_f2(&ct->sl,ct->shShip,"uGimbalRight",v->gimbal[1].x,v->gimbal[1].y);
    Texture2D previousTexture=ct->mat.maps[MATERIAL_MAP_ALBEDO].texture;
    if(ct->shipSkin.id)ct->mat.maps[MATERIAL_MAP_ALBEDO].texture=ct->shipSkin;
    draw_mesh(ct, ct->shShip, ct->shipHull, xf);
    draw_mesh(ct, ct->shShip, ct->shipNozzles, xf);
    ct->mat.maps[MATERIAL_MAP_ALBEDO].texture=previousTexture;
}

// update_trail_mesh() must have run this frame.
static void draw_ship_emissive(CtTunnel *ct, const FrameContext *ctx)
{
    const FlightView *v = &ct->view;
    ShaderLib *sl = &ct->sl;
    int sh = ct->shLine;
    Matrix xf = basis_matrix(dv_to_v3(v->shipRight), dv_to_v3(v->shipUp), dv_to_v3(v->shipFwd), dv_to_v3(dv_sub(v->shipPos, ct->renderOrigin)));
    env_set_common_uniforms(sl, sh, ctx);
    sl_f1(sl, sh, "uFlicker", 0.0f);
    sl_f1(sl, sh, "uEdgeMix", 0.0f);
    // The ship is always close: disable distance fades.
    sl_f4(sl, sh, "uFade", 1e6f, 2e6f, 0.0f, 0.001f);
    sl_f1(sl, sh, "uChunkFade", 1.0f);
    if (ct->fp.dartShip) draw_mesh(ct, sh, ct->dartEdges, xf);
    else {
        sl_i1(sl,sh,"uShipPose",1);
        sl_f2(sl,sh,"uSteer",v->controlX,v->controlY);
        draw_mesh(ct, sh, ct->shipEdges, xf);
        sl_i1(sl,sh,"uShipPose",0);
        sl_f1(sl,ct->shAfterburner,"uTime",ctx->shaderTime);
        sl_v3(sl,ct->shAfterburner,"uCamPos",ctx->camera.position);
        sl_f1(sl,ct->shAfterburner,"uBoost",ctx->speedNorm);
        sl_f1(sl,ct->shAfterburner,"uPower",fminf(ct->core.engineIntensity*.22f,2.0f));
        sl_f2(sl,ct->shAfterburner,"uGimbalLeft",v->gimbal[0].x,v->gimbal[0].y);
        sl_f2(sl,ct->shAfterburner,"uGimbalRight",v->gimbal[1].x,v->gimbal[1].y);
        draw_mesh(ct,ct->shAfterburner,ct->shipFlames,xf);
    }
    Vector3 mag = ct_srgb_to_linear((Color){ 0x65, 0xCE, 0xF5, 255 });
    sl_v3(sl, sh, "uEdgeColor", mag);
    sl_f1(sl, sh, "uEdgeMix", 1.0f);
    draw_mesh(ct, sh, ct->trailMesh, MatrixIdentity());
    sl_f1(sl, sh, "uEdgeMix", 0.0f);
}

// ---------------------------------------------------------------------------
// Lifecycle

// Path coverage needed around the ship: the chunk window plus the environment's build reach.
static double path_reach(const CtTunnel *ct) { return fmin(fmax(ct->env->pathReach, 0.0), 1500.0); }

static void ensure_path(CtTunnel *ct, double s)
{
    double reach = path_reach(ct);
    path_ensure(&ct->path, s - 100.0 - reach, s + CT_VIEW_DIST + 100.0 + reach);
}

static void prewarm(CtTunnel *ct)
{
    double s = ct->cur.s;
    ensure_path(ct, s);
    chunks_update(&ct->chunks, ct->env, &ct->path, s, CT_VIEW_DIST, ct->simTime, 0, 0);
}

static void reset_world(CtTunnel *ct, double s, bool regenPath)
{
    chunks_release_all(&ct->chunks, ct->env);
    if (regenPath) path_reset(&ct->path, ct->seed, ct->env->pathStyle(ct->env));
    ensure_path(ct, s);
    flight_reset(&ct->cur, &ct->fp, &ct->path, s);
    ct->prev = ct->cur;
    ct->view = flight_interpolate(&ct->prev, &ct->cur, 0.0f);
    ct->renderOrigin = ct->cur.shipPos;
    ct->acc = 0.0;
    prewarm(ct);
    // Creation, restart and jumps must be renderable before time advances.
    // The first interactive frame reports dt == 0 on Windows.
    ct_update(ct,0.0f);
}

CtTunnel *ct_create(const CtConfig *cfg)
{
    CtTunnel *ct = (CtTunnel *)calloc(1, sizeof(CtTunnel));
    ct->cfg = *cfg;
    snprintf(ct->shaderDir, sizeof(ct->shaderDir), "%s", cfg->shaderDir ? cfg->shaderDir : CT_SHADER_DIR);
    snprintf(ct->presetDir, sizeof(ct->presetDir), "%s", cfg->presetDir ? cfg->presetDir : CT_PRESET_DIR);
    if(cfg->assetDir)snprintf(ct->assetDir,sizeof(ct->assetDir),"%s",cfg->assetDir);
    else snprintf(ct->assetDir,sizeof(ct->assetDir),"%sassets",GetApplicationDirectory());
    ct->seed = cfg->seed;
    ct->whiteoutT = -1.0f;
    ct->whiteoutManual = -1.0f;
    ct->showOverlay = true;
    ct->vanishUV = (Vector2){ 0.5f, 0.5f };
    ct->jumpTarget = 5000.0f;
    for (int i = 0; i < 32; i++) ct->groupOpen[i] = false;

    sl_init(&ct->sl, ct->shaderDir);
    params_clear(&ct->reg);
    register_core_params(ct);
    ct->envParamStart = ct->reg.count;

    ct->envs[ENV_VORTEX] = env_vortex_create();
    ct->envs[ENV_TEMPLATE] = env_template_create();
    ct->envs[ENV_CITY] = env_city_create();
    ct->envs[ENV_CORRIDOR] = env_corridor_create();
    ct->envs[ENV_GREYBOX] = env_greybox_create();
    ct->envId = (cfg->environment >= 0 && cfg->environment < ENV_COUNT) ? cfg->environment : ENV_VORTEX;
    ct->env = &ct->envs[ct->envId];
    register_env_params(ct);
    for (int i = 0; i < ENV_COUNT; i++) {
        // Every environment registers once so its state has defaults, then the active one re-registers.
        if (i != ct->envId && ct->envs[i].registerParams) {
            ParamRegistry tmp;
            params_clear(&tmp);
            ct->envs[i].registerParams(&ct->envs[i], &tmp);
        }
        ct->envs[i].init(&ct->envs[i], ct->seed, &ct->sl);
    }

    pipe_init(&ct->pipe, &ct->sl, cfg->width, cfg->height, cfg->hdr16, ct->post.bloomQuarterRes);
    ct->shShip = sl_load(&ct->sl, "ship.vs", "ship.fs");
    ct->shShipDart = sl_load(&ct->sl, "ship.vs", "ship_dart.fs");
    ct->shAfterburner = sl_load(&ct->sl,"afterburner.vs","afterburner.fs");
    ct->shLine = sl_load(&ct->sl, "object.vs", "line_facing.fs");
    ct->mat = LoadMaterialDefault();
    build_ship(ct);

    path_init(&ct->path, ct->seed, ct->env->pathStyle(ct->env));
    chunks_init(&ct->chunks);
    reset_world(ct, 0.0, false);
    return ct;
}

void ct_destroy(CtTunnel *ct)
{
    if (!ct) return;
    chunks_free(&ct->chunks, ct->env);
    for (int i = 0; i < ENV_COUNT; i++) if (ct->envs[i].unload) ct->envs[i].unload(&ct->envs[i]);
    lg_unload_mesh(ct->shipHull);
    if(ct->shipSkin.id)UnloadTexture(ct->shipSkin);
    lg_unload_mesh(ct->shipEdges);
    lg_unload_mesh(ct->shipNozzles);
    lg_unload_mesh(ct->shipFlames);
    lg_unload_mesh(ct->dartHull);
    lg_unload_mesh(ct->dartEdges);
    lg_unload_mesh(ct->trailMesh);
    lg_free(&ct->shipHullGeo);
    lg_free(&ct->shipEdgesGeo);
    lg_free(&ct->shipNozzleGeo);
    lg_free(&ct->shipFlameGeo);
    lg_free(&ct->trailGeo);
    ct->mat.shader.id = rlGetShaderIdDefault();
    UnloadMaterial(ct->mat);
    pipe_unload(&ct->pipe);
    sl_unload_all(&ct->sl);
    path_free(&ct->path);
    free(ct);
}

void ct_set_environment(CtTunnel *ct, int id)
{
    if (id < 0 || id >= ENV_COUNT || id == ct->envId) return;
    chunks_release_all(&ct->chunks, ct->env);
    ct->envId = id;
    ct->env = &ct->envs[id];
    register_env_params(ct);
    ct->env->reseed(ct->env, ct->seed);
    reset_world(ct, ct->cur.s, true);
}

int ct_get_environment(const CtTunnel *ct) { return ct->envId; }

void ct_rebuild(CtTunnel *ct)
{
    ct->env->reseed(ct->env, ct->seed);
    reset_world(ct, ct->cur.s, true);
}

void ct_reseed(CtTunnel *ct, uint64_t seed)
{
    ct->seed = seed;
    ct_rebuild(ct);
}

uint64_t ct_get_seed(const CtTunnel *ct) { return ct->seed; }
void ct_reset_route(CtTunnel *ct,uint64_t seed)
{
    ct->seed=seed;ct->env->reseed(ct->env,seed);reset_world(ct,0,true);
}
const char *ct_shader_directory(const CtTunnel *ct) { return ct->shaderDir; }
const char *ct_asset_directory(const CtTunnel *ct) { return ct->assetDir; }

void ct_jump_to(CtTunnel *ct, double s)
{
    if (s < 0.0) s = 0.0;
    bool regen = s - 100.0 - path_reach(ct) < path_s_min(&ct->path);
    reset_world(ct, s, regen);
}

void ct_set_paused(CtTunnel *ct, bool paused) { ct->paused = paused; }
void ct_set_time_scale(CtTunnel *ct, float scale) { ct->core.timeScale = scale; }
void ct_set_input(CtTunnel *ct, const CtInput *in)
{
    ct->input.steerX = in->steerX;
    ct->input.steerY = in->steerY;
    ct->input.boost = in->boost;
    // Triggers latch until a sim step consumes them (frames can run without a step).
    ct->input.rollLeft = ct->input.rollLeft || in->rollLeft;
    ct->input.rollRight = ct->input.rollRight || in->rollRight;
}
void ct_set_intensity(CtTunnel *ct, float intensity) { ct->core.intensityOverride = intensity < 0.0f ? -1.0f : fminf(intensity, 1.0f); }
void ct_set_ship_damage(CtTunnel *ct,float damage) { ct->shipDamage=fminf(fmaxf(damage,0),1); }
void ct_trigger_whiteout(CtTunnel *ct) { ct->whiteoutT = 0.0f; }
void ct_set_whiteout(CtTunnel *ct, float w) { ct->whiteoutManual = w; }

void ct_set_camera_world(CtTunnel *ct, const double pos[3], const double target[3], Vector3 up, float fovyDeg)
{
    ct->extCamValid = true;
    ct->extCamPos = dv3(pos[0], pos[1], pos[2]);
    ct->extCamTarget = dv3(target[0], target[1], target[2]);
    ct->extCamUp = up;
    ct->extCamFov = fovyDeg;
}

// ---------------------------------------------------------------------------
// Update

static void track_coord(CtTunnel *ct, DVec3 rel)
{
    float m = (float)fmax(fabs(rel.x), fmax(fabs(rel.y), fabs(rel.z)));
    if (m > ct->maxRenderCoord) ct->maxRenderCoord = m;
}

void ct_update(CtTunnel *ct, float dt)
{
    double t0 = GetTime();
    if (dt > 0.1f) dt = 0.1f;
    if (dt < 0.0f) dt = 0.0f;
    float sdt = ct->paused ? 0.0f : dt * ct->core.timeScale;

    ensure_path(ct, ct->cur.s);
    int steps = flight_advance(&ct->prev, &ct->cur, &ct->fp, &ct->path, &ct->input, &ct->acc, sdt, 24);
    ct->simTime += steps * SIM_DT;
    float alpha = (float)(ct->acc / SIM_DT);
    ct->view = flight_interpolate(&ct->prev, &ct->cur, alpha);
    double sShip = ct->view.s;

    // Floating origin.
    if (dv_len(dv_sub(ct->view.shipPos, ct->renderOrigin)) > ct->core.rebaseDist) {
        DVec3 delta = dv_sub(ct->view.shipPos, ct->renderOrigin);
        ct->renderOrigin = ct->view.shipPos;
        Vector3 d = dv_to_v3(delta);
        ct->freeCamera.position = Vector3Subtract(ct->freeCamera.position, d);
        ct->freeCamera.target = Vector3Subtract(ct->freeCamera.target, d);
    }

    // Intensity: pacing or override.
    float target = ct->core.intensityOverride >= 0.0f ? ct->core.intensityOverride
                 : (ct->env->energyAt ? ct->env->energyAt(ct->env, &ct->path, sShip + 40.0) : 0.5f);
    ct->intensity += (target - ct->intensity) * (1.0f - expf(-ct->core.intensityResponse * dt));

    // Whiteout animation (real time so it works while paused too).
    if (ct->whiteoutT >= 0.0f) {
        ct->whiteoutT += dt / ct->core.whiteoutDuration;
        float t = ct->whiteoutT;
        if (t >= 1.0f) { ct->whiteoutT = -1.0f; ct->whiteout = 0.0f; ct->flare = 0.0f; }
        else {
            float up = t < 0.45f ? t / 0.45f : 1.0f - (t - 0.45f) / 0.55f;
            ct->flare = powf(fmaxf(up, 0.0f), 0.7f);
            ct->whiteout = powf(fmaxf(up, 0.0f), 2.5f);
        }
    }
    float whiteout = ct->whiteoutManual >= 0.0f ? ct->whiteoutManual : ct->whiteout;

    chunks_update(&ct->chunks, ct->env, &ct->path, sShip, CT_VIEW_DIST, ct->simTime, ct->core.buildBudget, ct->core.uploadBudget);

    // Camera in render space.
    Camera3D cam;
    float fov;
    if (ct->freeCam) {
        cam = ct->freeCamera;
        fov = cam.fovy;
    } else if (!ct->cfg.ownCamera && ct->extCamValid) {
        cam.position = dv_to_v3(dv_sub(ct->extCamPos, ct->renderOrigin));
        cam.target = dv_to_v3(dv_sub(ct->extCamTarget, ct->renderOrigin));
        cam.up = ct->extCamUp;
        fov = ct->extCamFov;
    } else {
        cam.position = dv_to_v3(dv_sub(ct->view.camPos, ct->renderOrigin));
        cam.target = dv_to_v3(dv_sub(ct->view.camTarget, ct->renderOrigin));
        cam.up = dv_to_v3(ct->view.camUp);
        fov = ct->view.fov;
    }
    cam.fovy = fov;
    cam.projection = CAMERA_PERSPECTIVE;
    track_coord(ct, v3_to_dv(cam.position));

    int w = ct->pipe.w, h = ct->pipe.h;
    FrameContext *c = &ct->ctx;
    c->time = ct->simTime;
    c->shaderTime = (float)fmod(ct->simTime, 3600.0);
    c->dt = sdt;
    c->sShip = sShip;
    c->speed = ct->view.speed;
    c->speedNorm = flight_speed_norm(&ct->fp, ct->view.speed);
    c->intensity = ct->intensity;
    c->whiteout = whiteout;
    c->flare = ct->flare;
    c->camera = cam;
    c->renderOrigin = ct->renderOrigin;
    c->view = MatrixLookAt(cam.position, cam.target, cam.up);
    c->proj = MatrixPerspective(fov * DEG2RAD, (double)w / (double)h, CT_Z_NEAR, CT_Z_FAR);
    c->zNear = (float)CT_Z_NEAR;
    c->zFar = (float)CT_Z_FAR;
    c->fovy = fov;
    c->width = w;
    c->height = h;
    c->pixelScale = 2.0f * tanf(fov * 0.5f * DEG2RAD) / (float)h;
    c->atmo = ct->atmo;
    c->atmo.coreColor = (Vector3){ powf(ct->core.coreR, 2.2f), powf(ct->core.coreG, 2.2f), powf(ct->core.coreB, 2.2f) };
    if (ct->env->atmosphere) ct->env->atmosphere(ct->env, &c->atmo);
    c->path = &ct->path;
    c->shaders = &ct->sl;
    c->stats = &ct->rstats;

    // Vanishing point: path 60 units ahead, smoothed.
    PathSample vp = path_sample(&ct->path, sShip + 60.0);
    float vw;
    Vector2 vuv = ct_project_uv(MatrixMultiply(c->view, c->proj), dv_to_v3(dv_sub(vp.pos, ct->renderOrigin)), &vw);
    if (vw > 0.0f) {
        float k = 1.0f - expf(-6.0f * dt);
        ct->vanishUV.x += (vuv.x - ct->vanishUV.x) * k;
        ct->vanishUV.y += (vuv.y - ct->vanishUV.y) * k;
    }
    c->vanishingUV = ct->vanishUV;

    // Track the largest chunk translation passed to GL.
    int nvis;
    const ChunkGpu *const *vis = chunks_visible(&ct->chunks, &nvis);
    for (int i = 0; i < nvis; i++) track_coord(ct, dv_sub(vis[i]->origin, ct->renderOrigin));

    if (ct->env->update) ct->env->update(ct->env, c);
    sl_poll(&ct->sl, GetTime());
    ct->msUpdate = (GetTime() - t0) * 1000.0;
    ct->frameMs = dt * 1000.0f;
}

// ---------------------------------------------------------------------------
// Render

static void draw_path_debug(CtTunnel *ct)
{
    double s0 = ct->ctx.sShip - 20.0, s1 = ct->ctx.sShip + 300.0;
    for (double s = floor(s0 / 5.0) * 5.0; s < s1; s += 5.0) {
        PathSample a = path_sample(&ct->path, s), b = path_sample(&ct->path, s + 5.0);
        Vector3 pa = dv_to_v3(dv_sub(a.pos, ct->renderOrigin)), pb = dv_to_v3(dv_sub(b.pos, ct->renderOrigin));
        DrawLine3D(pa, pb, WHITE);
        DrawLine3D(pa, Vector3Add(pa, dv_to_v3(a.T)), RED);
        DrawLine3D(pa, Vector3Add(pa, dv_to_v3(a.N)), GREEN);
        DrawLine3D(pa, Vector3Add(pa, dv_to_v3(a.B)), BLUE);
        if (fmod(s, 10.0) < 1e-6) {
            const int n = 32;
            for (int i = 0; i < n; i++) {
                double t0 = 6.2831853 * i / n, t1 = 6.2831853 * (i + 1) / n;
                DVec3 q0 = dv_add(a.pos, dv_add(dv_scale(a.B, cos(t0) * a.radius), dv_scale(a.N, sin(t0) * a.radius)));
                DVec3 q1 = dv_add(a.pos, dv_add(dv_scale(a.B, cos(t1) * a.radius), dv_scale(a.N, sin(t1) * a.radius)));
                DrawLine3D(dv_to_v3(dv_sub(q0, ct->renderOrigin)), dv_to_v3(dv_sub(q1, ct->renderOrigin)), (Color){ 255, 200, 0, 255 });
            }
        }
    }
    rlDrawRenderBatchActive();
}

// Planar floor reflection: the scene mirrored about the floor plane (view' = R * view, so a
// floor point projects to the same pixel in both passes), opaque + emissive content and the
// ship, then a vertical blur. The floor shader samples the result at its screen uv.
void ct_set_scene_draw(CtTunnel *ct, CtSceneDrawFn draw, void *user)
{
    ct->sceneDraw = draw;
    ct->sceneUser = user;
}

static void draw_game_scene(CtTunnel *ct, const FrameContext *ctx, bool emissive)
{
    if (!ct->sceneDraw) return;
    CtSceneView view = { ctx->camera, ctx->view, ctx->proj,
        { ctx->renderOrigin.x, ctx->renderOrigin.y, ctx->renderOrigin.z },
        ctx->shaderTime, ctx->speed, ctx->intensity, ctx->reflectionPass };
    ct->sceneDraw(ct->sceneUser, &view, emissive);
}

static void render_reflection(CtTunnel *ct, const ChunkGpu *const *vis, int nvis, bool ship)
{
    FrameContext *c = &ct->ctx;
    c->hasReflection = false;
    c->reflectionPass = false;
    int res = ct->post.reflectRes;
    if (res <= 0 || !ct->env->reflectiveFloor || ct->path.style.floorDepth <= 0.0f) return;
    double floorY = path_sample(&ct->path, c->sShip).pos.y - ct->path.style.floorDepth;
    float h = (float)(floorY - ct->renderOrigin.y);
    Matrix R = MatrixMultiply(MatrixMultiply(MatrixTranslate(0.0f, -h, 0.0f), MatrixScale(1.0f, -1.0f, 1.0f)), MatrixTranslate(0.0f, h, 0.0f));
    FrameContext rc = *c;
    rc.reflectionPass = true;
    rc.view = MatrixMultiply(R, c->view);
    rc.camera.position.y = 2.0f * h - c->camera.position.y;
    rc.camera.target.y = 2.0f * h - c->camera.target.y;
    rc.camera.up.y = -c->camera.up.y;
    pipe_begin_reflection(&ct->pipe, res == 1 ? 4 : 2, rc.view, rc.proj);
    if (ct->env->drawOpaque) ct->env->drawOpaque(ct->env, &rc, vis, nvis);
    if (ship) draw_ship_opaque(ct, &rc);
    draw_game_scene(ct, &rc, false);
    pipe_set_emissive(true);
    if (ct->env->drawEmissive) ct->env->drawEmissive(ct->env, &rc, vis, nvis);
    if (ship) draw_ship_emissive(ct, &rc);
    draw_game_scene(ct, &rc, true);
    pipe_set_emissive(false);
    pipe_end_reflection(&ct->pipe);
    c->reflection = pipe_blur_reflection(&ct->pipe, ct->post.reflectBlur);
    c->hasReflection = true;
    ct->rstats.drawCalls += 2;
}

void ct_render(CtTunnel *ct, const RenderTexture2D *target, int width, int height)
{
    if (width != ct->pipe.w || height != ct->pipe.h) pipe_resize(&ct->pipe, width, height, ct->post.bloomQuarterRes);
    ct->rstats.drawCalls = 0;
    ct->rstats.vertices = 0;
    FrameContext *c = &ct->ctx;
    if (c->width != ct->pipe.w || c->height != ct->pipe.h) {
        c->width = ct->pipe.w; c->height = ct->pipe.h;
        c->proj = MatrixPerspective(c->fovy * DEG2RAD, (double)c->width / (double)c->height, CT_Z_NEAR, CT_Z_FAR);
        c->pixelScale = 2.0f * tanf(c->fovy * 0.5f * DEG2RAD) / (float)c->height;
    }
    int nvis;
    const ChunkGpu *const *vis = chunks_visible(&ct->chunks, &nvis);

    bool ship = ct->cfg.drawShip && ct->core.showShip;
    if (ship) {update_trail_mesh(ct);if(!ct->fp.dartShip)update_nozzles(ct);}
    render_reflection(ct, vis, nvis, ship);

    pipe_begin_scene(&ct->pipe, c->view, c->proj, (Color){ 3, 6, 12, 255 });
    if (ct->env->drawBackground) ct->env->drawBackground(ct->env, c);
    if (ct->env->drawOpaque) ct->env->drawOpaque(ct->env, c, vis, nvis);
    if (ship) draw_ship_opaque(ct, c);
    draw_game_scene(ct, c, false);
    pipe_set_emissive(true);
    if (ct->env->drawEmissive) ct->env->drawEmissive(ct->env, c, vis, nvis);
    if (ship) draw_ship_emissive(ct, c);
    draw_game_scene(ct, c, true);
    if (ct->core.showPathDebug) draw_path_debug(ct);
    pipe_set_emissive(false);
    pipe_end_scene(&ct->pipe);

    if (ct->cfg.usePost) {
        // Ramps in with boost (speedNorm: 0 at base speed, 1 at boost speed).
        float boost = ct->post.zoomBlurAtBase + (1.0f - ct->post.zoomBlurAtBase) * c->speedNorm;
        float zoom = ct->post.zoomBlur * boost * (0.6f + 0.8f * c->intensity);
        pipe_post(&ct->pipe, &ct->post, (RenderTexture2D *)target, width, height, c->vanishingUV, zoom, c->whiteout, c->shaderTime);
        ct->rstats.drawCalls += ct->pipe.drawCalls;
    }
}

Texture2D ct_scene_texture(const CtTunnel *ct) { return ct->pipe.scene.color; }

// ---------------------------------------------------------------------------
// Queries

CtPathSample ct_path_sample(const CtTunnel *ct, double s)
{
    PathSample p = path_sample(&ct->path, s);
    CtPathSample o;
    o.pos[0] = p.pos.x; o.pos[1] = p.pos.y; o.pos[2] = p.pos.z;
    o.tangent = dv_to_v3(p.T);
    o.normal = dv_to_v3(p.N);
    o.binormal = dv_to_v3(p.B);
    o.radius = (float)p.radius;
    o.roll = (float)p.roll;
    o.curvature = (float)p.curvature;
    return o;
}

float ct_tunnel_radius_at(const CtTunnel *ct, double s) { return (float)path_sample(&ct->path, s).radius; }

int ct_debris_in_range(const CtTunnel *ct, double s0, double s1, CtDebris *out, int max)
{
    int n = 0;
    int nvis;
    const ChunkGpu *const *vis = chunks_visible(&ct->chunks, &nvis);
    for (int i = nvis - 1; i >= 0; i--) {
        const ChunkGpu *c = vis[i];
        if (c->s1 < s0 || c->s0 > s1) continue;
        for (int k = 0; k < c->debrisCount && n < max; k++) {
            const DebrisInfo *d = &c->debris[k];
            if (d->s < s0 || d->s > s1) continue;
            out[n].center[0] = d->center.x; out[n].center[1] = d->center.y; out[n].center[2] = d->center.z;
            out[n].radius = d->radius;
            out[n].s = d->s;
            n++;
        }
    }
    return n;
}

Vector2 ct_vanishing_point(const CtTunnel *ct) { return ct->vanishUV; }
double ct_ship_s(const CtTunnel *ct) { return ct->view.s; }
void ct_ship_world(const CtTunnel *ct, double out[3]) { out[0] = ct->view.shipPos.x; out[1] = ct->view.shipPos.y; out[2] = ct->view.shipPos.z; }
void ct_render_origin(const CtTunnel *ct, double out[3]) { out[0] = ct->renderOrigin.x; out[1] = ct->renderOrigin.y; out[2] = ct->renderOrigin.z; }

void ct_get_stats(const CtTunnel *ct, CtStats *o)
{
    memset(o, 0, sizeof(*o));
    o->frameMs = ct->frameMs;
    o->sShip = ct->view.s;
    o->speed = ct->view.speed;
    o->intensity = ct->intensity;
    o->activeChunks = ct->chunks.stats.active;
    o->buildsThisFrame = ct->chunks.stats.builtThisFrame;
    o->uploadsThisFrame = ct->chunks.stats.uploadedThisFrame;
    o->vertices = ct->rstats.vertices;
    o->drawCalls = ct->rstats.drawCalls;
    o->gpuMeshes = ct->chunks.stats.gpuMeshes;
    o->maxChunkBuildMs = ct->chunks.stats.maxBuildMs;
    o->maxStreamFrameMs = ct->chunks.stats.maxFrameMs;
    o->lastStreamFrameMs = ct->chunks.stats.frameMs;
    o->lastChunkBuildMs = ct->chunks.stats.lastBuildMs;
    ct_render_origin(ct, o->renderOrigin);
    o->maxRenderCoord = ct->maxRenderCoord;
    o->msUpdate = ct->msUpdate;
    o->msScene = ct->pipe.ms.scene;
    o->msBloom = ct->pipe.ms.bloom;
    o->msComposite = ct->pipe.ms.composite;
    int k = chunk_index_at(ct->view.s);
    int arch = -1;
    int nvis;
    const ChunkGpu *const *vis = chunks_visible(&ct->chunks, &nvis);
    for (int i = 0; i < nvis; i++) if (vis[i]->index == k) arch = vis[i]->archetype;
    o->chunkLabel = ct->env->chunkLabel ? ct->env->chunkLabel(ct->env, arch) : "";
}

// ---------------------------------------------------------------------------
// Parameters / presets

bool ct_param_set(CtTunnel *ct, const char *name, float value)
{
    Param *p = params_find(&ct->reg, name);
    if (!p) return false;
    params_set(p, value);
    return true;
}

float ct_param_get(CtTunnel *ct, const char *name)
{
    Param *p = params_find(&ct->reg, name);
    return p ? params_get(p) : 0.0f;
}

static void preset_path(const CtTunnel *ct, const char *nameOrPath, char *out, size_t n)
{
    if (strchr(nameOrPath, '/') || strstr(nameOrPath, ".ini")) snprintf(out, n, "%s", nameOrPath);
    else snprintf(out, n, "%s/%s.ini", ct->presetDir, nameOrPath);
}

bool ct_preset_save(CtTunnel *ct, const char *nameOrPath)
{
    char path[1024];
    preset_path(ct, nameOrPath, path, sizeof(path));
    bool ok = params_save_ini(&ct->reg, path);
    printf("preset: %s %s\n", ok ? "saved" : "FAILED to save", path);
    return ok;
}

bool ct_preset_load(CtTunnel *ct, const char *nameOrPath)
{
    char path[1024];
    preset_path(ct, nameOrPath, path, sizeof(path));
    params_reset_defaults(&ct->reg);
    int n = params_load_ini(&ct->reg, path);
    printf("preset: %s %s (%d keys)\n", n >= 0 ? "loaded" : "FAILED to load", path, n);
    if (n < 0) return false;
    ct_rebuild(ct);
    return true;
}

void ct_params_write_markdown(CtTunnel *ct, FILE *out)
{
    static const char *TYPES[3] = { "float", "int", "bool" };
    fprintf(out, "| Group | Name | Type | Default | Min | Max |\n|---|---|---|---|---|---|\n");
    for (int i = 0; i < ct->reg.count; i++) {
        const Param *p = &ct->reg.items[i];
        if (p->type == PARAM_BOOL)
            fprintf(out, "| %s | `%s` | bool | %s | | |\n", p->group, p->name, p->def > 0.5f ? "on" : "off");
        else
            fprintf(out, "| %s | `%s` | %s | %g | %g | %g |\n", p->group, p->name, TYPES[p->type], p->def, p->min, p->max);
    }
}

// ---------------------------------------------------------------------------
// Debug input

static void toggle_param(CtTunnel *ct, const char *name)
{
    Param *p = params_find(&ct->reg, name);
    if (p) params_set(p, params_get(p) > 0.5f ? 0.0f : 1.0f);
}

void ct_debug_handle_input(CtTunnel *ct)
{
    if (IsKeyPressed(KEY_F1)) ct->showPanel = !ct->showPanel;
    if (IsKeyPressed(KEY_F3)) ct->showOverlay = !ct->showOverlay;
    if (IsKeyPressed(KEY_F2)) {
        ct->freeCam = !ct->freeCam;
        if (ct->freeCam) {
            ct->freeCamera = ct->ctx.camera;
            DisableCursor();
        } else {
            EnableCursor();
        }
    }
    if (ct->freeCam) {
        UpdateCamera(&ct->freeCamera, CAMERA_FREE);
        return;   // WASD etc. belong to the free camera
    }
    if (IsKeyPressed(KEY_F12)) ct->wantScreenshot = true;
    if (IsKeyPressed(KEY_R)) ct_reseed(ct, ct->seed + 1);
    if (IsKeyPressed(KEY_P)) ct->paused = !ct->paused;
    if (IsKeyPressed(KEY_W)) ct_trigger_whiteout(ct);
    static const char *LAYERS[9] = { "showRings", "showRibbons", "showStreaks", "showDebris", "showHolo",
                                     "showCore", "showBackground", "showShip", "showPathDebug" };
    for (int i = 0; i < 9; i++) if (IsKeyPressed(KEY_ONE + i)) toggle_param(ct, LAYERS[i]);
    if (IsKeyPressed(KEY_T)) ct_set_environment(ct, (ct->envId + 1) % ENV_COUNT);
    // Speed tuning: [ ] base speed, - = boost speed (steps of 5).
    int db = (IsKeyPressed(KEY_RIGHT_BRACKET) - IsKeyPressed(KEY_LEFT_BRACKET)) * 5;
    int dB = (IsKeyPressed(KEY_EQUAL) - IsKeyPressed(KEY_MINUS)) * 5;
    if (db) ct_param_set(ct, "baseSpeed", ct->fp.baseSpeed + (float)db);
    if (dB) ct_param_set(ct, "boostSpeed", ct->fp.boostSpeed + (float)dB);
}

bool ct_debug_wants_screenshot(CtTunnel *ct)
{
    bool w = ct->wantScreenshot;
    ct->wantScreenshot = false;
    return w;
}

void ct_screenshot_name(const CtTunnel *ct, char *buf, int size)
{
    snprintf(buf, (size_t)size, "vortex_seed%llu_s%.0f.png", (unsigned long long)ct->seed, ct->view.s);
}

void ct_reset_stats(CtTunnel *ct)
{
    ct->chunks.stats.maxBuildMs = ct->chunks.stats.maxUploadMs = ct->chunks.stats.maxFrameMs = 0.0;
    ct->maxRenderCoord = 0.0f;
}

void ct_debug_show(CtTunnel *ct, bool panel, bool overlay)
{
    ct->showPanel = panel;
    ct->showOverlay = overlay;
    if (panel) for (int i = 0; i < 32; i++) ct->groupOpen[i] = (i == 2);
}

void ct_debug_draw(CtTunnel *ct, int width, int height)
{
    if (ct->rebuildPending && !IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        if (ct->rebuildPathPending) ct_rebuild(ct);
        else { ct->env->reseed(ct->env, ct->seed); reset_world(ct, ct->cur.s, false); }
        ct->rebuildPending = ct->rebuildPathPending = false;
    }
    if (ct->showOverlay) ct_debug_overlay(ct, width, height);
    if (ct->showPanel) ct_debug_panel(ct, width, height);
}
