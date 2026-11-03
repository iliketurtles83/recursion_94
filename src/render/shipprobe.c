// The original RECURSION_94 probe. Source: DrawPlayerShip in the old game
// (git show 1a7c6ca:legacy/src/gameplay.c). Coordinates below are the old
// game's: player at the origin, -z forward, arena units. P() maps them to ship
// space. Dropped from the original: the bank-driven wing lift (the engine banks
// the whole ship), the weapon-tier pods, the invulnerability flash and the small
// hardware (cables, louvers, fasteners, intake fins), which is clutter at this
// size. The original took its trim colours from the seed palette; the engine
// has none, so the trim is the dart's cyan with a violet second tone.
#include "shipprobe.h"
#include "raymath.h"
#include <math.h>

#define S 0.4f   // old game arena unit in world units

static Vector3 P(float x, float y, float z) { return (Vector3){ x * S, y * S, -z * S }; }

static Color mix(Color a, Color b, float t)
{
    return (Color){ (unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t),
                    (unsigned char)(a.b + (b.b - a.b) * t), 255 };
}

static Color scale(Color c, float k)
{
    return (Color){ (unsigned char)fminf(c.r * k, 255.0f), (unsigned char)fminf(c.g * k, 255.0f),
                    (unsigned char)fminf(c.b * k, 255.0f), 255 };
}

static const Color PRIMARY = { 0x4F, 0xEA, 0xF5, 255 };
static const Color SECONDARY = { 0x8A, 0x7C, 0xFF, 255 };
static const Color HOT = { 0xE8, 0xFB, 0xFF, 255 };

// ---------------------------------------------------------------------------
// Faces (ship space). Culling is off and the ship shader faces normals to the
// camera, so winding does not matter.

static void tri(LineGeo *g, Vector3 a, Vector3 b, Vector3 c, Color col)
{
    Vector3 n = Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a));
    if (Vector3Length(n) < 1e-9f) return;
    lg_fill_tri(g, a, b, c, Vector3Normalize(n), (LineAttr){ .color = col, .intensity = 1.0f });
}

static void quad(LineGeo *g, Vector3 a, Vector3 b, Vector3 c, Vector3 d, Color col)
{
    tri(g, a, b, c, col);
    tri(g, a, c, d, col);
}

// Triangle slab, thickness along y (old units).
static void thick_tri(LineGeo *g, Vector3 a, Vector3 b, Vector3 c, float thickness, Color top, Color edge)
{
    Vector3 h = { 0.0f, thickness * 0.5f * S, 0.0f };
    Vector3 at = Vector3Add(a, h), bt = Vector3Add(b, h), ct = Vector3Add(c, h);
    Vector3 ab = Vector3Subtract(a, h), bb = Vector3Subtract(b, h), cb = Vector3Subtract(c, h);
    tri(g, at, bt, ct, top);
    tri(g, ab, cb, bb, edge);
    quad(g, at, ab, bb, bt, edge);
    quad(g, bt, bb, cb, ct, edge);
    quad(g, ct, cb, ab, at, edge);
}

// Frustum from a (radius ra) to b (radius rb) with end caps; radii in old units.
static void cylinder(LineGeo *g, Vector3 a, Vector3 b, float ra, float rb, int sides, Color col)
{
    Vector3 axis = Vector3Subtract(b, a);
    if (Vector3Length(axis) < 1e-6f) return;
    axis = Vector3Normalize(axis);
    Vector3 ref = fabsf(axis.y) < 0.9f ? (Vector3){ 0, 1, 0 } : (Vector3){ 1, 0, 0 };
    Vector3 u = Vector3Normalize(Vector3CrossProduct(axis, ref)), v = Vector3CrossProduct(axis, u);
    ra *= S; rb *= S;
    for (int i = 0; i < sides; i++) {
        float t0 = (float)i * 2.0f * PI / (float)sides, t1 = (float)(i + 1) * 2.0f * PI / (float)sides;
        Vector3 r0 = Vector3Add(Vector3Scale(u, cosf(t0)), Vector3Scale(v, sinf(t0)));
        Vector3 r1 = Vector3Add(Vector3Scale(u, cosf(t1)), Vector3Scale(v, sinf(t1)));
        Vector3 a0 = Vector3Add(a, Vector3Scale(r0, ra)), a1 = Vector3Add(a, Vector3Scale(r1, ra));
        Vector3 b0 = Vector3Add(b, Vector3Scale(r0, rb)), b1 = Vector3Add(b, Vector3Scale(r1, rb));
        quad(g, a0, a1, b1, b0, col);
        tri(g, a, a1, a0, col);
        tri(g, b, b0, b1, col);
    }
}

