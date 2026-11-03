// Neon vector presentation for combat. Everything is drawn with one tiny shader:
// vertex colour alpha encodes HDR gain (alpha 255 = 8x), so bright edges feed the
// engine's bloom while dark bodies (gain ~1) give the wireframes solid silhouettes.
#include "combat_draw.h"
#include "rlgl.h"
#include "raymath.h"
#include <math.h>

#define GAIN_MAX 8.0f

static const char *VS =
    "#version 330\n"
    "in vec3 vertexPosition; in vec4 vertexColor; uniform mat4 mvp; out vec4 c;\n"
    "void main(){ c = vertexColor; gl_Position = mvp * vec4(vertexPosition, 1.0); }\n";
static const char *FS =
    "#version 330\n"
    "in vec4 c; out vec4 o;\n"
    "void main(){ o = vec4(c.rgb * (c.a * 8.0), 1.0); }\n";

void combat_draw_init(CombatDraw *d)
{
    *d = (CombatDraw){ 0 };
    d->shader = LoadShaderFromMemory(VS, FS);
    d->ready = IsShaderValid(d->shader);
}

void combat_draw_unload(CombatDraw *d)
{
    if (d->ready) UnloadShader(d->shader);
    d->ready = false;
}

// ---------------------------------------------------------------------------
// Arena <-> world

typedef struct Frame { Vector3 c, B, N, T; float k; } Frame;   // render space

static Vector3 v3(double x, double y, double z) { return (Vector3){ (float)x, (float)y, (float)z }; }

void arena_to_world(const CtTunnel *ct, const ArenaMap *m, Vector3 a, double out[3])
{
    CtPathSample p = ct_path_sample(ct, m->shipS - (double)a.z * m->scale);
    double lx = (double)a.x * m->scale, ly = ((double)a.y - m->yZero) * m->scale;
    for (int i = 0; i < 3; i++) {
        float b = i == 0 ? p.binormal.x : i == 1 ? p.binormal.y : p.binormal.z;
        float n = i == 0 ? p.normal.x : i == 1 ? p.normal.y : p.normal.z;
        out[i] = p.pos[i] + b * lx + n * ly;
    }
}

// Local frame at an arena point, in render space (world - origin).
static Frame frame_at(const CtTunnel *ct, const ArenaMap *m, const double origin[3], Vector3 a)
{
    CtPathSample p = ct_path_sample(ct, m->shipS - (double)a.z * m->scale);
    double w[3];
    arena_to_world(ct, m, a, w);
    return (Frame){ v3(w[0] - origin[0], w[1] - origin[1], w[2] - origin[2]),
                    p.binormal, p.normal, p.tangent, m->scale };
}

// Arena-local offset (x lateral, y up, z toward the player) -> render space.
static Vector3 fp(const Frame *f, float x, float y, float z)
{
    return Vector3Add(f->c, Vector3Scale(Vector3Add(Vector3Add(Vector3Scale(f->B, x), Vector3Scale(f->N, y)),
                                                    Vector3Scale(f->T, -z)), f->k));
}

// ---------------------------------------------------------------------------
// Primitives

static Vector3 gCam;   // render-space camera position for ribbon facing

static void col(Color c, float gain)
{
    float a = gain / GAIN_MAX;
    if (a < 0.0f) a = 0.0f;
    if (a > 1.0f) a = 1.0f;
    rlColor4ub(c.r, c.g, c.b, (unsigned char)(a * 255.0f));
}

// Camera-facing ribbon of world width w.
static void ribbon(Vector3 a, Vector3 b, float w, Color c, float gain)
{
    Vector3 dir = Vector3Subtract(b, a);
    Vector3 mid = Vector3Lerp(a, b, 0.5f);
    Vector3 side = Vector3CrossProduct(dir, Vector3Subtract(gCam, mid));
    float len = Vector3Length(side);
    if (len < 1e-6f) return;
    side = Vector3Scale(side, 0.5f * w / len);
    col(c, gain);
    rlVertex3f(a.x - side.x, a.y - side.y, a.z - side.z);
    rlVertex3f(a.x + side.x, a.y + side.y, a.z + side.z);
    rlVertex3f(b.x + side.x, b.y + side.y, b.z + side.z);
    rlVertex3f(b.x - side.x, b.y - side.y, b.z - side.z);
}

