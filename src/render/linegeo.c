#include "linegeo.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>

void lg_free(LineGeo *g)
{
    free(g->pos); free(g->uv); free(g->uv2); free(g->nrm); free(g->tan); free(g->col); free(g->idx);
    memset(g, 0, sizeof(*g));
}

void lg_clear(LineGeo *g)
{
    g->vcount = g->icount = 0;
    g->batchCount = 0;
    g->batchV[0] = g->batchI[0] = 0;
}

static void *grow(void *p, int elemSize, int newCap) { return realloc(p, (size_t)elemSize * (size_t)newCap); }

static void ensure_vcap(LineGeo *g, int need)
{
    if (need <= g->vcap) return;
    int cap = g->vcap ? g->vcap : 1024;
    while (cap < need) cap *= 2;
    g->pos = grow(g->pos, sizeof(float) * 3, cap);
    g->uv  = grow(g->uv,  sizeof(float) * 2, cap);
    g->uv2 = grow(g->uv2, sizeof(float) * 2, cap);
    g->nrm = grow(g->nrm, sizeof(float) * 3, cap);
    g->tan = grow(g->tan, sizeof(float) * 4, cap);
    g->col = grow(g->col, 4, cap);
    g->vcap = cap;
}

static void ensure_icap(LineGeo *g, int need)
{
    if (need <= g->icap) return;
    int cap = g->icap ? g->icap : 2048;
    while (cap < need) cap *= 2;
    g->idx = grow(g->idx, sizeof(unsigned short), cap);
    g->icap = cap;
}

int lg_reserve(LineGeo *g, int nverts, int nidx)
{
    if (g->batchCount == 0) {
        g->batchCount = 1;
        g->batchV[0] = g->vcount;
        g->batchI[0] = g->icount;
    }
    int local = g->vcount - g->batchV[g->batchCount - 1];
    if (local + nverts > LG_MAX_BATCH_VERTS) {
        if (g->batchCount >= LG_MAX_BATCHES) {
            fprintf(stderr, "linegeo: batch limit reached\n");
        } else {
            g->batchV[g->batchCount] = g->vcount;
            g->batchI[g->batchCount] = g->icount;
            g->batchCount++;
            local = 0;
        }
    }
    ensure_vcap(g, g->vcount + nverts);
    ensure_icap(g, g->icount + nidx);
    return local;
}

void lg_vertex(LineGeo *g, Vector3 p, Vector2 uv, Vector2 uv2, Vector3 n, Vector4 t, Color c)
{
    int i = g->vcount++;
    g->pos[i * 3 + 0] = p.x; g->pos[i * 3 + 1] = p.y; g->pos[i * 3 + 2] = p.z;
    g->uv[i * 2 + 0] = uv.x; g->uv[i * 2 + 1] = uv.y;
    g->uv2[i * 2 + 0] = uv2.x; g->uv2[i * 2 + 1] = uv2.y;
    g->nrm[i * 3 + 0] = n.x; g->nrm[i * 3 + 1] = n.y; g->nrm[i * 3 + 2] = n.z;
    g->tan[i * 4 + 0] = t.x; g->tan[i * 4 + 1] = t.y; g->tan[i * 4 + 2] = t.z; g->tan[i * 4 + 3] = t.w;
    g->col[i * 4 + 0] = c.r; g->col[i * 4 + 1] = c.g; g->col[i * 4 + 2] = c.b; g->col[i * 4 + 3] = c.a;
}

void lg_index(LineGeo *g, int i) { g->idx[g->icount++] = (unsigned short)i; }

void lg_quad(LineGeo *g, int base, int a, int b, int c, int d)
{
    lg_index(g, base + a); lg_index(g, base + b); lg_index(g, base + c);
    lg_index(g, base + a); lg_index(g, base + c); lg_index(g, base + d);
}

static Vector3 v3add(Vector3 a, Vector3 b, float s) { Vector3 r = { a.x + b.x * s, a.y + b.y * s, a.z + b.z * s }; return r; }