// Ellipsoid with semi-axes r (ship space).
static void ellipsoid(LineGeo *g, Vector3 c, Vector3 r, int rings, int slices, Color col)
{
    for (int i = 0; i < rings; i++) {
        float p0 = PI * ((float)i / rings - 0.5f), p1 = PI * ((float)(i + 1) / rings - 0.5f);
        for (int j = 0; j < slices; j++) {
            float q0 = 2.0f * PI * (float)j / slices, q1 = 2.0f * PI * (float)(j + 1) / slices;
            Vector3 v[4];
            float ps[4] = { p0, p0, p1, p1 }, qs[4] = { q0, q1, q1, q0 };
            for (int k = 0; k < 4; k++)
                v[k] = (Vector3){ c.x + r.x * cosf(ps[k]) * cosf(qs[k]), c.y + r.y * sinf(ps[k]),
                                  c.z + r.z * cosf(ps[k]) * sinf(qs[k]) };
            quad(g, v[0], v[1], v[2], v[3], col);
        }
    }
}

// Octagonal raised housing (DrawMachineryHousing); centre and size in old units.
static void housing(LineGeo *g, float px, float py, float pz, float sx, float sy, float sz, Color face, Color edge)
{
    static const Vector2 outline[8] = { { -1, -0.7f }, { -0.7f, -1 }, { 0.7f, -1 }, { 1, -0.7f },
                                        { 1, 0.7f }, { 0.7f, 1 }, { -0.7f, 1 }, { -1, 0.7f } };
    Vector3 crown = P(px, py + sy * 0.5f, pz);
    for (int i = 0; i < 8; i++) {
        int j = (i + 1) & 7;
        float ax = outline[i].x * sx * 0.5f, az = outline[i].y * sz * 0.5f;
        float bx = outline[j].x * sx * 0.5f, bz = outline[j].y * sz * 0.5f;
        Vector3 a = P(px + ax, py + sy * 0.25f, pz + az), b = P(px + bx, py + sy * 0.25f, pz + bz);
        Vector3 at = P(px + ax * 0.9f, py + sy * 0.5f, pz + az * 0.9f), bt = P(px + bx * 0.9f, py + sy * 0.5f, pz + bz * 0.9f);
        Vector3 ab = P(px + ax, py - sy * 0.5f, pz + az), bb = P(px + bx, py - sy * 0.5f, pz + bz);
        tri(g, crown, bt, at, face);
        quad(g, at, bt, b, a, scale(face, 0.72f));
        quad(g, a, b, bb, ab, edge);
    }
}