static void tri(Vector3 a, Vector3 b, Vector3 c, Color color, float gain)
{
    col(color, gain);
    rlVertex3f(a.x, a.y, a.z); rlVertex3f(b.x, b.y, b.z); rlVertex3f(c.x, c.y, c.z);
    rlVertex3f(c.x, c.y, c.z);   // quads mode: degenerate fourth vertex
}

static Color scale_color(Color c, float s)
{
    return (Color){ (unsigned char)fminf(255.0f, c.r * s), (unsigned char)fminf(255.0f, c.g * s),
                    (unsigned char)fminf(255.0f, c.b * s), 255 };
}

static Color mix_color(Color a, Color b, float t)
{
    return (Color){ (unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t),
                    (unsigned char)(a.b + (b.b - a.b) * t), 255 };
}

// Shapes are vertex/edge/face tables in unit arena-local space.
typedef struct Shape {
    const Vector3 *v; int nv;
    const int (*e)[2]; int ne;
    const int (*f)[3]; int nf;
} Shape;

static const Vector3 OCT_V[6] = { {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1} };
static const int OCT_E[12][2] = { {0,2},{0,3},{0,4},{0,5},{1,2},{1,3},{1,4},{1,5},{2,4},{2,5},{3,4},{3,5} };
static const int OCT_F[8][3] = { {0,2,4},{2,1,4},{1,3,4},{3,0,4},{2,0,5},{1,2,5},{3,1,5},{0,3,5} };
static const Vector3 CUBE_V[8] = { {-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1} };
static const int CUBE_E[12][2] = { {0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7} };
static const int CUBE_F[12][3] = { {0,1,2},{0,2,3},{4,6,5},{4,7,6},{0,4,5},{0,5,1},{1,5,6},{1,6,2},{2,6,7},{2,7,3},{3,7,4},{3,4,0} };
// Dart: nose toward the player (+z), three-point tail.
static const Vector3 DART_V[4] = { {0,0,1.3f}, {0,0.62f,-0.7f}, {-0.6f,-0.36f,-0.7f}, {0.6f,-0.36f,-0.7f} };
static const int DART_E[6][2] = { {0,1},{0,2},{0,3},{1,2},{2,3},{3,1} };
static const int DART_F[4][3] = { {0,1,2},{0,2,3},{0,3,1},{1,3,2} };

static const Shape SHAPE_OCT = { OCT_V, 6, OCT_E, 12, OCT_F, 8 };
static const Shape SHAPE_CUBE = { CUBE_V, 8, CUBE_E, 12, CUBE_F, 12 };
static const Shape SHAPE_DART = { DART_V, 4, DART_E, 6, DART_F, 4 };

typedef struct Xform { float scale, yaw, pitch; } Xform;

static Vector3 xf(const Frame *f, Vector3 v, Xform t)
{
    float cy = cosf(t.yaw), sy = sinf(t.yaw), cp = cosf(t.pitch), sp = sinf(t.pitch);
    float x = v.x * cy + v.z * sy, z = -v.x * sy + v.z * cy;
    float y = v.y * cp - z * sp;
    z = v.y * sp + z * cp;
    return fp(f, x * t.scale, y * t.scale, z * t.scale);
}

static void shape_body(const Frame *f, const Shape *s, Xform t, Color c)
{
    Color body = scale_color(c, 0.07f);
    for (int i = 0; i < s->nf; i++)
        tri(xf(f, s->v[s->f[i][0]], t), xf(f, s->v[s->f[i][1]], t), xf(f, s->v[s->f[i][2]], t), body, 1.0f);
}

static void shape_edges(const Frame *f, const Shape *s, Xform t, Color c, float gain, float width)
{
    for (int i = 0; i < s->ne; i++)
        ribbon(xf(f, s->v[s->e[i][0]], t), xf(f, s->v[s->e[i][1]], t), width, c, gain);
}