void lg_planar_polyline(LineGeo *g, const Vector3 *pts, const Vector3 *across, int n, float halfWidth, LineAttr a)
{
    if (n < 2) return;
    int base = lg_reserve(g, n * 2, (n - 1) * 6);
    float u = 0.0f;
    Vector2 uv2 = { a.group, a.anim };
    Vector4 t = { halfWidth, a.intensity, a.hash, a.param };
    for (int i = 0; i < n; i++) {
        if (i > 0) {
            float dx = pts[i].x - pts[i - 1].x, dy = pts[i].y - pts[i - 1].y, dz = pts[i].z - pts[i - 1].z;
            u += sqrtf(dx * dx + dy * dy + dz * dz);
        }
        lg_vertex(g, v3add(pts[i], across[i], -halfWidth), (Vector2){ u, -1.0f }, uv2, across[i], t, a.color);
        lg_vertex(g, v3add(pts[i], across[i], halfWidth), (Vector2){ u, 1.0f }, uv2, across[i], t, a.color);
    }
    for (int i = 0; i < n - 1; i++) lg_quad(g, base, i * 2, i * 2 + 1, i * 2 + 3, i * 2 + 2);
}

#define LG_ARC_MAX_PTS 256

void lg_planar_arc(LineGeo *g, float r, float a0, float a1, float z0, float halfWidth, float maxStepRad, LineAttr a)
{
    Vector3 pts[LG_ARC_MAX_PTS], acr[LG_ARC_MAX_PTS];
    int steps = (int)ceilf(fabsf(a1 - a0) / maxStepRad);
    if (steps < 1) steps = 1;
    if (steps > LG_ARC_MAX_PTS - 1) steps = LG_ARC_MAX_PTS - 1;
    for (int i = 0; i <= steps; i++) {
        float ang = a0 + (a1 - a0) * (float)i / (float)steps;
        float c = cosf(ang), s = sinf(ang);
        pts[i] = (Vector3){ c * r, s * r, z0 };
        acr[i] = (Vector3){ c, s, 0.0f };
    }
    lg_planar_polyline(g, pts, acr, steps + 1, halfWidth, a);
}

void lg_planar_radial(LineGeo *g, float r0, float r1, float ang, float z0, float halfWidth, LineAttr a)
{
    float c = cosf(ang), s = sinf(ang);
    Vector3 pts[2] = { { c * r0, s * r0, z0 }, { c * r1, s * r1, z0 } };
    Vector3 tang = { -s, c, 0.0f };
    Vector3 acr[2] = { tang, tang };
    lg_planar_polyline(g, pts, acr, 2, halfWidth, a);
}

void lg_facing_segment(LineGeo *g, Vector3 a, Vector3 b, float wa, float wb, float ia, float ib, LineAttr at)
{
    int base = lg_reserve(g, 4, 6);
    Vector2 uv2 = { at.group, at.anim };
    lg_vertex(g, a, (Vector2){ 0.0f, -1.0f }, uv2, b, (Vector4){ wa, ia, at.hash, 0.0f }, at.color);
    lg_vertex(g, a, (Vector2){ 0.0f,  1.0f }, uv2, b, (Vector4){ wa, ia, at.hash, 0.0f }, at.color);
    lg_vertex(g, b, (Vector2){ 1.0f,  1.0f }, uv2, a, (Vector4){ wb, ib, at.hash, 1.0f }, at.color);
    lg_vertex(g, b, (Vector2){ 1.0f, -1.0f }, uv2, a, (Vector4){ wb, ib, at.hash, 1.0f }, at.color);
    lg_quad(g, base, 0, 1, 2, 3);
}

