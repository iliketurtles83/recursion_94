// Neon vector presentation for combat. Everything is drawn with one tiny shader:
// vertex colour alpha encodes HDR gain (alpha 255 = 8x), so bright edges feed the
// engine's bloom while dark bodies (gain ~1) give the wireframes solid silhouettes.
#include "combat_draw.h"
#include "story.h"
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
static const char *DIM_FS =
    "#version 330\n"
    "in vec4 c; out vec4 o;\n"
    "void main(){ o = vec4(0.0, 0.0, 0.0, c.a); }\n";

// Combat focus: while anything hostile is on the field, the environment behind the
// fight is darkened by up to this much, so the order is enemy fire > enemies > world.
#define FOCUS_DIM 0.6f

void combat_draw_init(CombatDraw *d)
{
    *d = (CombatDraw){ 0 };
    d->shader = LoadShaderFromMemory(VS, FS);
    d->dimShader = LoadShaderFromMemory(VS, DIM_FS);
    d->ready = IsShaderValid(d->shader) && IsShaderValid(d->dimShader);
}

void combat_draw_unload(CombatDraw *d)
{
    if (IsShaderValid(d->shader)) UnloadShader(d->shader);
    if (IsShaderValid(d->dimShader)) UnloadShader(d->dimShader);
    d->ready = false;
}