// Ring in the arena x/y plane (facing the player), radius in arena units.
static void ring(const Frame *f, float radius, int segments, float phase, float width, Color c, float gain, float gapFrac)
{
    for (int i = 0; i < segments; i++) {
        float a0 = phase + (float)i / segments * 2.0f * PI;
        float a1 = phase + ((float)i + 1.0f - gapFrac) / segments * 2.0f * PI;
        ribbon(fp(f, cosf(a0) * radius, sinf(a0) * radius, 0), fp(f, cosf(a1) * radius, sinf(a1) * radius, 0), width, c, gain);
    }
}

// Distant objects "compile in" over the spawn band instead of popping.
static float compile_fade(float z) { float t = (z + 132.0f) / 34.0f; return t < 0 ? 0 : (t > 1 ? 1 : t * t * (3 - 2 * t)); }

static Color enemy_color(const GameplaySystem *g, const Enemy *e)
{
    switch (e->type) {
        case ENEMY_DRIFTER: return g->primaryColor;
        case ENEMY_CHASER: return g->threatColor;
        case ENEMY_SPLITTER: return g->secondaryColor;
        case ENEMY_BOSS_NODE: return g->secondaryColor;
        default: return mix_color(g->threatColor, g->hotColor, 0.45f);
    }
}

// ---------------------------------------------------------------------------
// Scene

// Rotating lock brackets around a target.
static void lock_brackets(const Frame *f, float s, float time, float w, Color c)
{
    float a = time * 2.2f;
    for (int k = 0; k < 4; k++) {
        float ang = a + k * PI * 0.5f;
        Vector2 d0 = { cosf(ang), sinf(ang) }, d1 = { cosf(ang + 0.5f), sinf(ang + 0.5f) }, d2 = { cosf(ang - 0.5f), sinf(ang - 0.5f) };
        Vector3 corner = fp(f, d0.x * s, d0.y * s, 0);
        ribbon(corner, fp(f, (d0.x * 0.7f + d1.x * 0.3f) * s, (d0.y * 0.7f + d1.y * 0.3f) * s, 0), w, c, 6.0f);
        ribbon(corner, fp(f, (d0.x * 0.7f + d2.x * 0.3f) * s, (d0.y * 0.7f + d2.y * 0.3f) * s, 0), w, c, 6.0f);
    }
}

static void draw_enemy(const CtTunnel *ct, const ArenaMap *m, const double *o, const GameplaySystem *g,
                       const Enemy *e, float time, bool emissive)
{
    Frame f = frame_at(ct, m, o, e->position);
    Color c = enemy_color(g, e);
    float fade = compile_fade(e->position.z);
    float id = (float)e->identity;
    float telegraph = e->phase == ENEMY_TELEGRAPH ? 0.5f + 0.5f * sinf(e->phaseTime * 28.0f) : 0.0f;
    float gain = (2.6f + telegraph * 3.5f) * fade;
    float r = e->size.x * 0.5f;
    float w = 0.05f;
    const Shape *shape = e->type == ENEMY_CHASER ? &SHAPE_DART : e->type == ENEMY_SPLITTER ? &SHAPE_CUBE : &SHAPE_OCT;
    Xform t = { r, e->age * (e->type == ENEMY_SPLITTER ? 0.9f : 1.6f) + id, e->type == ENEMY_CHASER ? 0.0f : e->age * 0.7f + id * 0.37f };
    if (e->type == ENEMY_CHASER) t = (Xform){ r * 1.15f, 0.0f, 0.0f };
    if (!emissive) { if (fade > 0.05f) shape_body(&f, shape, t, c); return; }
    shape_edges(&f, shape, t, c, gain, w);
    if (e->type == ENEMY_SPLITTER) {
        Xform inner = { r * 0.45f, -e->age * 1.7f, e->age * 1.1f };
        shape_edges(&f, &SHAPE_CUBE, inner, scale_color(c, 1.3f), gain * 1.3f, w * 0.8f);
    } else if (e->type == ENEMY_DRIFTER) {
        ring(&f, r * 1.45f, 12, e->age * 1.3f + id, w * 0.8f, c, gain * 0.7f, 0.35f);
    }
    if (telegraph > 0.0f) ring(&f, r * (1.6f + e->phaseTime * 1.5f), 16, 0.0f, w, g->hotColor, telegraph * 4.0f * fade, 0.2f);
    if (e->locked) lock_brackets(&f, r * 1.9f, time, w * 1.4f, g->hotColor);
}

