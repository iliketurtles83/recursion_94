// Determinism and continuity tests. Runs without a window or GL context.
#include "core/rng.h"
#include "core/noise.h"
#include "core/path.h"
#include "core/chunks.h"
#include "core/params.h"
#include "core/flight.h"
#include "env/env.h"
#include "render/gridfloor.h"
#include "env/city_towers.h"
#include <time.h>
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

// Path coverage a chunk build needs, as the core provides it (the ring buffer only holds
// PATH_CAP samples, so long test runs must stream the path like the game does).
static void ensure_for_chunk(PathSystem *p, const Environment *env, int k)
{
    path_ensure(p, k * CHUNK_LEN - 100.0 - env->pathReach, (k + 1) * CHUNK_LEN + 100.0 + env->pathReach);
}

// Builds chunks 0..n-1 in order on one path instance and shuffled on an independently
// generated one; fills inOrder[] with the hashes. Returns true when both orders agree.
static bool build_order_independent(Environment *env, uint64_t seed, int n, uint64_t *inOrder)
{
    enum { NMAX = 256 };
    PathStyle st = env->pathStyle(env);
    static ChunkCpu a, b;   // large; static to keep them off the stack
    uint64_t outOfOrder[NMAX];
    if (n > NMAX) n = NMAX;

    PathSystem p1, p2;
    path_init(&p1, seed, st);
    for (int k = 0; k < n; k++) {
        ensure_for_chunk(&p1, env, k);
        chunk_cpu_begin(&a, &p1, k);
        env->buildChunk(env, &p1, k, &a);
        inOrder[k] = chunk_hash(&a);
    }
    path_init(&p2, seed, st);
    int order[NMAX];
    for (int i = 0; i < n; i++) order[i] = i;
    Pcg32 r = pcg32_seed(3);
    for (int i = n - 1; i > 0; i--) { int j = pcg32_int(&r, 0, i); int t = order[i]; order[i] = order[j]; order[j] = t; }
    for (int i = 0; i < n; i++) {
        int k = order[i];
        ensure_for_chunk(&p2, env, k);   // jumping back regenerates the path from 0, as in the game
        chunk_cpu_begin(&b, &p2, k);
        env->buildChunk(env, &p2, k, &b);
        outOfOrder[k] = chunk_hash(&b);
    }
    bool same = true;
    for (int k = 0; k < n; k++) if (inOrder[k] != outOfOrder[k]) same = false;
    path_free(&p1); path_free(&p2);
    for (int i = 0; i < CHUNK_BATCHES; i++) { lg_free(&a.geo[i]); lg_free(&b.geo[i]); }
    return same;
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
    uint64_t inOrder[N];
    static ChunkCpu cpu;   // large; static to keep it off the stack
    memset(&cpu, 0, sizeof(cpu));
    CHECK(build_order_independent(&env, 99, N, inOrder), "chunk build determinism independent of order");

    PathSystem p1;
    path_init(&p1, 99, st);
    path_ensure(&p1, 0.0, N * CHUNK_LEN + 50.0);

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

    path_free(&p1); path_free(&p3);
    for (int b = 0; b < CHUNK_BATCHES; b++) lg_free(&cpu.geo[b]);
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

// Environment #2 (open, floored): level path with world-up frames, floor clamp, chunk determinism.
static void test_city(void)
{
    Environment env = env_city_create();
    ParamRegistry reg;
    params_clear(&reg);
    env.registerParams(&env, &reg);
    env.init(&env, 11, NULL);
    PathStyle st = env.pathStyle(&env);
    CHECK(!st.hasWalls && st.floorDepth > 0.0f, "city style is open with a floor");

    // Level path: y stays 0 and N stays world up, so the floor is the plane y = -floorDepth.
    PathSystem p;
    path_init(&p, 11, st);
    double maxY = 0.0, minUp = 1.0, maxAxisErr = 0.0, maxCurv = 0.0;
    for (double s = 0.0; s <= 20000.0; s += PATH_DS) {
        path_ensure(&p, s - 100.0, s + 50.0);
        PathSample q = path_sample(&p, s);
        if (fabs(q.pos.y) > maxY) maxY = fabs(q.pos.y);
        if (q.N.y < minUp) minUp = q.N.y;
        if (q.curvature > maxCurv) maxCurv = q.curvature;
        // Heading spring: the path weaves around the world axes the floor grid follows.
        double yawDeg = atan2(q.T.x, -q.T.z) * 180.0 / 3.14159265358979;
        double err = fabs(yawDeg - 90.0 * floor(yawDeg / 90.0 + 0.5));
        if (err > maxAxisErr) maxAxisErr = err;
    }
    printf("  city path: max |y| %.2e, min N.y %.9f, max heading off-axis %.1f deg, maxCurv %.5f (limit %.5f)\n",
           maxY, minUp, maxAxisErr, maxCurv, path_curvature_limit(&st));
    CHECK(maxY < 1e-9, "city path stays level (max |y| %g)", maxY);
    CHECK(minUp > 0.999999, "city path N is world up (min N.y %.9f)", minUp);
    CHECK(maxAxisErr < 30.0, "city heading stays near a grid axis (%.1f deg)", maxAxisErr);
    CHECK(maxCurv <= path_curvature_limit(&st), "city curvature within limit with yaw spring");

    // Flight: full down (and diagonals) never takes the ship below floor + clearance.
    path_ensure(&p, 0.0, 2000.0);
    ParamRegistry freg;
    params_clear(&freg);
    FlightParams fp;
    flight_register_params(&fp, &freg);
    static FlightState fs;
    flight_reset(&fs, &fp, &p, 0.0);
    double floorY = -st.floorDepth, lowest = 1e9;
    float minOffset = 1e9f;
    for (int i = 0; i < 720; i++) {
        FlightInput in = { 0 };
        in.steerY = -1.0f;
        in.steerX = i < 360 ? 1.0f : -1.0f;
        flight_step(&fs, &fp, &p, &in, 1.0 / FLIGHT_HZ);
        if (fs.shipPos.y < lowest) lowest = fs.shipPos.y;
        if (fs.y < minOffset) minOffset = fs.y;
    }
    printf("  city flight: lowest ship y %.3f (floor %.2f, clearance %.2f)\n", lowest, floorY, FLIGHT_CLEARANCE);
    CHECK(lowest >= floorY + FLIGHT_CLEARANCE - 1e-4, "ship stays above floor + clearance (%f)", lowest);
    CHECK(minOffset < -0.5f * st.floorDepth, "ship can still descend toward the floor (%f)", minOffset);
    path_free(&p);

    enum { N = 200 };
    static uint64_t h[N];
    CHECK(build_order_independent(&env, 11, N, h), "city chunk build independent of order (%d chunks)", N);
    int distinct = 0;
    for (int k = 1; k < N; k++) if (h[k] != h[k - 1]) distinct++;
    CHECK(distinct == N - 1, "city chunks differ from each other (%d/%d)", distinct, N - 1);

    // Grid floor phase: the shader adds a float render-space position to gridfloor_offset();
    // that must give the same grid phase as the exact world position, at any origin.
    Pcg32 r = pcg32_seed(17);
    double worst = 0.0;
    for (int i = 0; i < 20000; i++) {
        float cell = pcg32_range(&r, 0.5f, 8.0f);
        DVec3 o = dv3(pcg32_range(&r, -1e6f, 1e6f), 0.0, pcg32_range(&r, -1e6f, 1e6f));
        float lx = pcg32_range(&r, -1500.0f, 1500.0f), lz = pcg32_range(&r, -1500.0f, 1500.0f);
        Vector2 off = gridfloor_offset(o, cell);
        float gx = (off.x + lx) / cell, gz = (off.y + lz) / cell;          // what the shader does
        double ex = (o.x + lx) / cell, ez = (o.z + lz) / cell;             // exact
        double dx = fabs((gx - floor(gx)) - (ex - floor(ex))), dz = fabs((gz - floor(gz)) - (ez - floor(ez)));
        if (dx > 0.5) dx = 1.0 - dx;
        if (dz > 0.5) dz = 1.0 - dz;
        if (dx > worst) worst = dx;
        if (dz > worst) worst = dz;
    }
    printf("  grid floor: worst phase error %.2e cells over 20000 random origins up to 1e6\n", worst);
    CHECK(worst < 1e-3, "grid floor phase exact across origins (%g cells)", worst);
    env.unload(&env);
}

static int claim_cmp(const void *a, const void *b)
{
    const CellClaim *x = (const CellClaim *)a, *y = (const CellClaim *)b;
    if (x->ix != y->ix) return x->ix < y->ix ? -1 : 1;
    if (x->iz != y->iz) return x->iz < y->iz ? -1 : 1;
    return 0;
}

static const CellClaim *find_claim(const CellClaim *c, int n, int ix, int iz)
{
    CellClaim key = { ix, iz, 0.0, 0.0f, 0 };
    return (const CellClaim *)bsearch(&key, c, (size_t)n, sizeof(CellClaim), claim_cmp);
}

// Tower placement: every lattice cell near the path has exactly one owning chunk, no tower
// enters the lane, footprints sit on floor grid lines, builds stay inside the budget.
static void test_city_towers(void)
{
    Environment env = env_city_create();
    ParamRegistry reg;
    params_clear(&reg);
    env.registerParams(&env, &reg);
    PathStyle st = env.pathStyle(&env);
    CityTowerParams tp = { 0 };
    tp.cell = 2.2f; tp.latticeCells = 8; tp.laneHalfWidth = st.baseRadius; tp.clearance = 6.0f;
    tp.bandMax = 150.0f; tp.densityNear = 0.5f; tp.densityFar = 0.3f;
    tp.heightMin = 28.0f; tp.heightMax = 65.0f; tp.heightFar = 40.0f;
    tp.landmarkChance = 0.05f; tp.landmarkMul = 1.7f; tp.footMin = 3; tp.footMax = 5;
    tp.setbackChance = 0.55f; tp.floorY = -st.floorDepth;
    double reach = city_towers_reach(&tp);

    enum { N = 200, MAXC = 200000 };
    PathSystem p;
    path_init(&p, 23, st);

    static CellClaim all[MAXC], chunkClaims[4096];
    static TowerDesc towers[CITY_MAX_TOWERS];
    static LineGeo body, edges;
    int nAll = 0, nTowers = 0, laneOk = 1, gridOk = 1, ownerOk = 1;
    double worstMs = 0.0, minGap = 1e9;
    float inner = tp.laneHalfWidth + tp.clearance;
    for (int k = 0; k < N; k++) {
        int nc = 0;
        path_ensure(&p, k * CHUNK_LEN - 100.0 - reach, (k + 1) * CHUNK_LEN + 100.0 + reach);
        clock_t c0 = clock();
        int n = city_towers_plan(&tp, NULL, 23, &p, k, towers, CITY_MAX_TOWERS, chunkClaims, 4096, &nc);
        lg_clear(&body); lg_clear(&edges);
        PathSample o = path_sample(&p, k * CHUNK_LEN);
        city_towers_build(towers, n, o.pos, &body, &edges, 0.9f, 0.03f);
        double ms = (double)(clock() - c0) * 1000.0 / CLOCKS_PER_SEC;
        if (ms > worstMs) worstMs = ms;
        nTowers += n;
        for (int i = 0; i < nc && nAll < MAXC; i++) {
            if (chunk_index_at(chunkClaims[i].sOwner) != k) ownerOk = 0;
            all[nAll++] = chunkClaims[i];
        }
        for (int i = 0; i < n; i++) {
            const TowerDesc *t = &towers[i];
            // Brute-force distance from a fine polyline of the path to the footprint.
            for (double s = fmax(0.0, t->sOwner - 250.0); s <= t->sOwner + 250.0; s += 0.5) {
                DVec3 q = path_sample(&p, s).pos;
                double dx = q.x < t->x0[0] ? t->x0[0] - q.x : (q.x > t->x1[0] ? q.x - t->x1[0] : 0.0);
                double dz = q.z < t->z0[0] ? t->z0[0] - q.z : (q.z > t->z1[0] ? q.z - t->z1[0] : 0.0);
                double d = sqrt(dx * dx + dz * dz);
                if (d < minGap) minGap = d;
                if (d < inner - 0.05) laneOk = 0;
            }
            double v[4] = { t->x0[0], t->x1[0], t->z0[0], t->z1[0] };
            for (int j = 0; j < 4; j++) if (fabs(v[j] / tp.cell - floor(v[j] / tp.cell + 0.5)) > 1e-6) gridOk = 0;
        }
    }
    qsort(all, (size_t)nAll, sizeof(CellClaim), claim_cmp);
    int dups = 0;
    for (int i = 1; i < nAll; i++) if (!claim_cmp(&all[i - 1], &all[i])) dups++;

    // Brute force: owner of every cell near the interior of the path, from the whole polyline.
    int nSeg = (int)((N * CHUNK_LEN + reach) / 2.0);
    double *px = (double *)malloc(sizeof(double) * (size_t)(nSeg + 1)), *pz = (double *)malloc(sizeof(double) * (size_t)(nSeg + 1));
    double minx = 1e300, maxx = -1e300, minz = 1e300, maxz = -1e300;
    path_reset(&p, 23, st);
    for (int i = 0; i <= nSeg; i++) {
        path_ensure(&p, i * 2.0 - 10.0, i * 2.0 + 10.0);
        DVec3 q = path_sample(&p, i * 2.0).pos;
        px[i] = q.x; pz[i] = q.z;
        if (i * 2.0 >= CHUNK_LEN && i * 2.0 <= (N - 1) * CHUNK_LEN) {
            if (q.x < minx) minx = q.x;
            if (q.x > maxx) maxx = q.x;
            if (q.z < minz) minz = q.z;
            if (q.z > maxz) maxz = q.z;
        }
    }
    double L = tp.latticeCells * (double)tp.cell;
    int checked = 0, missing = 0, wrongOwner = 0;
    for (int iz = (int)floor((minz - tp.bandMax) / L); iz <= (int)floor((maxz + tp.bandMax) / L); iz++) {
        for (int ix = (int)floor((minx - tp.bandMax) / L); ix <= (int)floor((maxx + tp.bandMax) / L); ix++) {
            double cx = (ix + 0.5) * L, cz = (iz + 0.5) * L, best = 1e300, bs = 0.0;
            for (int i = 0; i < nSeg; i++) {
                double dx = px[i + 1] - px[i], dz = pz[i + 1] - pz[i], l2 = dx * dx + dz * dz;
                double t = l2 > 0.0 ? ((cx - px[i]) * dx + (cz - pz[i]) * dz) / l2 : 0.0;
                t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
                double ex = px[i] + dx * t - cx, ez = pz[i] + dz * t - cz, d2 = ex * ex + ez * ez;
                if (d2 < best) { best = d2; bs = (i + t) * 2.0; }
            }
            int owner = chunk_index_at(bs);
            if (sqrt(best) > tp.bandMax - 0.01 || owner < 1 || owner > N - 2) continue;
            checked++;
            const CellClaim *c = find_claim(all, nAll, ix, iz);
            if (!c) missing++;
            else if (chunk_index_at(c->sOwner) != owner) wrongOwner++;
        }
    }
    free(px); free(pz);
    printf("  city towers: %d chunks, %d towers, %d claimed cells, %d brute-force checked; worst build %.2f ms; min lane gap %.2f (need %.1f)\n",
           N, nTowers, nAll, checked, worstMs, minGap, inner);
    CHECK(nTowers > N * 5, "towers are placed (%d)", nTowers);
    CHECK(ownerOk, "every claim lies in its own chunk");
    CHECK(dups == 0, "no lattice cell claimed by two chunks (%d duplicates)", dups);
    CHECK(checked > 1000 && missing == 0, "every cell in the band claimed (%d of %d missing)", missing, checked);
    CHECK(wrongOwner == 0, "claimed by the brute-force owner (%d wrong)", wrongOwner);
    CHECK(laneOk, "no tower footprint inside lane + clearance (min gap %.3f)", minGap);
    CHECK(gridOk, "tower footprints on floor grid lines");
    CHECK(worstMs < 3.0, "chunk tower build < 3 ms (%.2f ms)", worstMs);
    lg_free(&body); lg_free(&edges);
    path_free(&p);
    env.unload(&env);
}

// Full city chunks (towers + traces + glyphs + props): vertex budget, build time, props
// float between the lane edge and the towers.
static void test_city_decor(void)
{
    Environment env = env_city_create();
    ParamRegistry reg;
    params_clear(&reg);
    env.registerParams(&env, &reg);
    env.init(&env, 31, NULL);
    PathStyle st = env.pathStyle(&env);
    float lane = st.baseRadius, gapEnd = lane + params_get(params_find(&reg, "towerClearance"));
    PathSystem p;
    path_init(&p, 31, st);
    static ChunkCpu c;
    enum { N = 200 };
    int maxVerts = 0, traceVerts = 0, glyphQuads = 0, props = 0, propsOk = 1;
    int arch[N], seen[8] = { 0 };
    double worstMs = 0.0;
    for (int k = 0; k < N; k++) {
        ensure_for_chunk(&p, &env, k);
        clock_t c0 = clock();
        chunk_cpu_begin(&c, &p, k);
        env.buildChunk(&env, &p, k, &c);
        double ms = (double)(clock() - c0) * 1000.0 / CLOCKS_PER_SEC;
        if (ms > worstMs) worstMs = ms;
        arch[k] = c.archetype;
        if (c.archetype >= 0 && c.archetype < 8) seen[c.archetype]++;
        int v = 0;
        for (int b = 0; b < CHUNK_BATCHES; b++) v += c.geo[b].vcount;
        if (v > maxVerts) maxVerts = v;
        traceVerts += c.geo[2].vcount;
        glyphQuads += c.geo[3].vcount / 4;
        for (int i = 0; i < c.debrisCount; i++) {
            const DebrisInfo *d = &c.debris[i];
            PathSample ps = path_sample(&p, d->s);
            DVec3 rel = dv_sub(d->center, ps.pos);
            double lat = fabs(dv_dot(rel, ps.B)), up = dv_dot(rel, ps.N) + st.floorDepth;
            if (lat - d->radius < lane - 1e-3 || lat + d->radius > gapEnd + 1e-3 || up - d->radius < 0.4 - 1e-3) propsOk = 0;
            props++;
        }
    }
    printf("  city chunks: max %d verts/chunk, avg %d trace verts and %d glyph panels per chunk, %d props; worst full build %.2f ms\n",
           maxVerts, traceVerts / N, glyphQuads / N, props, worstMs);
    CHECK(traceVerts > 0 && glyphQuads > 0 && props > 0, "decor and props generated");
    CHECK(maxVerts <= 60000, "chunk vertex budget (%d <= 60000)", maxVerts);
    CHECK(worstMs < 3.0, "full city chunk build < 3 ms (%.2f ms)", worstMs);
    CHECK(propsOk, "props between lane edge and towers, above the floor");

    // Pacing: starts on an avenue, every archetype appears, runs within their bounds
    // (AVENUE 4-9, DOWNTOWN 3-6, OPEN 3-6, PLAZA 2-3 chunks; the last run may be cut off).
    static const int rmin[4] = { 4, 3, 3, 2 }, rmax[4] = { 9, 6, 6, 3 };
    int runsOk = 1, runs = 0;
    for (int k = 0; k < N;) {
        int e = k;
        while (e < N && arch[e] == arch[k]) e++;
        int len = e - k, a = arch[k];
        if (a < 0 || a > 3 || len > rmax[a] || (e < N && len < rmin[a])) runsOk = 0;
        runs++;
        k = e;
    }
    printf("  city pacing: %d runs over %d chunks; AVENUE %d DOWNTOWN %d OPEN %d PLAZA %d chunks\n", runs, N, seen[0], seen[1], seen[2], seen[3]);
    CHECK(arch[0] == 0, "city flight starts on an AVENUE");
    CHECK(seen[0] && seen[1] && seen[2] && seen[3], "all four city archetypes appear");
    CHECK(runsOk, "city archetype runs within their length bounds");
    for (int b = 0; b < CHUNK_BATCHES; b++) lg_free(&c.geo[b]);
    path_free(&p);
    env.unload(&env);
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
    test_city();
    test_city_towers();
    test_city_decor();
    test_params();
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
