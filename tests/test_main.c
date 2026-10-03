// Determinism and continuity tests. Runs without a window or GL context.
#include "core/rng.h"
#include "core/noise.h"
#include "core/path.h"
#include "core/chunks.h"
#include "core/params.h"
#include "core/flight.h"
#include "env/env.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int g_fail = 0, g_pass = 0;

#define CHECK(cond, ...) do { if (cond) g_pass++; else { g_fail++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static PathStyle default_style(void)
{
    PathStyle s = { 12.0f, 0.12f, 60.0f, 14.0f, 20.0f, 0.15f, 0.015f, true };
    return s;
}

static void test_rng(void)
{
    Pcg32 a = pcg32_seed(1234), b = pcg32_seed(1234), c = pcg32_seed(1235);
    int same = 1, diff = 0;
    for (int i = 0; i < 1000; i++) {
        uint32_t x = pcg32_next(&a), y = pcg32_next(&b), z = pcg32_next(&c);
        if (x != y) same = 0;
        if (x != z) diff++;
    }
    CHECK(same, "pcg32 same seed must match");
    CHECK(diff > 990, "pcg32 different seeds should differ (%d)", diff);
    CHECK(ct_hash64_3(1, 2, 3) == ct_hash64_3(1, 2, 3), "hash stable");
    CHECK(ct_hash64_3(1, 2, 3) != ct_hash64_3(1, 3, 2), "hash order sensitive");
    double n0 = ct_noise1(7, 3.25), n1 = ct_noise1(7, 3.25);
    CHECK(n0 == n1, "noise deterministic");
    double mx = 0.0;
    for (int i = 0; i < 10000; i++) { double v = fabs(ct_noise1(9, i * 0.137)); if (v > mx) mx = v; }
    CHECK(mx <= 1.0, "noise range");
    Pcg32 r = pcg32_seed(5);
    int okRange = 1;
    for (int i = 0; i < 10000; i++) { int v = pcg32_int(&r, 3, 7); if (v < 3 || v > 7) okRange = 0; }
    CHECK(okRange, "pcg32_int inclusive range");
}

static void test_path(void)
{
    PathStyle st = default_style();
    PathSystem a, b;
    path_init(&a, 42, st);
    path_init(&b, 42, st);
    // a: generated in one go; b: generated incrementally like a running game.
    path_ensure(&a, 4900.0, 5100.0);
    for (double s = 0.0; s <= 5100.0; s += 7.3) path_ensure(&b, s - 100.0, s + 10.0);
    PathSample pa = path_sample(&a, 5000.0), pb = path_sample(&b, 5000.0);
    double d = dv_len(dv_sub(pa.pos, pb.pos));
    CHECK(d < 1e-9, "path determinism: |dp| = %g", d);
    CHECK(fabs(dv_dot(pa.N, pb.N) - 1.0) < 1e-9, "path frame determinism");

    // Jump back regenerates from 0 and must reproduce the same sample.
    path_ensure(&b, 20000.0, 20100.0);
    path_ensure(&b, 4900.0, 5100.0);
    PathSample pc = path_sample(&b, 5000.0);
    CHECK(dv_len(dv_sub(pa.pos, pc.pos)) < 1e-9, "path regeneration determinism");

    // Different seed differs.
    PathSystem c;
    path_init(&c, 43, st);
    path_ensure(&c, 4900.0, 5100.0);
    CHECK(dv_len(dv_sub(path_sample(&c, 5000.0).pos, pa.pos)) > 1.0, "different seed gives different path");

    // Continuity over 20,000 units.
    PathSystem p;
    path_init(&p, 7, st);
    double minN = 1.0, minT = 1.0, maxCurv = 0.0, maxStepErr = 0.0, minTsmall = 1.0;
    PathSample prev = { 0 };
    double limit = path_curvature_limit(&st);
    for (double s = 0.0; s <= 20000.0; s += PATH_DS) {
        path_ensure(&p, s - 100.0, s + 50.0);
        PathSample q = path_sample(&p, s);
        if (s > 0.0) {
            double dn = dv_dot(prev.N, q.N), dt = dv_dot(prev.T, q.T);
            if (dn < minN) minN = dn;
            if (dt < minT) minT = dt;
            double step = dv_len(dv_sub(q.pos, prev.pos));
            double err = fabs(step - PATH_DS);
            if (err > maxStepErr) maxStepErr = err;
            // C1: tangent must agree with the chord direction.
            double chord = dv_dot(dv_norm(dv_sub(q.pos, prev.pos)), dv_norm(dv_add(q.T, prev.T)));
            if (chord < minTsmall) minTsmall = chord;
        }
        if (q.curvature > maxCurv) maxCurv = q.curvature;
        prev = q;
    }
    CHECK(minN > 0.99, "normal continuity: min dot = %f", minN);
    CHECK(minT > 0.99, "tangent continuity: min dot = %f", minT);
    CHECK(maxCurv <= limit, "max curvature %f <= limit %f", maxCurv, limit);
    CHECK(maxStepErr < 1e-3, "position continuity / arc-length uniformity: max err %g", maxStepErr);
    CHECK(minTsmall > 0.9999, "C1 tangent vs chord: min dot %f", minTsmall);
    printf("  path: minDotN %.6f minDotT %.6f maxCurv %.5f (limit %.5f) stepErr %.2e\n", minN, minT, maxCurv, limit, maxStepErr);

    // Fine sampling across a control-segment boundary (every 40 units nominal).
    double worst = 1.0;
    for (double s = 10.0; s < 2000.0; s += 0.05) {
        PathSample x = path_sample(&a, s), y = path_sample(&a, s + 0.05);
        double dt = dv_dot(x.T, y.T);
        if (dt < worst) worst = dt;
    }
    CHECK(worst > 0.9999, "fine tangent continuity: %f", worst);

    path_free(&a); path_free(&b); path_free(&c); path_free(&p);
}