// ---------------------------------------------------------------------------
// Boss: the Recursive Core. A turbine of eight kite petals around a reactor of
// nested octahedra (each level a smaller, counter-spinning copy), a segmented
// halo, and a hex shield while firewall nodes live. Petals spread open when the
// core is exposed; enraged, the whole assembly heats toward the hot colour.

static float smooth01(float t) { t = t < 0 ? 0 : (t > 1 ? 1 : t); return t * t * (3 - 2 * t); }

static void draw_boss_node(const Frame *f, const GameplaySystem *g, const Enemy *e, float time, float fade, bool emissive)
{
    float r = e->size.x * 0.7f, id = (float)e->identity;   // drawn ~ the hit radius (0.55 + 0.28 size)
    Color c = enemy_color(g, e);
    Xform t = { r, time * 1.4f + id, time * 0.9f + id * 0.5f };
    if (!emissive) { shape_body(f, &SHAPE_OCT, t, c); return; }
    shape_edges(f, &SHAPE_OCT, t, c, 3.2f * fade, 0.05f);
    shape_edges(f, &SHAPE_CUBE, (Xform){ r * 0.38f, -time * 2.1f, time * 1.3f }, g->hotColor, 4.0f * fade, 0.04f);
    ring(f, r * 1.55f, 4, -time * 1.8f + id, 0.05f, c, 2.4f * fade, 0.45f);   // bracket square
    if (e->locked) lock_brackets(f, r * 2.0f, time, 0.07f, g->hotColor);
}

