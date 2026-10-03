#include "city_decor.h"
#include "core/rng.h"
#include "trace_route.h"
#include <math.h>
#include <string.h>

enum { F_TRACES = 2 };      // sub-stream of the tower hash (towers use F_TOWERS = 1)

static const Color COL_TRACE  = { 0xA8, 0x8C, 0xFF, 255 };   // lavender
static const Color COL_TRACE2 = { 0x8A, 0x6C, 0xFF, 255 };   // violet
static const Color COL_VIA    = { 0xE0, 0xD8, 0xFF, 255 };

typedef TraceFace Face;      // tower faces: v is world up

static Vector3 face_point(const Face *f, double u, double v, double off, DVec3 origin)
{
    DVec3 p = dv_add(dv_add(f->o, dv_scale(f->u, u)), dv_add(dv_scale(f->v, v), dv_scale(f->n, off)));
    return dv_to_v3(dv_sub(p, origin));
}

static void glyph_quad(LineGeo *g, const Face *f, double u0, double v0, double w, double h, float hash, int kind,
                       float intensity, float scroll, float off, DVec3 origin)
{
    int b = lg_reserve(g, 4, 6);
    Vector3 n = dv_to_v3(f->n);
    Vector4 t = { (float)w, (float)h, intensity, scroll };
    Vector2 uv2 = { hash, (float)kind };
    const double cu[4] = { 0, w, w, 0 }, cv[4] = { 0, 0, h, h };
    for (int i = 0; i < 4; i++)
        lg_vertex(g, face_point(f, u0 + cu[i], v0 + cv[i], off, origin), (Vector2){ (float)cu[i], (float)cv[i] }, uv2, n, t, WHITE);
    lg_quad(g, b, 0, 1, 2, 3);
}

static void decorate_face(const CityDecorParams *p, const Face *f, int front, int lane, float lod, float glyphMul, Pcg32 *r,
                          DVec3 origin, DVec3 towardCamera, LineGeo *traces, LineGeo *glyphs)
{
    static TraceGrid g;
    double pitch = p->tracePitch;
    if (!trace_grid_init(&g, f, p->tracePitch)) return;

    if (lane) {
        float roll = pcg32_float(r);
        if (roll < p->glyphStripChance * glyphMul) {
            // Narrow tall strip by the edge nearer the approaching camera.
            double w = 2.0 * pitch;
            int nearEnd = dv_dot(f->u, towardCamera) > 0.0;
            double u0 = nearEnd ? f->w - 0.45 - w : 0.45;
            double v0 = f->h * pcg32_range(r, 0.08f, 0.3f), v1 = f->h * pcg32_range(r, 0.6f, 0.95f);
            glyph_quad(glyphs, f, u0, v0, w, v1 - v0, pcg32_float(r), 0, p->glyphIntensity, p->glyphScroll, (float)p->faceOffset, origin);
            int c0 = (int)floor((u0 - g.u0) / pitch), c1 = (int)ceil((u0 + w - g.u0) / pitch);
            trace_grid_block(&g, c0, c1, (int)(v0 / pitch), (int)(v1 / pitch));
        } else if (roll < (p->glyphStripChance + p->glyphBlockChance) * glyphMul) {
            double w = f->w * pcg32_range(r, 0.5f, 0.85f), h = fmin(f->h * pcg32_range(r, 0.15f, 0.4f), 18.0);
            double u0 = (f->w - w) * pcg32_float(r), v0 = (f->h - h) * pcg32_range(r, 0.1f, 0.7f);
            glyph_quad(glyphs, f, u0, v0, w, h, pcg32_float(r), 1, p->glyphIntensity, p->glyphScroll, (float)p->faceOffset, origin);
            int c0 = (int)floor((u0 - g.u0) / pitch), c1 = (int)ceil((u0 + w - g.u0) / pitch);
            trace_grid_block(&g, c0, c1, (int)(v0 / pitch), (int)((v0 + h) / pitch));
        }
    }

    // Buses: dense on front faces, sparse on lane faces; fewer with distance (LOD).
    TraceRouteParams tr = { p->tracePitch, p->traceWidth, p->traceIntensity, p->traceStop, p->tracePulse, p->viaRadius,
                            p->faceOffset, 0.7f, 0.3f, COL_TRACE, COL_TRACE2, COL_VIA };
    trace_route_buses(&tr, f, &g, (front ? 1.0f : 0.45f) * lod * p->traceBuses, r, origin, traces);
}

void city_decor_build(const CityDecorParams *p, const TowerDesc *towers, int n, const PathSystem *path,
                      DVec3 origin, LineGeo *traces, LineGeo *glyphs)
{
    for (int ti = 0; ti < n; ti++) {
        const TowerDesc *t = &towers[ti];
        Pcg32 r = pcg32_seed(ct_hash64_2(t->hash, F_TRACES));
        PathSample ps = path_sample(path, t->sOwner);
        DVec3 centre = dv3((t->x0[0] + t->x1[0]) * 0.5, 0.0, (t->z0[0] + t->z1[0]) * 0.5);
        DVec3 toPath = dv_sub(ps.pos, centre);
        toPath.y = 0.0;
        toPath = dv_norm(toPath);
        DVec3 toCam = dv_scale(ps.T, -1.0);   // the camera approaches from behind along the path
        float far = (t->dist - p->inner) / fmaxf(p->bandMax - p->inner, 1.0f);
        far = far < 0.0f ? 0.0f : (far > 1.0f ? 1.0f : far);
        float lod = (1.0f + (p->traceFar - 1.0f) * far) * t->traceMul;
        for (int k = 0; k < t->tiers; k++) {
            double x0 = t->x0[k], x1 = t->x1[k], z0 = t->z0[k], z1 = t->z1[k];
            double yb = k == 0 ? t->baseY : t->yTop[k - 1];
            double h = t->yTop[k] - yb;
            Face faces[4] = {
                { dv3(x0, yb, z0), dv3(1, 0, 0), dv3(0, 1, 0), dv3(0, 0, -1), x1 - x0, h },
                { dv3(x1, yb, z1), dv3(-1, 0, 0), dv3(0, 1, 0), dv3(0, 0, 1), x1 - x0, h },
                { dv3(x0, yb, z1), dv3(0, 0, -1), dv3(0, 1, 0), dv3(-1, 0, 0), z1 - z0, h },
                { dv3(x1, yb, z0), dv3(0, 0, 1), dv3(0, 1, 0), dv3(1, 0, 0), z1 - z0, h },
            };
            for (int fi = 0; fi < 4; fi++) {
                int front = dv_dot(faces[fi].n, toCam) > 0.4;
                int lane = dv_dot(faces[fi].n, toPath) > 0.4;
                if (!front && !lane) continue;
                decorate_face(p, &faces[fi], front, lane && k == 0, lod, t->glyphMul, &r, origin, toCam, traces, glyphs);
            }
        }
    }
}