static uint64_t chunk_hash(const ChunkCpu *c)
{
    uint64_t h = 1469598103934665603ull;
    for (int b = 0; b < CHUNK_BATCHES; b++) h = ct_hash64_2(h, lg_hash(&c->geo[b]));
    const unsigned char *u = (const unsigned char *)c->uniforms;
    for (size_t i = 0; i < sizeof(c->uniforms); i++) { h ^= u[i]; h *= 0x100000001B3ull; }
    h = ct_hash64_2(h, (uint64_t)c->debrisCount);
    return h;
}

static void test_chunks(void)
{
    Environment env = env_vortex_create();
    ParamRegistry reg;
    params_clear(&reg);
    env.registerParams(&env, &reg);
    env.init(&env, 99, NULL);
    PathStyle st = env.pathStyle(&env);

    enum { N = 24 };
    uint64_t inOrder[N], outOfOrder[N];
    static ChunkCpu cpu;   // large; static to keep it off the stack
    memset(&cpu, 0, sizeof(cpu));

    PathSystem p1;
    path_init(&p1, 99, st);
    path_ensure(&p1, 0.0, N * CHUNK_LEN + 50.0);
    double maxBuild = 0.0;
    for (int k = 0; k < N; k++) {
        chunk_cpu_begin(&cpu, &p1, k);
        env.buildChunk(&env, &p1, k, &cpu);
        inOrder[k] = chunk_hash(&cpu);
    }
    // Different order, different path instance generated incrementally.
    PathSystem p2;
    path_init(&p2, 99, st);
    path_ensure(&p2, 0.0, N * CHUNK_LEN + 50.0);
    static ChunkCpu cpu2;
    memset(&cpu2, 0, sizeof(cpu2));
    int order[N];
    for (int i = 0; i < N; i++) order[i] = i;
    Pcg32 r = pcg32_seed(3);
    for (int i = N - 1; i > 0; i--) { int j = pcg32_int(&r, 0, i); int t = order[i]; order[i] = order[j]; order[j] = t; }
    for (int i = 0; i < N; i++) {
        int k = order[i];
        chunk_cpu_begin(&cpu2, &p2, k);
        env.buildChunk(&env, &p2, k, &cpu2);
        outOfOrder[k] = chunk_hash(&cpu2);
    }
    int same = 1;
    for (int k = 0; k < N; k++) if (inOrder[k] != outOfOrder[k]) same = 0;
    CHECK(same, "chunk build determinism independent of order");

    int distinct = 0;
    for (int k = 1; k < N; k++) if (inOrder[k] != inOrder[k - 1]) distinct++;
    CHECK(distinct == N - 1, "chunks differ from each other");

    // Far away: local coordinates stay small, counts within limits.
    PathSystem p3;
    path_init(&p3, 99, st);
    path_ensure(&p3, 52000.0 - 100.0, 52000.0 + 200.0);
    int k = (int)(52000.0 / CHUNK_LEN);
    chunk_cpu_begin(&cpu, &p3, k);
    env.buildChunk(&env, &p3, k, &cpu);
    float maxLocal = 0.0f;
    int verts = 0;
    for (int b = 0; b < CHUNK_BATCHES; b++) {
        const LineGeo *g = &cpu.geo[b];
        verts += g->vcount;
        for (int i = 0; i < g->vcount * 3; i++) if (fabsf(g->pos[i]) > maxLocal) maxLocal = fabsf(g->pos[i]);
        for (int bb = 0; bb < lg_batch_count(g); bb++) {
            Mesh m = lg_batch_mesh(g, bb);
            CHECK(m.vertexCount <= 65535, "mesh batch <= 65535 vertices");
        }
    }
    CHECK(maxLocal < 2.0f * (float)CHUNK_LEN, "chunk-local coordinates small at s=52000 (%f)", maxLocal);
    printf("  chunks: far chunk %d verts, max local coord %.1f\n", verts, maxLocal);

    // Debris never intrudes into the keep-out lane.
    int lane = 1;
    for (int kk = 0; kk < N; kk++) {
        chunk_cpu_begin(&cpu, &p1, kk);
        env.buildChunk(&env, &p1, kk, &cpu);
        for (int i = 0; i < cpu.debrisCount; i++) {
            const DebrisInfo *d = &cpu.debris[i];
            PathSample ps = path_sample(&p1, d->s);
            DVec3 rel = dv_sub(d->center, ps.pos);
            double radial = dv_len(dv_sub(rel, dv_scale(ps.T, dv_dot(rel, ps.T))));
            if (radial - d->radius < 0.35 * ps.radius - 1e-3) lane = 0;
        }
    }
    CHECK(lane, "debris outside keep-out radius");

    (void)maxBuild;
    path_free(&p1); path_free(&p2); path_free(&p3);
    for (int b = 0; b < CHUNK_BATCHES; b++) { lg_free(&cpu.geo[b]); lg_free(&cpu2.geo[b]); }
    env.unload(&env);
}