void lg_fill_quad(LineGeo *g, Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, Vector3 n, LineAttr a)
{
    int base = lg_reserve(g, 4, 6);
    Vector2 uv2 = { a.group, a.anim };
    Vector4 t = { 0.0f, a.intensity, a.hash, a.param };
    lg_vertex(g, p0, (Vector2){ 0, 0 }, uv2, n, t, a.color);
    lg_vertex(g, p1, (Vector2){ 1, 0 }, uv2, n, t, a.color);
    lg_vertex(g, p2, (Vector2){ 1, 1 }, uv2, n, t, a.color);
    lg_vertex(g, p3, (Vector2){ 0, 1 }, uv2, n, t, a.color);
    lg_quad(g, base, 0, 1, 2, 3);
}

void lg_fill_tri(LineGeo *g, Vector3 p0, Vector3 p1, Vector3 p2, Vector3 n, LineAttr a)
{
    int base = lg_reserve(g, 3, 3);
    Vector2 uv2 = { a.group, a.anim };
    Vector4 t = { 0.0f, a.intensity, a.hash, a.param };
    lg_vertex(g, p0, (Vector2){ 0, 0 }, uv2, n, t, a.color);
    lg_vertex(g, p1, (Vector2){ 1, 0 }, uv2, n, t, a.color);
    lg_vertex(g, p2, (Vector2){ 0.5f, 1 }, uv2, n, t, a.color);
    lg_index(g, base); lg_index(g, base + 1); lg_index(g, base + 2);
}

int lg_batch_count(const LineGeo *g) { return g->vcount > 0 ? g->batchCount : 0; }

Mesh lg_batch_mesh(const LineGeo *g, int b)
{
    Mesh m;
    memset(&m, 0, sizeof(m));
    int v0 = g->batchV[b], v1 = (b + 1 < g->batchCount) ? g->batchV[b + 1] : g->vcount;
    int i0 = g->batchI[b], i1 = (b + 1 < g->batchCount) ? g->batchI[b + 1] : g->icount;
    m.vertexCount = v1 - v0;
    m.triangleCount = (i1 - i0) / 3;
    m.vertices = g->pos + v0 * 3;
    m.texcoords = g->uv + v0 * 2;
    m.texcoords2 = g->uv2 + v0 * 2;
    m.normals = g->nrm + v0 * 3;
    m.tangents = g->tan + v0 * 4;
    m.colors = g->col + v0 * 4;
    m.indices = g->idx + i0;
    return m;
}

static uint64_t fnv(uint64_t h, const void *data, size_t n)
{
    const unsigned char *p = (const unsigned char *)data;
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 0x100000001B3ull; }
    return h;
}

uint64_t lg_hash(const LineGeo *g)
{
    uint64_t h = 0xCBF29CE484222325ull;
    h = fnv(h, &g->vcount, sizeof(int));
    h = fnv(h, &g->icount, sizeof(int));
    if (g->vcount) {
        h = fnv(h, g->pos, sizeof(float) * 3 * (size_t)g->vcount);
        h = fnv(h, g->uv, sizeof(float) * 2 * (size_t)g->vcount);
        h = fnv(h, g->uv2, sizeof(float) * 2 * (size_t)g->vcount);
        h = fnv(h, g->nrm, sizeof(float) * 3 * (size_t)g->vcount);
        h = fnv(h, g->tan, sizeof(float) * 4 * (size_t)g->vcount);
        h = fnv(h, g->col, 4 * (size_t)g->vcount);
    }
    if (g->icount) h = fnv(h, g->idx, sizeof(unsigned short) * (size_t)g->icount);
    return h;
}

// DrawMesh only checks indices != NULL to choose an indexed draw (the EBO lives in the VAO).
static unsigned short g_indexSentinel;

Mesh lg_upload_mesh(const LineGeo *g, int b, bool dynamic)
{
    Mesh m = lg_batch_mesh(g, b);
    UploadMesh(&m, dynamic);
    m.vertices = NULL; m.texcoords = NULL; m.texcoords2 = NULL; m.normals = NULL;
    m.tangents = NULL; m.colors = NULL;
    m.indices = &g_indexSentinel;
    return m;
}

void lg_unload_mesh(Mesh m)
{
    m.indices = NULL;
    UnloadMesh(m);
}