static void draw_boss(const CtTunnel *ct, const ArenaMap *m, const double *o, const GameplaySystem *g,
                      float time, bool emissive)
{
    const BossState *b = &g->boss;
    // Defeat: the core's last shockwave keeps expanding where it died.
    if (!b->active) {
        if (!emissive || b->defeatFlash <= 0.0f) return;
        Frame f = frame_at(ct, m, o, b->position);
        float k = 1.0f - b->defeatFlash;
        for (int i = 0; i < 4; i++)
            ring(&f, 3.0f + k * (10.0f + i * 7.0f), 48, i * 0.3f, 0.10f + 0.05f * i, i & 1 ? g->hotColor : g->secondaryColor,
                 6.0f * b->defeatFlash * b->defeatFlash, 0.15f * i);
        for (int i = 0; i < 16; i++) {
            float a = i * PI / 8.0f + k;
            float r0 = 2.0f + k * 14.0f, r1 = r0 + 3.0f + k * 8.0f;
            ribbon(fp(&f, cosf(a) * r0, sinf(a) * r0, 0), fp(&f, cosf(a) * r1, sinf(a) * r1, 0), 0.08f, g->hotColor, 5.0f * b->defeatFlash);
        }
        return;
    }

    float fade = compile_fade(b->position.z);
    const Enemy *core = b->coreSlot >= 0 ? &g->enemies[b->coreSlot] : NULL;
    float r = core && core->active ? core->size.x * 0.5f : 3.1f;
    Frame f = frame_at(ct, m, o, b->position);

    for (int n = 0; n < 4; n++) {   // firewall nodes and their tethers
        int slot = b->nodeSlots[n];
        if (slot < 0 || !g->enemies[slot].active) continue;
        const Enemy *e = &g->enemies[slot];
        Frame nf = frame_at(ct, m, o, e->position);
        draw_boss_node(&nf, g, e, time, fade, emissive);
        if (emissive) ribbon(f.c, nf.c, 0.04f, enemy_color(g, e), 1.1f * fade);
    }
    if (!core || !core->active) return;

    bool approach = b->phase == BOSS_APPROACH;
    bool enraged = b->phase == BOSS_ENRAGED || b->phase == BOSS_SINGULARITY;
    float arrive = approach ? fminf(b->phaseTime / 2.6f, 1.0f) : 1.0f;
    float spread = b->phase == BOSS_EXPOSED ? smooth01(b->phaseTime / 0.8f) : (enraged ? 1.0f : 0.0f);
    float heat = enraged ? 0.6f + 0.4f * sinf(time * 12.0f) : 0.0f;
    Color base = mix_color(enemy_color(g, core), g->hotColor, heat * 0.7f);
    float gain = (2.4f + heat * 2.5f) * fade;
    float rot = b->rotation;

    // Petals: kites in the arena x/y plane, pitched like turbine blades.
    for (int i = 0; i < 8; i++) {
        float a = rot + i * PI * 0.25f;
        float rin = r * (0.85f + 0.45f * spread), rmid = r * (1.35f + 0.55f * spread), rout = r * (1.95f + 0.7f * spread);
        Vector3 pin = fp(&f, cosf(a) * rin, sinf(a) * rin, 0.0f);
        Vector3 pl = fp(&f, cosf(a - 0.24f) * rmid, sinf(a - 0.24f) * rmid, 0.18f * r);
        Vector3 pr = fp(&f, cosf(a + 0.24f) * rmid, sinf(a + 0.24f) * rmid, -0.18f * r);
        Vector3 pt = fp(&f, cosf(a) * rout, sinf(a) * rout, -0.35f * r);
        if (!emissive) {
            Color body = scale_color(base, 0.07f);
            tri(pin, pl, pt, body, 1.0f);
            tri(pin, pt, pr, body, 1.0f);
            continue;
        }
        ribbon(pin, pl, 0.06f, base, gain);
        ribbon(pl, pt, 0.06f, base, gain);
        ribbon(pt, pr, 0.06f, base, gain);
        ribbon(pr, pin, 0.06f, base, gain);
        ribbon(pin, pt, 0.035f, base, gain * 0.5f);   // spine
    }

    // Reactor: three nested, counter-spinning octahedra; the innermost is the eye.
    // The outer one matches the core's hit radius, so the target is what you see.
    float scl = r * 0.75f;
    for (int level = 0; level < 3; level++, scl *= 0.6f) {
        float dir = level & 1 ? -1.0f : 1.0f;
        Xform t = { scl, dir * time * (1.2f + level * 0.8f) + level, dir * time * 0.7f };
        if (!emissive) { if (level == 0) shape_body(&f, &SHAPE_OCT, t, base); continue; }
        Color c = level == 2 ? g->hotColor : base;
        float eye = level == 2 ? (b->shieldNodes > 0 ? 3.0f : 7.0f + heat * 3.0f) : gain;
        shape_edges(&f, &SHAPE_OCT, t, c, eye * fade, 0.05f - level * 0.01f);
    }
    if (!emissive) return;

    // Segmented halo behind the turbine, counter-rotating.
    Frame halo = frame_at(ct, m, o, (Vector3){ b->position.x, b->position.y, b->position.z - 0.6f * r });
    ring(&halo, r * (2.35f + 0.4f * spread), 16, -rot * 0.6f, 0.07f, base, gain * 0.8f, 0.4f);
    // Hex shield while any firewall node lives.
    if (b->shieldNodes > 0)
        ring(&f, r * 0.82f, 6, rot * 0.5f, 0.05f, g->secondaryColor,
             (1.6f + 0.6f * sinf(time * 5.0f)) * fade, 0.0f);
    if (core->locked) lock_brackets(&f, r * 1.1f, time, 0.09f, g->hotColor);

    // Arrival portal: rings and spokes compile around the core, then clear.
    if (approach) {
        float p = sinf(arrive * PI);
        for (int k = 0; k < 3; k++)
            ring(&f, r * (2.8f + k * 1.2f) * (0.6f + 0.4f * arrive), 24, (k & 1 ? -1.0f : 1.0f) * time * (1.0f + k * 0.5f),
                 0.08f, k == 1 ? g->hotColor : g->secondaryColor, 5.0f * p, 0.3f);
        for (int i = 0; i < 12; i++) {
            float a = i * PI / 6.0f + time * 0.4f;
            float r0 = r * 2.0f, r1 = r * (2.0f + 4.0f * p);
            ribbon(fp(&f, cosf(a) * r0, sinf(a) * r0, 0), fp(&f, cosf(a) * r1, sinf(a) * r1, 0), 0.05f, g->secondaryColor, 4.0f * p);
        }
    }
}