// The sim must produce identical states at 30, 60 and 144 fps (fixed 120 Hz steps).
static void test_flight_framerate(void)
{
    PathStyle st = default_style();
    PathSystem path;
    path_init(&path, 5, st);
    path_ensure(&path, 0.0, 2000.0);
    ParamRegistry reg;
    params_clear(&reg);
    FlightParams fp;
    flight_register_params(&fp, &reg);
    const double fps[3] = { 30.0, 60.0, 144.0 };
    static FlightState prev[3], cur[3];
    double acc[3] = { 0, 0, 0 };
    for (int k = 0; k < 3; k++) flight_reset(&cur[k], &fp, &path, 0.0);
    const int targetSteps = 720;   // 6 s of sim
    for (int k = 0; k < 3; k++) {
        double dt = 1.0 / fps[k];
        int done = 0;
        while (done < targetSteps) {
            FlightInput in = { 0 };
            // Input as a function of sim time so the frame rate can't change what is pressed.
            double simT = cur[k].time;
            in.steerX = simT < 2.0 ? 1.0f : (simT < 4.0 ? -0.5f : 0.0f);
            in.steerY = simT > 3.0 ? 0.7f : 0.0f;
            in.boost = simT > 1.0 && simT < 3.5;
            int left = targetSteps - done;
            done += flight_advance(&prev[k], &cur[k], &fp, &path, &in, &acc[k], dt, left < 24 ? left : 24);
        }
    }
    // Inputs are sampled per frame, so a change can land up to one frame later at low fps:
    // results must be equivalent, not bit-identical.
    double worstPos = 0.0, worstCam = 0.0;
    for (int k = 1; k < 3; k++) {
        CHECK(fabs(cur[k].time - cur[0].time) < 1e-9, "same number of sim steps");
        double dp = dv_len(dv_sub(cur[k].shipPos, cur[0].shipPos));
        double dc = dv_len(dv_sub(cur[k].camPos, cur[0].camPos));
        if (dp > worstPos) worstPos = dp;
        if (dc > worstCam) worstCam = dc;
    }
    printf("  flight: 30/60/144 fps max ship deviation %.4f, camera %.4f units after 6 s\n", worstPos, worstCam);
    CHECK(worstPos < 0.25 && worstCam < 0.25, "flight equivalent across frame rates");
    path_free(&path);
}

static void test_params(void)
{
    ParamRegistry reg;
    params_clear(&reg);
    float f = 0; int n = 0; bool b = false;
    params_float(&reg, "G", "alpha", &f, 1.5f, 0.0f, 10.0f);
    params_int(&reg, "G", "count", &n, 3, 0, 100);
    params_bool(&reg, "H", "flag", &b, true);
    CHECK(f == 1.5f && n == 3 && b, "defaults applied");
    f = 7.123456f; n = 42; b = false;
    const char *path = "cybertunnel_test_params.ini";
    CHECK(params_save_ini(&reg, path), "save ini");
    f = 0; n = 0; b = true;
    int applied = params_load_ini(&reg, path);
    CHECK(applied == 3, "load ini applied %d", applied);
    CHECK(f == 7.123456f && n == 42 && !b, "ini round trip (%f %d %d)", f, n, b);
    params_set(params_find(&reg, "alpha"), 99.0f);
    CHECK(f == 10.0f, "clamp to range");
    remove(path);
}

int main(void)
{
    printf("cybertunnel tests\n");
    test_rng();
    test_path();
    test_chunks();
    test_flight_framerate();
    test_params();
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
