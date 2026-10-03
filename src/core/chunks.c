#include "chunks.h"
#include "path.h"
#include "env/env.h"
#include "rlgl.h"
#include <math.h>
#include <string.h>

int chunk_index_at(double s) { return (int)floor(s / CHUNK_LEN); }

void chunks_init(ChunkManager *cm)
{
    memset(cm, 0, sizeof(*cm));
    for (int i = 0; i < CHUNK_POOL; i++) cm->slots[i].index = -1;
}

static void release_slot(ChunkSlot *sl, Environment *env)
{
    if (sl->state == CHUNK_UPLOADED) {
        if (env && env->releaseChunk) env->releaseChunk(env, &sl->gpu);
        else chunk_release_default(&sl->gpu);
    }
    sl->state = CHUNK_EMPTY;
    sl->index = -1;
}

void chunks_release_all(ChunkManager *cm, Environment *env)
{
    for (int i = 0; i < CHUNK_POOL; i++) release_slot(&cm->slots[i], env);
    cm->visibleCount = 0;
}

void chunks_free(ChunkManager *cm, Environment *env)
{
    chunks_release_all(cm, env);
    for (int i = 0; i < CHUNK_POOL; i++)
        for (int b = 0; b < CHUNK_BATCHES; b++) lg_free(&cm->slots[i].cpu.geo[b]);
}

void chunk_cpu_begin(ChunkCpu *c, const PathSystem *path, int index)
{
    c->index = index;
    c->s0 = index * CHUNK_LEN;
    c->s1 = (index + 1) * CHUNK_LEN;
    c->origin = path_sample(path, c->s0).pos;
    for (int b = 0; b < CHUNK_BATCHES; b++) lg_clear(&c->geo[b]);
    memset(c->uniforms, 0, sizeof(c->uniforms));
    c->debrisCount = 0;
    c->archetype = 0;
}

void chunk_upload_default(ChunkCpu *in, ChunkGpu *out)
{
    out->index = in->index;
    out->origin = in->origin;
    out->s0 = in->s0;
    out->s1 = in->s1;
    out->meshCount = 0;
    out->vertexCount = 0;
    memcpy(out->uniforms, in->uniforms, sizeof(out->uniforms));
    memcpy(out->debris, in->debris, sizeof(out->debris));
    out->debrisCount = in->debrisCount;
    out->archetype = in->archetype;
    for (int b = 0; b < CHUNK_BATCHES; b++) {
        LineGeo *g = &in->geo[b];
        int nb = lg_batch_count(g);
        for (int k = 0; k < nb && out->meshCount < CHUNK_MAX_MESHES; k++) {
            Mesh probe = lg_batch_mesh(g, k);
            if (probe.vertexCount == 0 || probe.triangleCount == 0) continue;
            Mesh m = lg_upload_mesh(g, k, false);
            out->vertexCount += m.vertexCount;
            out->meshes[out->meshCount] = m;
            out->meshBatch[out->meshCount] = b;
            out->meshCount++;
        }
    }
}

void chunk_release_default(ChunkGpu *gpu)
{
    for (int i = 0; i < gpu->meshCount; i++) lg_unload_mesh(gpu->meshes[i]);
    gpu->meshCount = 0;
    gpu->vertexCount = 0;
}

static ChunkSlot *find_slot(ChunkManager *cm, int index)
{
    for (int i = 0; i < CHUNK_POOL; i++)
        if (cm->slots[i].state != CHUNK_EMPTY && cm->slots[i].index == index) return &cm->slots[i];
    return NULL;
}

static ChunkSlot *free_slot(ChunkManager *cm)
{
    for (int i = 0; i < CHUNK_POOL; i++)
        if (cm->slots[i].state == CHUNK_EMPTY) return &cm->slots[i];
    return NULL;
}

void chunks_update(ChunkManager *cm, Environment *env, const PathSystem *path,
                   double sShip, double viewDist, double time, int buildBudget, int uploadBudget)
{
    int first = chunk_index_at(sShip - 24.0);
    int last = chunk_index_at(sShip + viewDist);
    if (first < 0) first = 0;
    if (last - first + 1 > CHUNK_POOL) last = first + CHUNK_POOL - 1;
    cm->first = first;
    cm->last = last;
    cm->stats.builtThisFrame = 0;
    cm->stats.uploadedThisFrame = 0;

    for (int i = 0; i < CHUNK_POOL; i++) {
        ChunkSlot *sl = &cm->slots[i];
        if (sl->state != CHUNK_EMPTY && (sl->index < first || sl->index > last)) release_slot(sl, env);
    }

    int built = 0, uploaded = 0;
    bool prewarm = buildBudget <= 0;
    double tStream = GetTime();
    // Nearest-first: the window is ordered by distance ahead of the ship.
    for (int k = first; k <= last; k++) {
        ChunkSlot *sl = find_slot(cm, k);
        if (!sl) {
            if (!prewarm && built >= buildBudget) continue;
            sl = free_slot(cm);
            if (!sl) break;
            double t0 = GetTime();
            chunk_cpu_begin(&sl->cpu, path, k);
            env->buildChunk(env, path, k, &sl->cpu);
            double ms = (GetTime() - t0) * 1000.0;
            cm->stats.lastBuildMs = ms;
            if (ms > cm->stats.maxBuildMs) cm->stats.maxBuildMs = ms;
            sl->index = k;
            sl->state = CHUNK_BUILT;
            built++;
        }
        if (sl->state == CHUNK_BUILT) {
            if (!prewarm && uploaded >= uploadBudget) continue;
            double t0 = GetTime();
            if (env->uploadChunk) env->uploadChunk(env, &sl->cpu, &sl->gpu);
            else chunk_upload_default(&sl->cpu, &sl->gpu);
            double ms = (GetTime() - t0) * 1000.0;
            cm->stats.lastUploadMs = ms;
            if (ms > cm->stats.maxUploadMs) cm->stats.maxUploadMs = ms;
            sl->gpu.fadeStart = prewarm ? -1e9 : time;
            sl->state = CHUNK_UPLOADED;
            uploaded++;
        }
    }
    cm->stats.builtThisFrame = built;
    cm->stats.uploadedThisFrame = uploaded;
    if (!prewarm) {
        cm->stats.frameMs = (GetTime() - tStream) * 1000.0;
        if (cm->stats.frameMs > cm->stats.maxFrameMs) cm->stats.maxFrameMs = cm->stats.frameMs;
    }

    // Visible list, far -> near (helps nothing for additive, but deterministic order).
    cm->visibleCount = 0;
    int meshes = 0, verts = 0;
    for (int k = last; k >= first; k--) {
        ChunkSlot *sl = find_slot(cm, k);
        if (sl && sl->state == CHUNK_UPLOADED) {
            cm->visible[cm->visibleCount++] = &sl->gpu;
            meshes += sl->gpu.meshCount;
            verts += sl->gpu.vertexCount;
        }
    }
    cm->stats.active = cm->visibleCount;
    cm->stats.gpuMeshes = meshes;
    cm->stats.vertexCount = verts;
}

const ChunkGpu *const *chunks_visible(const ChunkManager *cm, int *count)
{
    *count = cm->visibleCount;
    return cm->visible;
}