void combat_draw_scene(CombatDraw *d, CtTunnel *ct, const ArenaMap *m, const GameplaySystem *g,
                       const CtSceneView *view, bool emissive)
{
    if (!d->ready || view->reflection) return;
    d->camera = view->camera;
    for (int i = 0; i < 3; i++) d->origin[i] = view->origin[i];
    d->haveCamera = true;
    gCam = view->camera.position;
    const double *o = view->origin;
    float time = g->runTime;

    rlDrawRenderBatchActive();
    BeginShaderMode(d->shader);
    rlBegin(RL_QUADS);

    for (int i = 0; i < MAX_ENEMIES; i++) {
        const Enemy *e = &g->enemies[i];
        if (!e->active || e->type == ENEMY_BOSS_CORE || e->type == ENEMY_BOSS_NODE) continue;
        draw_enemy(ct, m, o, g, e, time, emissive);
    }
    draw_boss(ct, m, o, g, time, emissive);

    if (emissive) {
        // Player shots: bright core streak inside a coloured sheath.
        for (int i = 0; i < MAX_PLAYER_PROJECTILES; i++) {
            const PlayerProjectile *p = &g->playerProjectiles[i];
            if (!p->active) continue;
            Vector3 dir = Vector3Normalize(p->velocity);
            float len = p->charged ? 1.6f : 2.6f;
            Vector3 tailA = Vector3Subtract(p->position, Vector3Scale(dir, len));
            Frame head = frame_at(ct, m, o, p->position), tail = frame_at(ct, m, o, tailA);
            Color c = p->charged ? g->hotColor : g->secondaryColor;
            ribbon(tail.c, head.c, p->charged ? 0.16f : 0.11f, c, 4.0f);
            ribbon(tail.c, head.c, 0.04f, (Color){ 235, 250, 255, 255 }, 7.0f);
            if (p->charged) ring(&head, 0.55f, 3, time * 9.0f + i, 0.04f, c, 5.0f, 0.0f);
        }
        // Enemy shots: hot diamonds; boost-armed shots get an amber core and a trail.
        for (int i = 0; i < MAX_ENEMY_PROJECTILES; i++) {
            const EnemyProjectile *p = &g->projectiles[i];
            if (!p->active) continue;
            Frame f = frame_at(ct, m, o, p->position);
            Color c = p->homing ? (Color){ 255, 170, 40, 255 } : g->threatColor;
            Xform t = { 0.62f, time * 6.0f + i, time * 4.0f };
            shape_edges(&f, &SHAPE_OCT, t, c, 5.0f, 0.045f);
            Vector3 back = Vector3Subtract(p->position, Vector3Scale(Vector3Normalize(p->velocity), p->homing ? 2.4f : 1.2f));
            ribbon(frame_at(ct, m, o, back).c, f.c, 0.07f, c, 2.5f);
        }
        // Particles: debris streaks and shockwave rings.
        for (int i = 0; i < MAX_COMBAT_PARTICLES; i++) {
            const CombatParticle *p = &g->particles[i];
            if (!p->active) continue;
            float life = p->life / fmaxf(p->maxLife, 0.001f);
            if (p->shockwave) {
                Frame f = frame_at(ct, m, o, p->origin);
                ring(&f, 0.6f + (1.0f - life) * 4.5f, 24, 0.0f, 0.06f, p->color, 5.0f * life, 0.0f);
            } else {
                Vector3 back = Vector3Subtract(p->position, Vector3Scale(p->velocity, 0.06f));
                ribbon(frame_at(ct, m, o, back).c, frame_at(ct, m, o, p->position).c, 0.06f, p->color, 6.0f * life);
            }
        }
        // Chain-link beam: a lance down the player's lane.
        if (g->beamTimer > 0.0f) {
            float a = g->beamTimer / 0.34f;
            Vector3 p0 = g->player.position, p1 = p0;
            p0.z -= 1.0f;
            p1.z = -140.0f;
            for (int k = 0; k < 8; k++) {   // segmented so the lance follows the path's curve
                Vector3 s0 = Vector3Lerp(p0, p1, k / 8.0f), s1 = Vector3Lerp(p0, p1, (k + 1) / 8.0f);
                Vector3 r0 = frame_at(ct, m, o, s0).c, r1 = frame_at(ct, m, o, s1).c;
                ribbon(r0, r1, 0.9f * a, g->hotColor, 3.0f * a);
                ribbon(r0, r1, 0.22f * a, (Color){ 255, 250, 235, 255 }, 8.0f * a);
            }
        }
        // Chain-link bomb: three expanding rings around the ship.
        if (g->bombTimer > 0.0f) {
            float a = g->bombTimer / 0.72f;
            Frame f = frame_at(ct, m, o, g->player.position);
            for (int k = 0; k < 3; k++)
                ring(&f, (1.0f - a) * (26.0f + k * 9.0f), 40, k * 0.4f, 0.12f, k == 1 ? g->hotColor : g->secondaryColor, 6.0f * a, 0.0f);
        }
    }

    rlEnd();
    rlDrawRenderBatchActive();
    EndShaderMode();
}