void combat_draw_update(CombatDraw *d, const GameplaySystem *g, float dt)
{
    bool hostile = false;
    for (int i = 0; i < MAX_ENEMIES && !hostile; i++) hostile = g->enemies[i].active;
    for (int i = 0; i < MAX_ENEMY_PROJECTILES && !hostile; i++) hostile = g->projectiles[i].active;
    if (g->gameOver) hostile = false;
    d->focus += ((hostile ? 1.0f : 0.0f) - d->focus) * (1.0f - expf(-2.5f * dt));
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

// ---------------------------------------------------------------------------
// Role colours. Fire is fixed on every seed so the player learns it once (the
// bullet-hell rule): player fire is a white core in a cool sheath, enemy fire
// is one hot pink that nothing else may use. Enemies, bosses and debris keep
// the seed palette, pushed off the enemy-fire hue when a palette lands on it.

static const Color PLAYER_FIRE = { 120, 200, 255, 255 };
static const Color PLAYER_CORE = { 240, 250, 255, 255 };
static const Color ENEMY_FIRE = { 255, 28, 120, 255 };
static const Color ENEMY_FIRE_CORE = { 255, 228, 240, 255 };
#define ENEMY_FIRE_HUE 332.0f
#define FIRE_HUE_CLEARANCE 50.0f

static Color hsv_color(float h, float s, float v)
{
    h = fmodf(h + 360.0f, 360.0f) / 60.0f;
    float c = v * s, x = c * (1.0f - fabsf(fmodf(h, 2.0f) - 1.0f)), m = v - c;
    float r = 0, g = 0, b = 0;
    if (h < 1) { r = c; g = x; } else if (h < 2) { r = x; g = c; } else if (h < 3) { g = c; b = x; }
    else if (h < 4) { g = x; b = c; } else if (h < 5) { r = x; b = c; } else { r = c; b = x; }
    return (Color){ (unsigned char)((r + m) * 255.0f), (unsigned char)((g + m) * 255.0f), (unsigned char)((b + m) * 255.0f), 255 };
}

// A saturated colour within the clearance of the enemy-fire hue is turned away from it.
static Color off_fire_hue(Color c)
{
    float r = c.r / 255.0f, g = c.g / 255.0f, b = c.b / 255.0f;
    float mx = fmaxf(r, fmaxf(g, b)), mn = fminf(r, fminf(g, b)), d = mx - mn;
    if (mx <= 0.0f || d / mx < 0.35f) return c;   // near-white or grey: no hue to clash
    float h = mx == r ? 60.0f * fmodf((g - b) / d, 6.0f) : mx == g ? 60.0f * ((b - r) / d + 2.0f) : 60.0f * ((r - g) / d + 4.0f);
    if (h < 0.0f) h += 360.0f;
    float delta = fmodf(h - ENEMY_FIRE_HUE + 540.0f, 360.0f) - 180.0f;
    if (fabsf(delta) >= FIRE_HUE_CLEARANCE) return c;
    return hsv_color(ENEMY_FIRE_HUE + (delta < 0.0f ? -1.0f : 1.0f) * FIRE_HUE_CLEARANCE, d / mx, mx);
}

typedef struct { Color primary, secondary, hot, threat; } RolePalette;
static RolePalette gPal;   // the seed palette for this frame, off the enemy-fire hue

static void set_role_palette(const GameplaySystem *g)
{
    gPal = (RolePalette){ off_fire_hue(g->primaryColor), off_fire_hue(g->secondaryColor),
                          off_fire_hue(g->hotColor), off_fire_hue(g->threatColor) };
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

// Needle (sniper): long along z, pointing at the player.
static const Vector3 NEEDLE_V[6] = { {0,0,1.6f}, {0,0,-1.6f}, {0.3f,0,0}, {-0.3f,0,0}, {0,0.3f,0}, {0,-0.3f,0} };
static const int NEEDLE_E[12][2] = { {0,2},{0,3},{0,4},{0,5},{1,2},{1,3},{1,4},{1,5},{2,4},{4,3},{3,5},{5,2} };
static const int NEEDLE_F[8][3] = { {0,2,4},{0,4,3},{0,3,5},{0,5,2},{1,4,2},{1,3,4},{1,5,3},{1,2,5} };
// Flat hex hull (carrier): six corners in the x/z plane, top and bottom.
static const Vector3 HULL_V[12] = {
    { 1.0f, 0.42f, 0.0f }, { 0.5f, 0.42f, 0.69f }, { -0.5f, 0.42f, 0.69f }, { -1.0f, 0.42f, 0.0f }, { -0.5f, 0.42f, -0.69f }, { 0.5f, 0.42f, -0.69f },
    { 1.0f, -0.42f, 0.0f }, { 0.5f, -0.42f, 0.69f }, { -0.5f, -0.42f, 0.69f }, { -1.0f, -0.42f, 0.0f }, { -0.5f, -0.42f, -0.69f }, { 0.5f, -0.42f, -0.69f } };
static const int HULL_E[18][2] = { {0,1},{1,2},{2,3},{3,4},{4,5},{5,0}, {6,7},{7,8},{8,9},{9,10},{10,11},{11,6},
                                   {0,6},{1,7},{2,8},{3,9},{4,10},{5,11} };
static const int HULL_F[20][3] = { {0,1,2},{0,2,3},{0,3,4},{0,4,5}, {6,8,7},{6,9,8},{6,10,9},{6,11,10},
                                   {0,6,7},{0,7,1},{1,7,8},{1,8,2},{2,8,9},{2,9,3},{3,9,10},{3,10,4},{4,10,11},{4,11,5},{5,11,6},{5,6,0} };

static const Shape SHAPE_OCT = { OCT_V, 6, OCT_E, 12, OCT_F, 8 };
static const Shape SHAPE_NEEDLE = { NEEDLE_V, 6, NEEDLE_E, 12, NEEDLE_F, 8 };
static const Shape SHAPE_HULL = { HULL_V, 12, HULL_E, 18, HULL_F, 20 };
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

// Filled camera-facing disc, radius in arena units, pulled `lift` world units toward the
// camera (so an emissive disc sits in front of its dark backing instead of z-fighting).
static void disc(const Frame *f, float radius, int segments, Color c, float gain, float lift)
{
    Vector3 toCam = Vector3Normalize(Vector3Subtract(gCam, f->c));
    Vector3 u = Vector3CrossProduct(f->N, toCam);
    if (Vector3Length(u) < 1e-4f) u = f->B;
    u = Vector3Normalize(u);
    Vector3 v = Vector3CrossProduct(toCam, u);
    Vector3 c0 = Vector3Add(f->c, Vector3Scale(toCam, lift));
    float rw = radius * f->k;
    for (int i = 0; i < segments; i++) {
        float a0 = (float)i / segments * 2.0f * PI, a1 = (float)(i + 1) / segments * 2.0f * PI;
        Vector3 p0 = Vector3Add(c0, Vector3Add(Vector3Scale(u, cosf(a0) * rw), Vector3Scale(v, sinf(a0) * rw)));
        Vector3 p1 = Vector3Add(c0, Vector3Add(Vector3Scale(u, cosf(a1) * rw), Vector3Scale(v, sinf(a1) * rw)));
        tri(c0, p0, p1, c, gain);
    }
}

// Distant objects "compile in" over the spawn band instead of popping.
static float compile_fade(float z) { float t = (z + 132.0f) / 34.0f; return t < 0 ? 0 : (t > 1 ? 1 : t * t * (3 - 2 * t)); }

static Color enemy_color(const GameplaySystem *g, const Enemy *e)
{
    switch (e->type) {
        case ENEMY_DRIFTER: return gPal.primary;
        case ENEMY_CHASER: return gPal.threat;
        case ENEMY_SPLITTER: return gPal.secondary;
        case ENEMY_BOSS_NODE: return gPal.secondary;
        case ENEMY_SNIPER: return gPal.hot;
        case ENEMY_CARRIER: return mix_color(gPal.secondary, gPal.primary, 0.5f);
        case ENEMY_SUPPORT: return mix_color(gPal.primary, PLAYER_CORE, 0.55f);
        default: return mix_color(gPal.threat, gPal.hot, 0.45f);
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
        ribbon(corner, fp(f, (d0.x * 0.7f + d1.x * 0.3f) * s, (d0.y * 0.7f + d1.y * 0.3f) * s, 0), w, c, 4.0f);
        ribbon(corner, fp(f, (d0.x * 0.7f + d2.x * 0.3f) * s, (d0.y * 0.7f + d2.y * 0.3f) * s, 0), w, c, 4.0f);
    }
}

// Sniper, carrier and support (886ba1b roles). Each has its own outline;
// the sniper's mark and the support's tethers say what they are doing.
static void draw_role_enemy(const CtTunnel *ct, const ArenaMap *m, const double *o, const GameplaySystem *g,
                            const Enemy *e, const Frame *f, Color c, float fade, float time, bool emissive)
{
    float id = (float)e->identity, w = 0.05f;
    float gain = (2.8f + e->hurt * 4.0f) * fade;
    bool tele = e->phase == ENEMY_TELEGRAPH;
    if (e->type == ENEMY_SNIPER) {
        // Seen end-on from the chase camera, so the silhouette is a scope: a
        // ring with four spars around the needle's nose, turning slowly.
        Xform t = { 1.3f, 0.0f, 0.0f };
        if (!emissive) { if (fade > 0.05f) shape_body(f, &SHAPE_NEEDLE, t, c); return; }
        shape_edges(f, &SHAPE_NEEDLE, t, c, gain, w);
        float spin = time * 0.6f + id;
        ring(f, 1.15f, 8, spin, w, c, gain, 0.35f);
        for (int k = 0; k < 4; k++) {
            float a = spin + k * PI * 0.5f;
            ribbon(fp(f, cosf(a) * 0.55f, sinf(a) * 0.55f, 0.0f), fp(f, cosf(a) * 1.75f, sinf(a) * 1.75f, 0.0f), w, c, gain);
        }
        if (tele) {
            // The mark: a dashed sight line to the committed aim point and a
            // closing reticle there, brightening until the shot.
            float k = fminf(e->phaseTime / GetSniperMarkSeconds(g), 1.0f);
            float a = (1.5f + 4.5f * k) * fade;
            Vector3 from = e->position, to = e->aim;
            for (int seg = 0; seg < 24; seg += 2) {
                Vector3 p0 = Vector3Lerp(from, to, seg / 24.0f), p1 = Vector3Lerp(from, to, (seg + 1) / 24.0f);
                ribbon(frame_at(ct, m, o, p0).c, frame_at(ct, m, o, p1).c, 0.03f + 0.03f * k, ENEMY_FIRE, a);
            }
            Frame af = frame_at(ct, m, o, to);
            ring(&af, 1.4f - 0.7f * k, 4, PI * 0.25f, 0.04f, ENEMY_FIRE, a, 0.6f);
            ring(&af, 0.2f, 8, 0.0f, 0.04f, ENEMY_FIRE_CORE, a, 0.0f);
        }
    } else if (e->type == ENEMY_CARRIER) {
        float r = e->size.x * 0.55f;
        Xform t = { r, sinf(e->age * 0.4f + id) * 0.25f, 0.15f };
        if (!emissive) { if (fade > 0.05f) shape_body(f, &SHAPE_HULL, t, c); return; }
        shape_edges(f, &SHAPE_HULL, t, c, gain, w * 1.2f);
        // Launch bays at the sides; they open (ring grows, hot) before a launch.
        float open = tele ? fminf(e->phaseTime / CARRIER_BAY_SECONDS, 1.0f) : 0.0f;
        for (int side = -1; side <= 1; side += 2) {
            Frame bf = *f;
            bf.c = fp(f, (float)side * r * 0.62f, 0.0f, 0.4f);
            ring(&bf, 0.35f + 0.35f * open, 6, time * 2.0f * side, w, open > 0.0f ? ENEMY_FIRE : c,
                 (2.0f + 4.0f * open) * fade, 0.0f);
        }
    } else {   // support
        float r = e->size.x * 0.5f;
        Xform t = { r * 0.8f, time * 0.9f + id, time * 0.6f };
        if (!emissive) { if (fade > 0.05f) shape_body(f, &SHAPE_OCT, t, c); return; }
        shape_edges(f, &SHAPE_OCT, t, c, gain, w);
        ring(f, r * 1.5f, 6, -time * 0.8f + id, w, c, gain * 0.9f, 0.0f);       // hex halo
        ring(f, r * 1.9f, 6, time * 0.5f, w * 0.7f, c, gain * 0.5f, 0.5f);
    }
    if (e->locked) lock_brackets(f, e->size.x * 0.95f, time, w * 1.4f, PLAYER_FIRE);
}

// Support tether: a pulsing dashed line from a protected enemy to its support,
// and a small hex around the protected enemy (it takes half damage).
static void draw_support_tether(const CtTunnel *ct, const ArenaMap *m, const double *o, const GameplaySystem *g,
                                const Enemy *e, const Frame *f, float time, float fade)
{
    int slot = GetEnemySupport(g, e);
    if (slot < 0) return;
    const Enemy *s = &g->enemies[slot];
    Color c = enemy_color(g, s);
    float flow = time * 3.0f;
    for (int seg = 0; seg < 10; seg++) {
        float u0 = (seg + fmodf(flow, 1.0f)) / 10.0f, u1 = u0 + 0.05f;
        if (u1 > 1.0f) continue;
        Vector3 p0 = Vector3Lerp(s->position, e->position, u0), p1 = Vector3Lerp(s->position, e->position, u1);
        ribbon(frame_at(ct, m, o, p0).c, frame_at(ct, m, o, p1).c, 0.04f, c, 2.4f * fade);
    }
    ring(f, e->size.x * 0.85f, 6, time * 0.7f, 0.04f, c, 2.0f * fade, 0.0f);
}

static void draw_enemy(const CtTunnel *ct, const ArenaMap *m, const double *o, const GameplaySystem *g,
                       const Enemy *e, float time, bool emissive)
{
    Frame f = frame_at(ct, m, o, e->position);
    Color c = mix_color(enemy_color(g, e), PLAYER_CORE, e->hurt);   // white hit flash
    float fade = compile_fade(e->position.z);
    float id = (float)e->identity;
    float telegraph = e->phase == ENEMY_TELEGRAPH ? 0.5f + 0.5f * sinf(e->phaseTime * 28.0f) : 0.0f;
    float gain = (2.6f + telegraph * 3.5f + e->hurt * 4.0f) * fade;
    float r = e->size.x * 0.5f;
    float w = 0.05f;
    if (e->type >= ENEMY_SNIPER) { draw_role_enemy(ct, m, o, g, e, &f, c, fade, time, emissive); return; }
    const Shape *shape = e->type == ENEMY_CHASER ? &SHAPE_DART : e->type == ENEMY_SPLITTER ? &SHAPE_CUBE : &SHAPE_OCT;
    Xform t = { r, e->age * (e->type == ENEMY_SPLITTER ? 0.9f : 1.6f) + id, e->type == ENEMY_CHASER ? 0.0f : e->age * 0.7f + id * 0.37f };
    if (e->type == ENEMY_CHASER) t = (Xform){ r * 1.15f, 0.0f, 0.0f };
    if (!emissive) { if (fade > 0.05f) shape_body(&f, shape, t, c); return; }
    shape_edges(&f, shape, t, c, gain, w);
    draw_support_tether(ct, m, o, g, e, &f, time, fade);
    if (e->type == ENEMY_SPLITTER) {
        Xform inner = { r * 0.45f, -e->age * 1.7f, e->age * 1.1f };
        shape_edges(&f, &SHAPE_CUBE, inner, scale_color(c, 1.3f), gain * 1.3f, w * 0.8f);
    }
    // About to fire: the body blinks (gain above); no extra outline (clarity pass).
    if (e->locked) lock_brackets(&f, r * 1.9f, time, w * 1.4f, PLAYER_FIRE);
}


// ---------------------------------------------------------------------------
// Boss: the Recursive Core. A turbine of eight kite petals around a reactor of
// nested octahedra (each level a smaller, counter-spinning copy), a segmented
// halo, and a hex shield while firewall nodes live. Petals spread open when the
// core is exposed; enraged, the whole assembly heats toward the hot colour.

static float smooth01(float t) { t = t < 0 ? 0 : (t > 1 ? 1 : t); return t * t * (3 - 2 * t); }

// 1 on a music grid line, decaying after it (0 with no clock).
static float music_flash(const GameplaySystem *g, int division, float decay)
{
    float since = music_clock_since(&g->music, division);
    return since < 10.0f ? expf(-since * decay) : 0.0f;
}

static void draw_boss_node(const Frame *f, const GameplaySystem *g, const Enemy *e, float time, float fade, bool emissive)
{
    fade *= 1.0f + 1.8f * music_flash(g, MUSIC_BAR, 7.0f);   // firewall nodes flash on the downbeat
    float r = e->size.x * 0.7f, id = (float)e->identity;   // drawn ~ the hit radius (0.55 + 0.28 size)
    Color c = mix_color(enemy_color(g, e), PLAYER_CORE, e->hurt);
    Xform t = { r, time * 1.4f + id, time * 0.9f + id * 0.5f };
    if (!emissive) { shape_body(f, &SHAPE_OCT, t, c); return; }
    shape_edges(f, &SHAPE_OCT, t, c, 3.2f * fade, 0.05f);
    shape_edges(f, &SHAPE_CUBE, (Xform){ r * 0.38f, -time * 2.1f, time * 1.3f }, gPal.hot, 4.0f * fade, 0.04f);
    ring(f, r * 1.55f, 4, -time * 1.8f + id, 0.05f, c, 2.4f * fade, 0.45f);   // bracket square
    if (e->locked) lock_brackets(f, r * 2.0f, time, 0.07f, PLAYER_FIRE);
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
            ring(&f, 3.0f + k * (10.0f + i * 7.0f), 48, i * 0.3f, 0.10f + 0.05f * i, i & 1 ? gPal.hot : gPal.secondary,
                 6.0f * b->defeatFlash * b->defeatFlash, 0.15f * i);
        for (int i = 0; i < 16; i++) {
            float a = i * PI / 8.0f + k;
            float r0 = 2.0f + k * 14.0f, r1 = r0 + 3.0f + k * 8.0f;
            ribbon(fp(&f, cosf(a) * r0, sinf(a) * r0, 0), fp(&f, cosf(a) * r1, sinf(a) * r1, 0), 0.08f, gPal.hot, 5.0f * b->defeatFlash);
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
        if (emissive) ribbon(f.c, nf.c, 0.04f, enemy_color(g, e), (1.1f + 2.5f * music_flash(g, MUSIC_BAR, 7.0f)) * fade);
    }
    if (!core || !core->active) return;

    bool approach = b->phase == BOSS_APPROACH;
    bool enraged = b->phase == BOSS_ENRAGED || b->phase == BOSS_SINGULARITY;
    float arrive = approach ? fminf(b->phaseTime / 2.6f, 1.0f) : 1.0f;
    float spread = b->phase == BOSS_EXPOSED ? smooth01(b->phaseTime / 0.8f) : (enraged ? 1.0f : 0.0f);
    float heat = enraged ? 0.6f + 0.4f * sinf(time * 12.0f) : 0.0f;
    Color base = mix_color(mix_color(enemy_color(g, core), gPal.hot, heat * 0.7f), PLAYER_CORE, core->hurt * 0.8f);
    float gain = (2.4f + heat * 2.5f + core->hurt * 3.0f) * fade;
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
        Color c = level == 2 ? gPal.hot : base;
        float eye = level == 2 ? (b->shieldNodes > 0 ? 3.0f : 7.0f + heat * 3.0f) : gain;
        shape_edges(&f, &SHAPE_OCT, t, c, eye * fade, 0.05f - level * 0.01f);
    }
    if (!emissive) return;

    // Segmented halo behind the turbine, counter-rotating.
    Frame halo = frame_at(ct, m, o, (Vector3){ b->position.x, b->position.y, b->position.z - 0.6f * r });
    ring(&halo, r * (2.35f + 0.4f * spread), 16, -rot * 0.6f, 0.07f, base, gain * 0.8f, 0.4f);
    // Hex shield while any firewall node lives.
    if (b->shieldNodes > 0)
        ring(&f, r * 0.82f, 6, rot * 0.5f, 0.05f, gPal.secondary,
             (1.6f + 0.6f * sinf(time * 5.0f)) * fade, 0.0f);
    if (core->locked) lock_brackets(&f, r * 1.1f, time, 0.09f, PLAYER_FIRE);

    // Arrival portal: rings and spokes compile around the core, then clear.
    if (approach) {
        float p = sinf(arrive * PI);
        for (int k = 0; k < 3; k++)
            ring(&f, r * (2.8f + k * 1.2f) * (0.6f + 0.4f * arrive), 24, (k & 1 ? -1.0f : 1.0f) * time * (1.0f + k * 0.5f),
                 0.08f, k == 1 ? gPal.hot : gPal.secondary, 5.0f * p, 0.3f);
        for (int i = 0; i < 12; i++) {
            float a = i * PI / 6.0f + time * 0.4f;
            float r0 = r * 2.0f, r1 = r * (2.0f + 4.0f * p);
            ribbon(fp(&f, cosf(a) * r0, sinf(a) * r0, 0), fp(&f, cosf(a) * r1, sinf(a) * r1, 0), 0.05f, gPal.secondary, 4.0f * p);
        }
    }
}

// Darken the world already drawn behind the fight: a soft camera-facing ellipse over the
// arena where enemies hold (z ~ -26), drawn with no depth test before any combat glow.
// The ship, near the camera at the bottom of the view, stays outside it.
static void draw_focus_dim(const CombatDraw *d, CtTunnel *ct, const ArenaMap *m, const double *o, const GameplaySystem *g)
{
    float a = FOCUS_DIM * d->focus;
    if (a <= 0.003f) return;
    const GameplayArena *ar = &g->arena;
    Frame f = frame_at(ct, m, o, (Vector3){ 0.5f * (ar->minX + ar->maxX), 0.55f * (ar->minY + ar->maxY), -26.0f });
    Vector3 toCam = Vector3Normalize(Vector3Subtract(gCam, f.c));
    Vector3 u = Vector3Normalize(Vector3CrossProduct(f.N, toCam));
    Vector3 v = Vector3CrossProduct(toCam, u);
    float rx = 1.0f * (ar->maxX - ar->minX) * f.k, ry = 0.8f * (ar->maxY - ar->minY) * f.k;
    unsigned char inner = (unsigned char)(a * 255.0f);
    rlDrawRenderBatchActive();
    rlDisableDepthTest();
    rlSetBlendMode(BLEND_ALPHA);
    BeginShaderMode(d->dimShader);
    rlBegin(RL_QUADS);
    const int SEG = 32;
    const float RING[3] = { 0.0f, 0.4f, 1.0f };        // full strength to 40%, fading to the rim
    const unsigned char ALPHA[3] = { inner, inner, 0 };
    for (int band = 0; band < 2; band++) {
        for (int i = 0; i < SEG; i++) {
            float a0 = (float)i / SEG * 2.0f * PI, a1 = (float)(i + 1) / SEG * 2.0f * PI;
            Vector3 q[4];
            float rr[4] = { RING[band], RING[band + 1], RING[band + 1], RING[band] };
            float aa[4] = { a0, a0, a1, a1 };
            unsigned char al[4] = { ALPHA[band], ALPHA[band + 1], ALPHA[band + 1], ALPHA[band] };
            for (int k = 0; k < 4; k++) {
                q[k] = Vector3Add(f.c, Vector3Add(Vector3Scale(u, cosf(aa[k]) * rx * rr[k]), Vector3Scale(v, sinf(aa[k]) * ry * rr[k])));
                rlColor4ub(0, 0, 0, al[k]);
                rlVertex3f(q[k].x, q[k].y, q[k].z);
            }
        }
    }
    rlEnd();
    rlDrawRenderBatchActive();
    EndShaderMode();
    rlSetBlendMode(BLEND_ADDITIVE);
    rlEnableDepthTest();
}

// ---------------------------------------------------------------------------
// Enemy shots (clarity pass): one hit radius, a shape per source, a cap on
// their size on screen, and they fade once past the ship (they can no longer hit).

#define NEEDLE_LENGTH 1.6f
#define SHOT_MAX_ANGLE 0.022f  // radians: the most an orb may cover on screen

static float shot_radius(int kind) { return kind == SHOT_BOSS ? 0.62f : kind == SHOT_NEEDLE ? 0.3f : 0.42f; }

static float shot_scale(const Frame *f, float radius)
{
    float dist = Vector3Length(Vector3Subtract(f->c, gCam));
    float angle = radius * f->k / fmaxf(dist, 1e-3f);
    return angle > SHOT_MAX_ANGLE ? SHOT_MAX_ANGLE / angle : 1.0f;
}

static float shot_alpha(const GameplaySystem *g, const EnemyProjectile *p)
{
    float past = p->position.z - (g->player.position.z + 0.5f);
    return past <= 0.0f ? 1.0f : fmaxf(0.0f, 1.0f - past / 2.5f);
}

// Crosshair: corner brackets on the tap-shot line at two depths (read the
// angle from their offset). Charging, the near one opens into the real lock
// cone at its depth, with a pip per lock; full charge turns it white.
#define RETICLE_NEAR 18.0f
#define RETICLE_FAR 40.0f

static void reticle_corners(const Frame *f, float s, float w, Color c, float gain)
{
    for (int k = 0; k < 4; k++) {
        float sx = (k & 1) ? 1.0f : -1.0f, sy = (k & 2) ? 1.0f : -1.0f;
        Vector3 corner = fp(f, sx * s, sy * s, 0);
        ribbon(corner, fp(f, sx * s * 0.5f, sy * s, 0), w, c, gain);
        ribbon(corner, fp(f, sx * s, sy * s * 0.5f, 0), w, c, gain);
        ribbon(corner, fp(f, sx * s * 0.75f, sy * s, 0), w * 0.4f, PLAYER_CORE, gain * 1.6f);
        ribbon(corner, fp(f, sx * s, sy * s * 0.75f, 0), w * 0.4f, PLAYER_CORE, gain * 1.6f);
    }
}

static void draw_reticle(const CtTunnel *ct, const ArenaMap *m, const double *o, const GameplaySystem *g, float time)
{
    if (g->gameOver) return;
    Vector3 p = g->player.position;
    Frame nearF = frame_at(ct, m, o, (Vector3){ p.x, p.y, p.z - RETICLE_NEAR });
    Frame farF = frame_at(ct, m, o, (Vector3){ p.x, p.y, p.z - RETICLE_FAR });
    reticle_corners(&farF, 0.9f, 0.08f, PLAYER_FIRE, 3.0f);
    float charge = g->player.chargeTime;
    if (charge <= 0.12f) { reticle_corners(&nearF, 1.2f, 0.09f, PLAYER_FIRE, 4.0f); return; }
    // Same cone as UpdateLocks in gameplay.c, at the near reticle's depth.
    float cone = 1.0f + fminf(charge, 1.2f) * 2.1f + RETICLE_NEAR * 0.035f;
    bool full = charge > 0.72f;
    Color c = full ? PLAYER_CORE : PLAYER_FIRE;
    reticle_corners(&nearF, cone * 0.72f, 0.09f, c, full ? 5.0f + 1.5f * sinf(time * 20.0f) : 4.0f);
    int maxLocks = g->weaponTier >= 2 ? 6 : 4;
    for (int i = 0; i < maxLocks; i++) {
        float a = PI * 0.5f + ((float)i - (float)(maxLocks - 1) * 0.5f) * 0.22f;
        bool lit = i < g->lockedCount;
        Vector3 c0 = fp(&nearF, cosf(a) * (cone * 0.72f + 0.35f), sinf(a) * (cone * 0.72f + 0.35f), 0);
        Vector3 c1 = fp(&nearF, cosf(a) * (cone * 0.72f + 0.75f), sinf(a) * (cone * 0.72f + 0.75f), 0);
        ribbon(c0, c1, lit ? 0.09f : 0.05f, lit ? PLAYER_CORE : PLAYER_FIRE, lit ? 6.0f : 1.5f);
    }
}

// ---------------------------------------------------------------------------
// Pickup weapons (player colours: they are ours)

static uint32_t draw_hash(uint32_t v)
{
    v ^= v >> 16; v *= 0x7feb352du; v ^= v >> 15; v *= 0x846ca68bu; v ^= v >> 16;
    return v;
}

// Capsule: a spinning cube cage around the weapon's glyph, ringed on the beat.
// ARC = a zigzag bolt, DRILL = a three-blade spike, MIRROR = a hexagon.
static void draw_capsule(const CtTunnel *ct, const ArenaMap *m, const double *o, const GameplaySystem *g,
                         const WeaponCapsule *c, float time, bool emissive)
{
    Frame f = frame_at(ct, m, o, c->position);
    float spin = time * 1.6f + c->position.x;
    Xform cage = { 1.0f, spin, 0.6f };
    if (!emissive) { shape_body(&f, &SHAPE_CUBE, (Xform){ 0.9f, spin, 0.6f }, (Color){ 6, 10, 16, 255 }); return; }
    float beat = music_flash(g, MUSIC_BEAT, 6.0f);
    shape_edges(&f, &SHAPE_CUBE, cage, PLAYER_FIRE, 3.2f + 2.0f * beat, 0.05f);
    ring(&f, 1.9f + 0.3f * beat, 4, -spin * 0.5f, 0.05f, PLAYER_CORE, 3.0f + 3.0f * beat, 0.55f);
    // The glyph sits on the cage's near face, large enough to read the type at a glance.
    const float G = 1.5f, Z = 1.2f;
    Color gc = mix_color(PLAYER_FIRE, PLAYER_CORE, 0.5f);
    if (c->type == WEAPON_ARC) {
        Vector3 pts[4] = { fp(&f, -0.45f * G, 0.6f * G, Z), fp(&f, 0.15f * G, 0.12f * G, Z),
                           fp(&f, -0.15f * G, -0.1f * G, Z), fp(&f, 0.45f * G, -0.6f * G, Z) };
        for (int k = 0; k < 3; k++) ribbon(pts[k], pts[k + 1], 0.13f, gc, 6.0f);
    } else if (c->type == WEAPON_DRILL) {
        for (int k = 0; k < 3; k++) {
            float a = spin * 2.0f + k * 2.0943951f;
            ribbon(fp(&f, 0, 0, Z), fp(&f, cosf(a) * 0.6f * G, sinf(a) * 0.6f * G, Z), 0.13f, gc, 6.0f);
        }
    } else {
        Frame front = f;
        front.c = fp(&f, 0, 0, Z);
        ring(&front, 0.55f * G, 6, spin, 0.12f, gc, 6.0f, 0.0f);
    }
}

// Arc: a jagged bolt along the landed hits; the jaggies re-roll on every 16th.
static void draw_arc(const CtTunnel *ct, const ArenaMap *m, const double *o, const GameplaySystem *g,
                     const ArcBolt *a, int index)
{
    float alpha = a->done ? fmaxf(a->fade / 0.35f, 0.0f) : 1.0f;
    uint32_t tick = (uint32_t)(g->music.valid ? g->music.step : g->runTime * 8.0f);
    for (int k = 0; k < a->hits; k++) {
        Vector3 p0 = a->points[k], p1 = a->points[k + 1];
        Vector3 prev = frame_at(ct, m, o, p0).c;
        const int SEG = 7;
        for (int s = 1; s <= SEG; s++) {
            float t = (float)s / SEG;
            Vector3 p = Vector3Lerp(p0, p1, t);
            if (s < SEG) {
                uint32_t h = draw_hash(tick * 131u + (uint32_t)(index * 17 + k * 7 + s));
                float j = 0.9f * sinf(t * PI);
                p.x += ((float)(h & 255u) / 127.5f - 1.0f) * j;
                p.y += ((float)((h >> 8) & 255u) / 127.5f - 1.0f) * j;
            }
            Vector3 cur = frame_at(ct, m, o, p).c;
            ribbon(prev, cur, 0.16f * alpha, PLAYER_FIRE, 3.5f * alpha);
            ribbon(prev, cur, 0.05f * alpha, PLAYER_CORE, 8.0f * alpha);
            prev = cur;
        }
        Frame hit = frame_at(ct, m, o, p1);
        ring(&hit, 0.9f, 8, (float)k, 0.05f, PLAYER_CORE, 5.0f * alpha, 0.4f);
    }
}

// Drill: a spinning spike in flight; stuck, a closing ring counts down to the bar line.
static void draw_drill(const CtTunnel *ct, const ArenaMap *m, const double *o, const GameplaySystem *g,
                       const DrillRound *d, float time)
{
    Frame f = frame_at(ct, m, o, d->position);
    float spin = time * 14.0f + d->age;
    for (int k = 0; k < 3; k++) {
        float a = spin + k * 2.0943951f;
        ribbon(fp(&f, 0, 0, 0), fp(&f, cosf(a) * 0.6f, sinf(a) * 0.6f, 0.4f), 0.07f, PLAYER_CORE, 7.0f);
    }
    if (d->stuck > 0.0f) {
        float sixteenth = music_flash(g, MUSIC_STEP, 18.0f);
        float bar = g->music.valid ? music_clock_since(&g->music, MUSIC_BAR) * g->music.stepsPerSecond / MUSIC_BAR : fminf(d->stuck / 1.6f, 1.0f);
        ring(&f, 2.6f - 1.8f * bar, 12, -spin * 0.2f, 0.07f, gPal.hot, 4.0f + 3.0f * sixteenth, 0.3f);
        ring(&f, 0.5f, 6, spin * 0.3f, 0.06f, PLAYER_CORE, 4.0f + 4.0f * sixteenth, 0.0f);
    } else {
        Vector3 back = Vector3Subtract(d->position, Vector3Scale(Vector3Normalize(d->velocity), 1.6f));
        ribbon(frame_at(ct, m, o, back).c, f.c, 0.12f, PLAYER_FIRE, 4.0f);
    }
}

// Mirror: a faint hex around the ship while charged; bright at the catch radius during a roll.
static void draw_mirror(const CtTunnel *ct, const ArenaMap *m, const double *o, const GameplaySystem *g, float time)
{
    if (g->weapon != WEAPON_MIRROR) return;
    Frame f = frame_at(ct, m, o, g->player.position);
    if (g->player.rolling) {
        ring(&f, MIRROR_CATCH_RADIUS, 6, time * 6.0f, 0.045f, PLAYER_CORE, 2.5f + 4.0f * g->mirrorFlash, 0.0f);
        ring(&f, MIRROR_CATCH_RADIUS * 0.8f, 6, -time * 6.0f, 0.03f, PLAYER_FIRE, 2.0f, 0.3f);
    } else if (g->ammo > 0) {
        ring(&f, MIRROR_CATCH_RADIUS, 6, time * 0.8f, 0.03f, PLAYER_FIRE, 1.6f, 0.5f);
    }
    if (g->mirrorFlash > 0.0f)
        ring(&f, MIRROR_CATCH_RADIUS + (1.0f - g->mirrorFlash) * 3.0f, 6, 0, 0.06f, PLAYER_CORE, 5.0f * g->mirrorFlash, 0.0f);
}

void combat_draw_scene(CombatDraw *d, CtTunnel *ct, const ArenaMap *m, const GameplaySystem *g,
                       const CtSceneView *view, bool emissive)
{
    if (!d->ready || view->reflection) return;
    d->camera = view->camera;
    for (int i = 0; i < 3; i++) d->origin[i] = view->origin[i];
    d->haveCamera = true;
    gCam = view->camera.position;
    set_role_palette(g);
    const double *o = view->origin;
    float time = g->runTime;
    if (emissive) draw_focus_dim(d, ct, m, o, g);

    rlDrawRenderBatchActive();
    BeginShaderMode(d->shader);
    rlBegin(RL_QUADS);

    for (int i = 0; i < MAX_ENEMIES; i++) {
        const Enemy *e = &g->enemies[i];
        if (!e->active || e->type == ENEMY_BOSS_CORE || e->type == ENEMY_BOSS_NODE) continue;
        draw_enemy(ct, m, o, g, e, time, emissive);
    }
    draw_boss(ct, m, o, g, time, emissive);
    for (int i = 0; i < MAX_CAPSULES; i++)
        if (g->capsules[i].active) draw_capsule(ct, m, o, g, &g->capsules[i], time, emissive);

    // Enemy shots, opaque pass: a dark rim disc that cuts the shot out of the bright
    // corridor behind it. Sizes per shot kind (shot_radius); one hit radius, ENEMY_SHOT_HIT_RADIUS.
    if (!emissive) {
        for (int i = 0; i < MAX_ENEMY_PROJECTILES; i++) {
            const EnemyProjectile *p = &g->projectiles[i];
            if (!p->active) continue;
            float a = shot_alpha(g, p);
            if (a <= 0.02f) continue;
            Frame f = frame_at(ct, m, o, p->position);
            float s = shot_scale(&f, shot_radius(p->kind));
            if (p->kind == SHOT_NEEDLE) {
                Vector3 back = Vector3Subtract(p->position, Vector3Scale(Vector3Normalize(p->velocity), NEEDLE_LENGTH));
                ribbon(frame_at(ct, m, o, back).c, f.c, 0.2f * s, (Color){ 10, 2, 6, 255 }, 1.0f);
            } else {
                disc(&f, (shot_radius(p->kind) + 0.2f) * s, 14, (Color){ 10, 2, 6, 255 }, 1.0f, 0.0f);
            }
        }
    }

    if (emissive) {
        // Player shots: white core streak inside a cool sheath, on every seed.
        for (int i = 0; i < MAX_PLAYER_PROJECTILES; i++) {
            const PlayerProjectile *p = &g->playerProjectiles[i];
            if (!p->active) continue;
            Vector3 dir = Vector3Normalize(p->velocity);
            float len = p->charged ? 1.8f : 2.8f;
            Vector3 tailA = Vector3Subtract(p->position, Vector3Scale(dir, len));
            Frame head = frame_at(ct, m, o, p->position), tail = frame_at(ct, m, o, tailA);
            ribbon(tail.c, head.c, p->charged ? 0.20f : 0.10f, PLAYER_FIRE, p->charged ? 4.5f : 3.5f);
            ribbon(tail.c, head.c, p->charged ? 0.06f : 0.035f, PLAYER_CORE, 7.0f);
            if (p->charged) ring(&head, 0.55f, 3, time * 9.0f + i, 0.04f, PLAYER_FIRE, 5.0f, 0.0f);
        }
        // Enemy shots: filled round orbs in the reserved hot pink with a near-white
        // centre and a thin bright edge; boost-armed (homing) shots add a spinning
        // dashed ring and a longer tail.
        for (int i = 0; i < MAX_ENEMY_PROJECTILES; i++) {
            const EnemyProjectile *p = &g->projectiles[i];
            if (!p->active) continue;
            float a = shot_alpha(g, p);
            if (a <= 0.02f) continue;
            Frame f = frame_at(ct, m, o, p->position);
            float r = shot_radius(p->kind), s = shot_scale(&f, r);
            Vector3 dir = Vector3Normalize(p->velocity);
            if (p->kind == SHOT_NEEDLE) {
                // A short needle along its flight: reads as a dash, not a blob.
                Vector3 back = Vector3Subtract(p->position, Vector3Scale(dir, NEEDLE_LENGTH));
                Vector3 b = frame_at(ct, m, o, back).c;
                ribbon(b, f.c, 0.12f * s, ENEMY_FIRE, 3.0f * a);
                ribbon(b, f.c, 0.04f * s, ENEMY_FIRE_CORE, 6.0f * a);
            } else {
                float pulse = 0.85f + 0.15f * sinf(time * 18.0f + i);
                disc(&f, r * s, 14, ENEMY_FIRE, 2.6f * pulse * a, 0.03f);
                disc(&f, r * 0.48f * s, 10, ENEMY_FIRE_CORE, 5.5f * a, 0.06f);
                ring(&f, r * 1.06f * s, 14, 0.0f, 0.05f * s, ENEMY_FIRE, 5.0f * a, 0.0f);
                Vector3 back = Vector3Subtract(p->position, Vector3Scale(dir, p->homing ? 2.6f : 1.0f));
                ribbon(frame_at(ct, m, o, back).c, f.c, 0.07f * s, ENEMY_FIRE, 1.6f * a);
            }
            if (p->homing) ring(&f, (r + 0.26f) * s, 6, time * 7.0f + i, 0.05f * s, ENEMY_FIRE, 4.0f * a, 0.45f);
        }
        // Sniper rails: a white-hot line along the committed aim, past the ship's plane.
        for (int i = 0; i < MAX_RAILS; i++) {
            const RailShot *r = &g->rails[i];
            if (!r->active) continue;
            float a = r->life / RAIL_SHOW_SECONDS;
            Vector3 dir = Vector3Subtract(r->aim, r->from);
            Vector3 end = Vector3Add(r->aim, Vector3Scale(dir, 8.0f / fmaxf(fabsf(dir.z), 1.0f)));
            for (int k = 0; k < 8; k++) {
                Vector3 r0 = frame_at(ct, m, o, Vector3Lerp(r->from, end, k / 8.0f)).c;
                Vector3 r1 = frame_at(ct, m, o, Vector3Lerp(r->from, end, (k + 1) / 8.0f)).c;
                ribbon(r0, r1, 0.12f * a, ENEMY_FIRE, 4.0f * a);
                ribbon(r0, r1, 0.035f, ENEMY_FIRE_CORE, 8.0f * a);
            }
        }
        // Particles: debris streaks and shockwave rings.
        for (int i = 0; i < MAX_COMBAT_PARTICLES; i++) {
            const CombatParticle *p = &g->particles[i];
            if (!p->active) continue;
            float life = p->life / fmaxf(p->maxLife, 0.001f);
            if (p->shockwave) {
                Frame f = frame_at(ct, m, o, p->origin);
                ring(&f, 0.6f + (1.0f - life) * 4.5f, 24, 0.0f, 0.06f, off_fire_hue(p->color), 5.0f * life, 0.0f);
            } else {
                Vector3 back = Vector3Subtract(p->position, Vector3Scale(p->velocity, 0.06f));
                ribbon(frame_at(ct, m, o, back).c, frame_at(ct, m, o, p->position).c, 0.06f, off_fire_hue(p->color), 6.0f * life);
            }
        }
        // Core lattice guide: the safe lane's edges as inward brackets at three
        // depths ahead of the ship, in the player's colours (safe = ours).
        const BossState *b = &g->boss;
        if (b->active && b->latticeGuide > 0.0f) {
            float a = fminf(b->latticeGuide / 0.3f, 1.0f) * (0.75f + 0.25f * sinf(time * 14.0f));
            float y0 = g->arena.minY, y1 = g->arena.maxY;
            for (int k = 0; k < 3; k++) {
                float z = g->player.position.z - 3.0f - 9.0f * (float)k;
                float fade = a * (1.0f - 0.28f * (float)k);
                for (int side = -1; side <= 1; side += 2) {
                    float x = b->latticeLane + (float)side * (BOSS_LATTICE_GAP - ENEMY_SHOT_HIT_RADIUS);
                    float lip = -(float)side * 0.9f;
                    Vector3 lo = frame_at(ct, m, o, (Vector3){ x, y0, z }).c;
                    Vector3 hi = frame_at(ct, m, o, (Vector3){ x, y1, z }).c;
                    Vector3 loLip = frame_at(ct, m, o, (Vector3){ x + lip, y0, z }).c;
                    Vector3 hiLip = frame_at(ct, m, o, (Vector3){ x + lip, y1, z }).c;
                    ribbon(lo, hi, 0.07f, PLAYER_FIRE, 3.5f * fade);
                    ribbon(lo, loLip, 0.07f, PLAYER_CORE, 5.0f * fade);
                    ribbon(hi, hiLip, 0.07f, PLAYER_CORE, 5.0f * fade);
                }
            }
        }
        draw_reticle(ct, m, o, g, time);
        for (int i = 0; i < MAX_ARCS; i++) if (g->arcs[i].active) draw_arc(ct, m, o, g, &g->arcs[i], i);
        for (int i = 0; i < MAX_DRILLS; i++) if (g->drills[i].active) draw_drill(ct, m, o, g, &g->drills[i], time);
        draw_mirror(ct, m, o, g, time);
        // Beam: a data stream down the ship's lane. A wide sheath and white core,
        // rings racing out along it, and packets streaming forward.
        if (g->beamSteps > 0.0f) {
            float t = g->beamTimer;
            float a = fminf(t / 0.08f, 1.0f) * fminf(g->beamSteps, 1.0f);
            Vector3 p0 = g->player.position, p1 = p0;
            p0.z -= 1.0f;
            p1.z = -140.0f;
            // Segmented so it follows the path's curve; the first stretch tapers
            // from a thread at the nose, so it does not white out the ship.
            const float edges[13] = { 0, 0.01f, 0.025f, 0.05f, 0.09f, 0.14f, 0.2f, 0.3f, 0.42f, 0.56f, 0.7f, 0.85f, 1.0f };
            for (int k = 0; k < 12; k++) {
                Vector3 r0 = frame_at(ct, m, o, Vector3Lerp(p0, p1, edges[k])).c;
                Vector3 r1 = frame_at(ct, m, o, Vector3Lerp(p0, p1, edges[k + 1])).c;
                float taper = fminf(edges[k + 1] / 0.14f, 1.0f);
                taper = 0.15f + 0.85f * taper * taper;
                ribbon(r0, r1, 1.0f * a * taper, PLAYER_FIRE, 1.5f * a * taper);
                ribbon(r0, r1, 0.4f * a * taper, PLAYER_FIRE, 3.5f * a);
                ribbon(r0, r1, 0.12f * a * taper, PLAYER_CORE, 8.0f * a);
            }
            for (int k = 0; k < 6; k++) {
                float z = p0.z - fmodf(t * 90.0f + k * 22.0f, 132.0f);
                Frame f = frame_at(ct, m, o, (Vector3){ p0.x, p0.y, z });
                ring(&f, 1.7f, 8, t * 4.0f + k, 0.06f, PLAYER_CORE, 5.0f * a, 0.3f);
            }
            for (int k = 0; k < 28; k++) {
                uint32_t h = draw_hash((uint32_t)k * 2654435761u + 17u);
                float ox = ((float)(h & 255u) / 127.5f - 1.0f) * 1.1f;
                float oy = ((float)((h >> 8) & 255u) / 127.5f - 1.0f) * 1.1f;
                float z = p0.z - fmodf(t * 140.0f + (float)((h >> 16) % 130u), 130.0f);
                Vector3 q0 = { p0.x + ox, p0.y + oy, z }, q1 = q0;
                q1.z -= 3.0f;
                ribbon(frame_at(ct, m, o, q0).c, frame_at(ct, m, o, q1).c, 0.05f, PLAYER_CORE, 7.0f * a);
            }
        }
        // Bomb: code injection. A sphere expanding from where it was launched;
        // its shell is drawn as slices (rings) toward the front cap, two dashed
        // sets with different counts turning against each other, so they moire.
        if (g->bombActive) {
            float R = g->bombRadius, t = g->bombTimer;
            Vector3 c0 = g->bombOrigin;
            for (int k = 0; k < 9; k++) {
                float d = R - 2.0f - (float)k * 5.0f;
                if (d < 0.0f) break;
                float rr = sqrtf(fmaxf(R * R - d * d, 0.0f));
                Frame f = frame_at(ct, m, o, (Vector3){ c0.x, c0.y, c0.z - d });
                float fade = 1.0f - (float)k / 9.0f;
                ring(&f, rr, 48, t * 1.5f + k * 0.3f, 0.08f, (k & 1) ? PLAYER_CORE : PLAYER_FIRE, 5.0f * fade, 0.45f);
                ring(&f, rr * 0.985f, 41, -t * 1.9f - k * 0.2f, 0.05f, mix_color(PLAYER_FIRE, gPal.secondary, 0.5f),
                     3.0f * fade, 0.5f);
            }
            if (R < 40.0f) {   // the side of the shell sweeping past the ship
                Frame f = frame_at(ct, m, o, c0);
                ring(&f, R, 48, t * 2.0f, 0.1f, PLAYER_CORE, 6.0f * (1.0f - R / 40.0f), 0.3f);
            }
        }
    }

    rlEnd();
    rlDrawRenderBatchActive();
    EndShaderMode();
}

// ---------------------------------------------------------------------------
// Story floor scroller

// The engine keeps the ship this far (world units) above the corridor floor
// (FLIGHT_CLEARANCE in core/flight.h); the arena's minY sits at that height.
#define FLOOR_CLEARANCE_WORLD 0.6f
#define STORY_DOTS_MAX 8000
#define STORY_DOT_RADIUS 0.075f   // arena units

void combat_draw_story(CombatDraw *d, CtTunnel *ct, const ArenaMap *m, const GameplaySystem *g,
                       const Story *story, const CtSceneView *view)
{
    if (!d->ready || view->reflection || story->scrollText < 0 || g->gameOver) return;
    static StoryDot dots[STORY_DOTS_MAX];
    const GameplayArena *ar = &g->arena;
    int n = story_floor_dots(story, &g->music, 0.42f * (ar->maxX - ar->minX), dots, STORY_DOTS_MAX);
    if (n <= 0) return;
    gCam = view->camera.position;
    set_role_palette(g);
    const double *o = view->origin;
    float floorY = ar->minY - FLOOR_CLEARANCE_WORLD / m->scale + 0.12f;
    float midX = 0.5f * (ar->minX + ar->maxX);
    Color voice = mix_color(gPal.primary, PLAYER_CORE, 0.82f);

    rlDrawRenderBatchActive();
    BeginShaderMode(d->shader);
    rlBegin(RL_QUADS);
    for (int i = 0; i < n; i++) {
        const StoryDot *s = &dots[i];
        if (s->gain <= 0.01f) continue;
        Frame f = frame_at(ct, m, o, (Vector3){ midX + s->x, floorY + s->lift, s->z });
        Vector3 toCam = Vector3Normalize(Vector3Subtract(gCam, f.c));
        Vector3 u = Vector3CrossProduct(f.N, toCam);
        if (Vector3Length(u) < 1e-4f) u = f.B;
        u = Vector3Scale(Vector3Normalize(u), STORY_DOT_RADIUS * f.k);
        Vector3 v = Vector3Scale(Vector3Normalize(Vector3CrossProduct(toCam, u)), STORY_DOT_RADIUS * f.k);
        // Primary to secondary along the line, lifted toward white midway; answers near-white.
        float w = 1.0f - fabsf(2.0f * s->u - 1.0f);
        Color c = s->answer ? voice : mix_color(mix_color(gPal.primary, gPal.secondary, s->u), PLAYER_CORE, 0.4f * w);
        col(c, s->gain);
        rlVertex3f(f.c.x - u.x - v.x, f.c.y - u.y - v.y, f.c.z - u.z - v.z);
        rlVertex3f(f.c.x + u.x - v.x, f.c.y + u.y - v.y, f.c.z + u.z - v.z);
        rlVertex3f(f.c.x + u.x + v.x, f.c.y + u.y + v.y, f.c.z + u.z + v.z);
        rlVertex3f(f.c.x - u.x + v.x, f.c.y - u.y + v.y, f.c.z - u.z + v.z);
        if ((i & 1023) == 1023) { rlEnd(); rlDrawRenderBatchActive(); rlBegin(RL_QUADS); }
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
    // The meters' unlit segments pulse on each bar of the music.
    float bar = music_flash(g, MUSIC_BAR, 5.0f);
    off = (Color){ (unsigned char)(40 + 50 * bar), (unsigned char)(60 + 60 * bar), (unsigned char)(75 + 70 * bar), 160 };
    Color energyColor = g->player.energy < 30.0f ? g->threatColor : g->primaryColor;

    int x = 24, y = h - 108, bw = 300;
    DrawText("CORE // ENERGY", x, y, 13, dim);
    DrawText(TextFormat("%03d", (int)g->player.energy), x + bw - 34, y, 15, energyColor);
    meter(x, y + 18, bw, 9, g->player.energy / 100.0f, 25, energyColor, off);
    y += 42;
    DrawText("CHAIN LINKS", x, y, 13, dim);
    DrawText(TextFormat("%03d", g->chainLinks), x + bw - 34, y, 15, g->secondaryColor);
    meter(x, y + 18, bw, 9, (float)g->chainLinks / MAX_CHAIN_LINKS, 20, g->secondaryColor, off);
    // Specials: beam and bomb cost the same; the pool holds two.
    int ready = g->chainLinks / BEAM_LINK_COST;
    const char *special = TextFormat("SPECIALS %d/2   K BEAM   L BOMB", ready);
    DrawText(special, x, y + 32, 11, ready > 0 ? g->secondaryColor : dim);
    // Pickup weapon: name and ammo (one slot, I to fire; the mirror works on rolls).
    if (g->weapon != WEAPON_NONE) {
        int full = GetWeaponAmmoFull(g->weapon);
        Color wc = g->weaponFlash > 0.0f ? PLAYER_CORE : PLAYER_FIRE;
        const char *label = g->weapon == WEAPON_MIRROR ? TextFormat("MIRROR // ROLL  x%d", g->ammo)
                                                       : TextFormat("I %s  x%d", GetWeaponName(g->weapon), g->ammo);
        DrawText(label, x, y - 72, 13, wc);
        meter(x, y - 54, bw, 7, (float)g->ammo / (float)(2 * full), 2 * full, wc, off);
    }

    const char *score = TextFormat("SCORE %08d", g->score);
    DrawText(score, w - MeasureText(score, 22) - 24, 48, 22, (Color){ 225, 240, 250, 230 });
    static const char *weapons[4] = { "PULSE", "ACCELERATOR", "MULTILOCK-6", "OVERDRIVE" };
    static const int next[3] = { 4, 8, 12 };
    const char *weapon = TextFormat("UPLINK  %s", weapons[g->weaponTier]);   // chain tier (not the pickup weapon)
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
    // Boss: name, core integrity and phase at the top (the arrival title card is story.c).
    const BossState *b = &g->boss;
    if (b->active && b->coreSlot >= 0) {
        const Enemy *core = &g->enemies[b->coreSlot];
        const char *name = story_boss_name(b->encounterIndex);
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
    } else if (b->defeatFlash > 0.0f) {
        unsigned char a = (unsigned char)(fminf(b->defeatFlash * 1.5f, 1.0f) * 230.0f);
        const char *t = "CORE COLLAPSED";
        DrawText(t, w / 2 - MeasureText(t, 44) / 2, h / 7, 44, (Color){ g->hotColor.r, g->hotColor.g, g->hotColor.b, a });
        const char *sub = "ENERGY RESTORED";
        DrawText(sub, w / 2 - MeasureText(sub, 16) / 2, h / 7 + 54, 16, (Color){ dim.r, dim.g, dim.b, a });
    }
    // Bomb wave passing the camera: a screen-space ripple from the centre.
    if (g->bombActive && g->bombTimer < 0.3f) {
        float k = g->bombTimer / 0.3f;
        float r = k * 0.75f * (float)w;
        DrawRing((Vector2){ w * 0.5f, h * 0.5f }, r, r + 6.0f + 20.0f * (1.0f - k), 0, 360, 72,
                 (Color){ PLAYER_CORE.r, PLAYER_CORE.g, PLAYER_CORE.b, (unsigned char)(200.0f * (1.0f - k)) });
    }
    if (g->stretchSteps > 0.0f) {
        const char *st = g->beamSteps > 0.0f ? "BEAM // HALF TIME" : "HALF TIME";
        DrawText(st, w / 2 - MeasureText(st, 14) / 2, 22, 14, PLAYER_FIRE);
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