void shipprobe_hull(LineGeo *g)
{
    lg_clear(g);
    Color body = scale(mix((Color){ 118, 137, 156, 255 }, PRIMARY, 0.12f), 0.6f);
    Color hullDark = mix(PRIMARY, (Color){ 6, 11, 25, 255 }, 0.86f);
    Color hullEdge = mix((Color){ 40, 53, 69, 255 }, PRIMARY, 0.14f);
    Color canopy = mix((Color){ 4, 13, 26, 255 }, PRIMARY, 0.06f);
    Color panel = mix(PRIMARY, (Color){ 31, 38, 47, 255 }, 0.80f);

    // Faceted fuselage: fine nose, wide shoulders, tapered stern.
    const float hullZ[4] = { -1.82f, -0.58f, 0.30f, 0.83f };
    const float hullW[4] = { 0.025f, 0.30f, 0.40f, 0.22f };
    const float hullH[4] = { 0.02f, 0.25f, 0.23f, 0.14f };
    Vector3 hull[4][8];
    for (int r = 0; r < 4; r++)
        for (int s = 0; s < 8; s++) {
            float a = (float)s * PI / 4.0f;
            hull[r][s] = P(cosf(a) * hullW[r], sinf(a) * hullH[r], hullZ[r]);
        }
    for (int r = 0; r < 3; r++)
        for (int s = 0; s < 8; s++)
            quad(g, hull[r][s], hull[r][(s + 1) & 7], hull[r + 1][(s + 1) & 7], hull[r + 1][s], s < 4 ? body : hullDark);
    for (int s = 0; s < 8; s++) tri(g, P(0, 0, 0.83f), hull[3][s], hull[3][(s + 1) & 7], hullDark);

    // Main wings, inset panels and the dorsal plate.
    Vector3 nose = P(0, -0.01f, -1.52f);
    Vector3 leftTip = P(-1.86f, 0, 0.48f), rightTip = P(1.86f, 0, 0.48f);
    Vector3 leftRoot = P(-0.24f, 0, 0.38f), rightRoot = P(0.24f, 0, 0.38f);
    thick_tri(g, nose, leftTip, leftRoot, 0.13f, body, hullDark);
    thick_tri(g, nose, rightRoot, rightTip, 0.13f, body, hullDark);
    tri(g, P(-0.18f, 0.075f, -0.70f), P(-1.43f, 0.075f, 0.34f), P(-0.37f, 0.075f, 0.23f), hullEdge);
    tri(g, P(0.18f, 0.075f, -0.70f), P(0.37f, 0.075f, 0.23f), P(1.43f, 0.075f, 0.34f), hullEdge);
    thick_tri(g, P(0, 0.08f, -1.48f), P(-0.34f, 0.10f, 0.37f), P(0.34f, 0.10f, 0.37f), 0.075f, scale(body, 0.78f), hullDark);

    // Smoked-glass canopy.
    ellipsoid(g, P(0, 0.24f, -0.45f), (Vector3){ 0.31f * 0.72f * S, 0.31f * 0.55f * S, 0.31f * 1.6f * S }, 6, 12, canopy);

    // Armour panels and canards.
    quad(g, P(-0.30f, 0.135f, -0.94f), P(-0.40f, 0.14f, 0.18f), P(-0.18f, 0.15f, 0.28f), P(-0.10f, 0.145f, -1.06f), hullEdge);
    quad(g, P(0.10f, 0.145f, -1.06f), P(0.18f, 0.15f, 0.28f), P(0.40f, 0.14f, 0.18f), P(0.30f, 0.135f, -0.94f), hullEdge);
    for (int side = -1; side <= 1; side += 2) {
        float f = (float)side;
        thick_tri(g, P(f * 0.24f, 0, -1.16f), P(f * 0.88f, 0, -0.72f), P(f * 0.28f, 0, -0.57f), 0.075f, hullEdge, hullDark);
    }

    // Avionics rails, intake shoulders, trailing surfaces and intake housings.
    for (int side = -1; side <= 1; side += 2) {
        float f = (float)side;
        cylinder(g, P(f * 0.28f, 0.135f, -0.92f), P(f * 0.34f, 0.135f, 0.36f), 0.045f, 0.065f, 7, panel);
        cylinder(g, P(f * 0.67f, -0.025f, 0.44f), P(f * 0.61f, -0.015f, -0.38f), 0.17f, 0.10f, 10, panel);
        thick_tri(g, P(f * 0.62f, -0.01f, 0.42f), P(f * 1.48f, 0, 0.50f), P(f * 1.08f, 0, 0.82f), 0.055f, panel, hullDark);
        housing(g, f * 0.84f, 0.18f, 0.01f, 0.42f, 0.24f, 0.80f, hullEdge, hullDark);
        float mz = 0.01f - 0.403f;
        quad(g, P(f * 0.84f - 0.14f, 0.115f, mz), P(f * 0.84f - 0.14f, 0.225f, mz),
             P(f * 0.84f + 0.14f, 0.225f, mz), P(f * 0.84f + 0.14f, 0.115f, mz), (Color){ 5, 8, 12, 255 });
        housing(g, f * 1.27f, 0.12f, 0.29f, 0.29f, 0.075f, 0.25f, body, hullDark);
    }
    cylinder(g, P(0, -0.20f, 0.34f), P(0, -0.18f, -1.24f), 0.09f, 0.045f, 8, panel);

    // Fin and tail planes.
    thick_tri(g, P(0, 0.05f, 0.66f), P(0, 0.78f, 0.52f), P(0, 0.07f, -0.18f), 0.09f, body, hullDark);
    thick_tri(g, P(-0.70f, -0.02f, 0.62f), P(-1.20f, 0, 0.92f), P(-0.48f, 0, 0.18f), 0.10f, hullEdge, hullDark);
    thick_tri(g, P(0.70f, -0.02f, 0.62f), P(0.48f, 0, 0.18f), P(1.20f, 0, 0.92f), 0.10f, hullEdge, hullDark);

    // Twin engines with cooling collars.
    for (int e = -1; e <= 1; e += 2) {
        float x = (float)e * 0.52f;
        cylinder(g, P(x, -0.035f, 0.82f), P(x - (float)e * 0.06f, 0, -0.30f), 0.22f, 0.12f, 12, body);
        cylinder(g, P(x, -0.035f, 0.83f), P(x, -0.035f, 0.92f), 0.19f, 0.16f, 12, hullDark);
        for (int c = 0; c < 3; c++) {
            float z = 0.50f + (float)c * 0.11f;
            cylinder(g, P(x, -0.035f, z), P(x, -0.035f, z + 0.023f), 0.207f, 0.210f, 12, panel);
        }
    }
}