// ---------------------------------------------------------------------------
// HUD

static void meter(int x, int y, int w, int h, float amount, int segments, Color on, Color off)
{
    int gap = 3, sw = (w - gap * (segments - 1)) / segments;
    for (int i = 0; i < segments; i++) {
        bool lit = (float)(i + 1) <= amount * segments + 0.001f;
        DrawRectangle(x + i * (sw + gap), y, sw, h, lit ? on : off);
    }
}

void combat_draw_hud(const CombatDraw *d, const GameplaySystem *g, int w, int h)
{
    (void)d;
    Color dim = { 150, 190, 210, 200 }, off = { 40, 60, 75, 160 };
    Color energyColor = g->player.energy < 30.0f ? g->threatColor : g->primaryColor;

    int x = 24, y = h - 108, bw = 300;
    DrawText("CORE // ENERGY", x, y, 13, dim);
    DrawText(TextFormat("%03d", (int)g->player.energy), x + bw - 34, y, 15, energyColor);
    meter(x, y + 18, bw, 9, g->player.energy / 100.0f, 25, energyColor, off);
    y += 42;
    DrawText("CHAIN LINKS", x, y, 13, dim);
    DrawText(TextFormat("%03d", g->chainLinks), x + bw - 34, y, 15, g->secondaryColor);
    meter(x, y + 18, bw, 9, (float)g->chainLinks / MAX_CHAIN_LINKS, 20, g->secondaryColor, off);
    const char *special = g->chainLinks >= BOMB_LINK_COST ? "K BEAM READY // L BOMB READY"
                        : g->chainLinks >= BEAM_LINK_COST ? "K BEAM READY // L BOMB 100"
                        : "K BEAM 50 // L BOMB 100";
    DrawText(special, x, y + 32, 11, dim);

    const char *score = TextFormat("SCORE %08d", g->score);
    DrawText(score, w - MeasureText(score, 22) - 24, 48, 22, (Color){ 225, 240, 250, 230 });
    static const char *weapons[4] = { "PULSE", "ACCELERATOR", "MULTILOCK-6", "OVERDRIVE" };
    static const int next[3] = { 4, 8, 12 };
    const char *weapon = TextFormat("WEAPON  %s", weapons[g->weaponTier]);
    Color weaponColor = g->weaponTier == 0 ? dim : (g->weaponTier >= 3 ? g->hotColor : g->secondaryColor);
    DrawText(weapon, w - MeasureText(weapon, 15) - 24, 78, 15, weaponColor);
    if (g->weaponTier < 3) {
        const char *nt = TextFormat("NEXT UPLINK  x%d", next[g->weaponTier]);
        DrawText(nt, w - MeasureText(nt, 11) - 24, 98, 11, dim);
    }
    if (g->combo > 1) {
        const char *combo = TextFormat("x%d CHAIN", g->combo);
        DrawText(combo, w - MeasureText(combo, 20) - 24, 116, 20, g->hotColor);
    }
    if (g->player.chargeTime > 0.12f) {
        const char *lock = TextFormat("LOCK %d", g->lockedCount);
        DrawText(lock, w / 2 - MeasureText(lock, 16) / 2, h / 2 + 90, 16, g->hotColor);
    }
    // Boss: name, core integrity and phase at the top; title card on arrival.
    static const char *bossNames[BOSSES_PER_RECURSION] = { "RECURSIVE CORE", "IRON BASTION", "PRISM WRAITH",
                                                           "SIEGE MANTIS", "FRACTAL LEVIATHAN" };
    const BossState *b = &g->boss;
    if (b->active && b->coreSlot >= 0) {
        const Enemy *core = &g->enemies[b->coreSlot];
        const char *name = bossNames[b->encounterIndex % BOSSES_PER_RECURSION];
        float hp = core->active && b->maxHealth > 0.0f ? core->health / b->maxHealth : 0.0f;
        int bw2 = 440, bx = w / 2 - bw2 / 2;
        Color bc = b->phase >= BOSS_ENRAGED ? g->hotColor : g->threatColor;
        DrawRectangle(bx - 12, 38, bw2 + 24, 44, (Color){ 0, 0, 0, 150 });   // keeps text readable over bright ceilings
        DrawText(name, bx, 46, 15, bc);
        const char *phase = b->phase == BOSS_APPROACH ? "COMPILING"
                          : b->shieldNodes > 0 ? TextFormat("FIREWALL x%d", b->shieldNodes)
                          : b->phase >= BOSS_ENRAGED ? "OVERLOAD" : "CORE EXPOSED";
        DrawText(phase, bx + bw2 - MeasureText(phase, 13), 48, 13, dim);
        float shown = b->phase == BOSS_APPROACH ? fminf(b->phaseTime / 2.6f, 1.0f) : hp;
        meter(bx, 66, bw2, 7, shown, 40, b->shieldNodes > 0 ? scale_color(bc, 0.55f) : bc, off);
        if (b->introFlash > 0.0f) {
            unsigned char a = (unsigned char)(fminf(b->introFlash * 1.6f, 1.0f) * 230.0f);
            const char *t = TextFormat("// %s //", name);
            DrawText(t, w / 2 - MeasureText(t, 44) / 2, h / 7, 44, (Color){ bc.r, bc.g, bc.b, a });
            const char *sub = TextFormat("ROUND %d", b->encounterIndex % BOSSES_PER_RECURSION + 1);
            DrawText(sub, w / 2 - MeasureText(sub, 16) / 2, h / 7 + 54, 16, (Color){ dim.r, dim.g, dim.b, a });
        }
    } else if (b->defeatFlash > 0.0f) {
        unsigned char a = (unsigned char)(fminf(b->defeatFlash * 1.5f, 1.0f) * 230.0f);
        const char *t = "CORE COLLAPSED";
        DrawText(t, w / 2 - MeasureText(t, 44) / 2, h / 7, 44, (Color){ g->hotColor.r, g->hotColor.g, g->hotColor.b, a });
        const char *sub = "ENERGY RESTORED";
        DrawText(sub, w / 2 - MeasureText(sub, 16) / 2, h / 7 + 54, 16, (Color){ dim.r, dim.g, dim.b, a });
    }
    if (g->overdriveActive) {
        const char *warn = "BOOST // GUIDED FIRE // HIT DAMAGE +15%";
        DrawText(warn, w / 2 - MeasureText(warn, 14) / 2, 22, 14, (Color){ 255, 170, 40, 220 });
    }
    // Hit flash: edge-only so the centre stays readable.
    if (g->hitFlash > 0.0f) {
        unsigned char a = (unsigned char)(g->hitFlash * 150.0f);
        Color edge = { 255, 35, 115, a }, clear = { 255, 35, 115, 0 };
        int band = h / 6;
        DrawRectangleGradientV(0, 0, w, band, edge, clear);
        DrawRectangleGradientV(0, h - band, w, band, clear, edge);
        DrawRectangleGradientH(0, 0, band, h, edge, clear);
        DrawRectangleGradientH(w - band, 0, band, h, clear, edge);
    }
    if (g->gameOver) {
        DrawRectangle(0, 0, w, h, (Color){ 0, 0, 0, 120 });
        const char *t = "SIGNAL LOST";
        DrawText(t, w / 2 - MeasureText(t, 56) / 2, h / 2 - 70, 56, g->threatColor);
        const char *s = TextFormat("SCORE %08d", g->score);
        DrawText(s, w / 2 - MeasureText(s, 24) / 2, h / 2, 24, (Color){ 225, 240, 250, 255 });
        const char *r = "R RECOMPILE RUN    ESC SEED SELECT";
        DrawText(r, w / 2 - MeasureText(r, 18) / 2, h / 2 + 44, 18, dim);
    }
}