// ---------------------------------------------------------------------------
// Emissive trim (camera-facing segments, line shader).

static void line(LineGeo *g, Vector3 a, Vector3 b, Color c, float intensity)
{
    LineAttr at = { c, intensity, 0.0f, 0.0f, -1.0f, 0.0f };
    lg_facing_segment(g, a, b, 0.010f, 0.010f, intensity, intensity, at);
}

// Point light: a short fat segment along z.
static void light(LineGeo *g, Vector3 p, float halfWidth, Color c, float intensity)
{
    LineAttr at = { c, intensity, 0.0f, 0.0f, -1.0f, 0.0f };
    Vector3 b = { p.x, p.y, p.z - 0.6f * halfWidth };
    lg_facing_segment(g, p, b, halfWidth, halfWidth, intensity, intensity, at);
}

void shipprobe_lights(LineGeo *g, const Vector3 engines[2], float engineIntensity)
{
    lg_clear(g);
    Vector3 nose = P(0, -0.01f, -1.52f);
    Vector3 leftTip = P(-1.86f, 0, 0.48f), rightTip = P(1.86f, 0, 0.48f);
    Vector3 leftRoot = P(-0.24f, 0, 0.38f), rightRoot = P(0.24f, 0, 0.38f);
    line(g, nose, leftTip, PRIMARY, 2.5f);
    line(g, nose, rightTip, PRIMARY, 2.5f);
    line(g, leftTip, leftRoot, SECONDARY, 2.0f);
    line(g, rightTip, rightRoot, SECONDARY, 2.0f);
    line(g, P(0, 0.415f, -0.42f), P(0, 0.25f, -0.94f), PRIMARY, 1.4f);
    light(g, leftTip, 0.075f * S, HOT, 4.0f);
    light(g, rightTip, 0.075f * S, HOT, 4.0f);
    light(g, P(0, 0, -1.73f), 0.075f * S, PRIMARY, 3.0f);
    for (int e = -1; e <= 1; e += 2) {
        float x = (float)e * 0.52f;
        // Nozzle rim.
        for (int k = 0; k < 16; k++) {
            float a0 = (float)k * 2.0f * PI / 16.0f, a1 = (float)(k + 1) * 2.0f * PI / 16.0f;
            line(g, P(x + 0.235f * cosf(a0), -0.035f + 0.235f * sinf(a0), 0.82f),
                    P(x + 0.235f * cosf(a1), -0.035f + 0.235f * sinf(a1), 0.82f), PRIMARY, 2.0f);
        }
        light(g, P(x + (float)e * 0.16f, -0.05f, 0.48f), 0.055f * S, SECONDARY, 2.5f);
    }
    LineAttr ga = { HOT, engineIntensity, 0.0f, 0.0f, -1.0f, 0.0f };
    for (int i = 0; i < 2; i++) {
        Vector3 a = engines[i], b = { a.x, a.y, a.z - 0.08f };
        lg_facing_segment(g, a, b, SHIPPROBE_GLOW_HALF, SHIPPROBE_GLOW_HALF, engineIntensity, engineIntensity, ga);
    }
}
