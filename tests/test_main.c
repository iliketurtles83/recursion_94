// Determinism and continuity tests. Runs without a window or GL context.
#include "core/rng.h"
#include "core/noise.h"
#include "core/path.h"
#include "core/chunks.h"
#include "core/params.h"
#include "core/flight.h"
#include "env/env.h"
#include "render/gridfloor.h"
#include "render/tunneldetail.h"
#include "env/city_towers.h"
#include "env/corridor_pillars.h"
#include "env/corridor_beams.h"
#include "env/crystal_field.h"
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <windows.h>
#endif

// The legacy Windows CRT clock() can advance in coarse ticks, producing false
// failures for sub-millisecond work. QPC measures these short builds precisely.
static double build_timer_seconds(void)
{
#ifdef _WIN32
    LARGE_INTEGER counter, frequency;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart / (double)frequency.QuadPart;
#else
    return (double)clock() / CLOCKS_PER_SEC;
#endif
}

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
    params_set(params_find(&reg,"showPanels"),1);
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

static void test_tunnel_detail(void)
{
    PathSystem path;path_init(&path,83,default_style());path_ensure(&path,0,120);
    DVec3 origin=path_sample(&path,0).pos;
    LineGeo a={0},b={0};
    tunnel_detail_build(&a,&path,origin,0);tunnel_detail_build(&b,&path,origin,0);
    int bytes=a.vcount*60+a.icount*2;
    CHECK(bytes>=180*1024&&bytes<=220*1024,"tunnel adds about 200 KB of actual mesh data (%d)",bytes);
    CHECK(lg_hash(&a)==lg_hash(&b),"solid tunnel hardware deterministic");
    bool valid=true,outside=true;int materials[5]={0};
    for(int i=0;i<a.vcount;i++) {
        float norm=0;
        for(int j=0;j<3;j++){valid=valid&&isfinite(a.pos[i*3+j])&&isfinite(a.nrm[i*3+j]);norm+=a.nrm[i*3+j]*a.nrm[i*3+j];}
        valid=valid&&fabsf(norm-1)<.002f;
        int material=(int)a.uv2[i*2+1];if(material>=1&&material<=4)materials[material]++;
        DVec3 world=dv_add(origin,dv3(a.pos[i*3],a.pos[i*3+1],a.pos[i*3+2]));
        double nearest=1e9,radius=12;
        for(int s=0;s<=48;s+=3){PathSample p=path_sample(&path,s);double d=dv_len(dv_sub(world,p.pos));if(d<nearest){nearest=d;radius=p.radius;}}
        if(nearest<radius*.95)outside=false;
    }
    CHECK(valid&&a.batchCount==1,"hardware normals finite and mesh within index limit");
    CHECK(outside,"solid wall hardware outside player flight lane");
    CHECK(materials[1]&&materials[2]&&materials[3]&&materials[4],"ribs, recesses, louvers and conduit materials present");
    printf("  tunnel: %d added hardware vertices, %d mesh bytes per 48 m chunk\n",a.vcount,bytes);
    lg_free(&a);lg_free(&b);path_free(&path);
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
        double c0 = build_timer_seconds();
        int n = city_towers_plan(&tp, NULL, 23, &p, k, towers, CITY_MAX_TOWERS, chunkClaims, 4096, &nc);
        lg_clear(&body); lg_clear(&edges);
        PathSample o = path_sample(&p, k * CHUNK_LEN);
        city_towers_build(towers, n, o.pos, &body, &edges, 0.9f, 0.03f);
        double ms = (build_timer_seconds() - c0) * 1000.0;
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
        double c0 = build_timer_seconds();
        chunk_cpu_begin(&c, &p, k);
        env.buildChunk(&env, &p, k, &c);
        double ms = (build_timer_seconds() - c0) * 1000.0;
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

// Environment #3 (server corridor): level path, world-up frames, heading close to a grid axis.
static void test_greybox(void)
{
    Environment env = env_greybox_create();
    ParamRegistry reg;
    params_clear(&reg);
    env.registerParams(&env, &reg);
    env.init(&env, 7, NULL);

    // Same path style as the vortex defaults, so the same seed flies the same course.
    Environment vx = env_vortex_create();
    ParamRegistry vreg;
    params_clear(&vreg);
    vx.registerParams(&vx, &vreg);
    PathStyle a = env.pathStyle(&env), b = vx.pathStyle(&vx);
    bool same = a.baseRadius == b.baseRadius && a.radiusNoiseAmp == b.radiusNoiseAmp && a.radiusNoiseScale == b.radiusNoiseScale
             && a.maxYawStepDeg == b.maxYawStepDeg && a.maxPitchDeg == b.maxPitchDeg && a.pitchSpring == b.pitchSpring
             && a.twistRateMax == b.twistRateMax && a.hasWalls == b.hasWalls && a.floorDepth == b.floorDepth
             && a.ceilingHeight == b.ceilingHeight && a.yawSpring == b.yawSpring && a.yawAxisDeg == b.yawAxisDeg;
    CHECK(same, "greybox path style equals the vortex default");
    vx.unload(&vx);

    enum { N = 60 };
    uint64_t h[N];
    CHECK(build_order_independent(&env, 7, N, h), "greybox chunk build independent of order (%d chunks)", N);

    // One ring per 10 m with no duplicates at chunk boundaries: count ring circles (72 segments
    // each) over 20 chunks; the rails add a fixed number of segments per chunk.
    PathSystem p;
    path_init(&p, 7, a);
    static ChunkCpu c;
    int segs = 0;
    for (int k = 0; k < 20; k++) {
        path_ensure(&p, k * CHUNK_LEN - 100.0, (k + 1) * CHUNK_LEN + 100.0);
        chunk_cpu_begin(&c, &p, k);
        env.buildChunk(&env, &p, k, &c);
        segs += c.geo[0].icount / 6;
    }
    int rings = 20 * (int)CHUNK_LEN / 10, envelopes = 20 * (int)CHUNK_LEN / 50 + 1;
    int railSegs = 20 * 8 * (int)ceil(CHUNK_LEN / 2.0);
    CHECK(segs == (rings + envelopes) * 72 + railSegs, "greybox: %d rings + %d envelope circles + rails over 960 m (%d segments)",
          rings, envelopes, segs);

    // Envelope circles stay every 50 m when the ring spacing does not divide 50.
    Param *spacing = params_find(&reg, "gbRingSpacing"), *showRings = params_find(&reg, "gbShowRings"), *rails = params_find(&reg, "gbRails");
    params_set(spacing, 12.0f); params_set(showRings, 0.0f); params_set(rails, 0.0f);
    segs = 0;
    for (int k = 0; k < 20; k++) {
        chunk_cpu_begin(&c, &p, k);
        env.buildChunk(&env, &p, k, &c);
        segs += c.geo[0].icount / 6;
    }
    CHECK(segs == envelopes * 72, "greybox: envelope every 50 m with 12 m ring spacing (%d circles)", segs / 72);
    path_free(&p);
    env.unload(&env);
}

// Environment #4 (crystal field): open void without floor or walls, no roll, gentle pitch.
static void test_crystal(void)
{
    Environment env = env_crystal_create();
    ParamRegistry reg;
    params_clear(&reg);
    env.registerParams(&env, &reg);
    env.init(&env, 17, NULL);
    PathStyle st = env.pathStyle(&env);
    CHECK(!st.hasWalls && st.floorDepth == 0.0f && st.ceilingHeight == 0.0f && st.twistRateMax == 0.0f,
          "crystal style is open, without floor, ceiling or twist");
    double worstPitch = 0.0, worstRoll = 0.0, maxCurv = 0.0;
    for (int seed = 1; seed <= 5; seed++) {
        PathSystem p;
        path_init(&p, (uint64_t)seed * 7919u, st);
        for (double s = 0.0; s <= 20000.0; s += PATH_DS) {
            path_ensure(&p, s - 100.0, s + 50.0);
            PathSample q = path_sample(&p, s);
            double pitch = fabs(asin(q.T.y)) * 180.0 / 3.14159265358979;
            if (pitch > worstPitch) worstPitch = pitch;
            if (fabs(q.roll) > worstRoll) worstRoll = fabs(q.roll);
            if (q.curvature > maxCurv) maxCurv = q.curvature;
        }
        path_free(&p);
    }
    printf("  crystal path (5 seeds x 20000 units): max pitch %.1f deg (style %.1f), max roll %.2e, maxCurv %.5f (limit %.5f)\n",
           worstPitch, st.maxPitchDeg, worstRoll, maxCurv, path_curvature_limit(&st));
    CHECK(worstPitch <= st.maxPitchDeg * 1.25, "crystal path pitch within the style limit (%.1f deg)", worstPitch);
    CHECK(worstRoll == 0.0, "crystal path has no roll");
    CHECK(maxCurv <= path_curvature_limit(&st), "crystal curvature within limit");

    enum { N = 60 };
    uint64_t h[N];
    CHECK(build_order_independent(&env, 17, N, h), "crystal chunk build independent of order (%d chunks)", N);
    env.unload(&env);
}

// Distance from p to the segment ab.
static double seg_distance(DVec3 p, DVec3 a, DVec3 b)
{
    DVec3 ab = dv_sub(b, a);
    double l2 = dv_dot(ab, ab), t = l2 > 0.0 ? dv_dot(dv_sub(p, a), ab) / l2 : 0.0;
    t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
    return dv_len(dv_sub(p, dv_madd(a, ab, t)));
}

// Crystal field: solid topology, no shape inside the steering box keep-out at any arc length,
// build time and vertex budget.
static void test_crystal_field(void)
{
    static const int edges[CF_KIND_COUNT] = { 6, 12, 30, 12, 9, 18 };
    static const int polys[CF_KIND_COUNT] = { 4, 8, 20, 6, 5, 12 };
    bool topo = true;
    for (int k = 0; k < CF_KIND_COUNT; k++) {
        const ShapeSolid *so = crystal_solid(k);
        // Euler with polygon faces: F = E - V + 2. An n-sided polygon is n - 2 triangles and the
        // sides of all polygons add up to 2E, so the triangle count is 2E - 2F.
        if (so->ne != edges[k] || so->ne - so->nv + 2 != polys[k] || so->nf != 2 * so->ne - 2 * polys[k]) topo = false;
        float rmax = 0.0f;
        for (int i = 0; i < so->nv; i++) rmax = fmaxf(rmax, sqrtf(so->v[i].x * so->v[i].x + so->v[i].y * so->v[i].y + so->v[i].z * so->v[i].z));
        if (fabsf(rmax - 1.0f) > 1e-5f) topo = false;
    }
    CHECK(topo, "crystal solids: edge counts and Euler characteristic, circumradius 1");

    Environment env = env_crystal_create();
    ParamRegistry reg;
    params_clear(&reg);
    env.registerParams(&env, &reg);
    env.init(&env, 41, NULL);
    PathStyle st = env.pathStyle(&env);
    CrystalFieldParams f = { 0 };
    f.laneRadius = st.baseRadius;
    f.steerLimit = 0.8f;
    f.clearance = params_get(params_find(&reg, "cfClearance"));
    f.bandMax = params_get(params_find(&reg, "cfBandMax"));
    f.clusters = params_get(params_find(&reg, "cfClusters"));
    f.scatter = params_get(params_find(&reg, "cfScatter"));
    f.clusterMin = (int)params_get(params_find(&reg, "cfClusterMin"));
    f.clusterMax = (int)params_get(params_find(&reg, "cfClusterMax"));
    f.clusterRadMin = params_get(params_find(&reg, "cfClusterRadMin"));
    f.clusterRadMax = params_get(params_find(&reg, "cfClusterRadMax"));
    f.sideBias = params_get(params_find(&reg, "cfSideBias"));
    f.nearBias = params_get(params_find(&reg, "cfNearBias"));
    f.giantFrac = params_get(params_find(&reg, "cfGiantFrac"));
    f.mediumFrac = params_get(params_find(&reg, "cfMediumFrac"));
    f.smallMin = 0.5f; f.smallMax = 2.0f; f.mediumMin = 2.0f; f.mediumMax = 6.0f; f.giantMin = 8.0f; f.giantMax = 16.0f;
    f.edgeIntensity = 2.5f;
    double keep = crystal_keepout(&f), worst = 1e9, reach = crystal_path_reach(&f);
    CHECK(reach <= env.pathReach, "crystal placement reach %.0f within pathReach %.0f", reach, env.pathReach);
    static CrystalShape sh[CF_MAX_SHAPES];
    int total = 0, maxN = 0, giants = 0;
    for (int seed = 1; seed <= 3; seed++) {
        PathSystem p;
        path_init(&p, (uint64_t)seed * 104729u, st);
        for (int k = 0; k < 80; k++) {
            double s0 = k * CHUNK_LEN, s1 = s0 + CHUNK_LEN;
            path_ensure(&p, fmax(0.0, s0 - env.pathReach), s1 + env.pathReach);
            int n = crystal_field_plan(&f, &p, ct_hash64_2((uint64_t)seed, (uint64_t)k), s0, s1, sh, CF_MAX_SHAPES);
            total += n;
            if (n > maxN) maxN = n;
            for (int i = 0; i < n; i++) {
                if (sh[i].size >= f.giantMin) giants++;
                double d = 1e300;
                DVec3 a = path_sample(&p, fmax(0.0, s0 - 400.0)).pos;
                for (double s = fmax(0.0, s0 - 400.0) + PATH_DS; s <= s1 + 400.0; s += PATH_DS) {
                    DVec3 b = path_sample(&p, s).pos;
                    d = fmin(d, seg_distance(sh[i].pos, a, b));
                    a = b;
                }
                if (d - sh[i].bound - keep < worst) worst = d - sh[i].bound - keep;
            }
        }
        path_free(&p);
    }
    printf("  crystal field (3 seeds x 80 chunks): %d shapes, %.1f per chunk (max %d), %d giants; keep-out %.2f, worst margin %.3f\n",
           total, total / 240.0, maxN, giants, keep, worst);
    CHECK(worst >= -1e-6, "no crystal inside the steering box keep-out (worst margin %.3f)", worst);
    CHECK(maxN < CF_MAX_SHAPES, "crystal shapes per chunk below the cap (%d)", maxN);

    // Full chunk builds: time and vertex budget.
    PathSystem p;
    path_init(&p, 41, st);
    static ChunkCpu c;
    double worstMs = 0.0;
    int maxVerts = 0;
    for (int k = 0; k < 60; k++) {
        ensure_for_chunk(&p, &env, k);
        chunk_cpu_begin(&c, &p, k);
        double t0 = build_timer_seconds();
        env.buildChunk(&env, &p, k, &c);
        double ms = (build_timer_seconds() - t0) * 1000.0;
        if (ms > worstMs) worstMs = ms;
        int v = 0;
        for (int b = 0; b < CHUNK_BATCHES; b++) v += c.geo[b].vcount;
        if (v > maxVerts) maxVerts = v;
    }
    printf("  crystal chunks: worst build %.2f ms, max %d vertices\n", worstMs, maxVerts);
    CHECK(worstMs < 3.0, "crystal chunk build < 3 ms (%.2f ms)", worstMs);
    CHECK(maxVerts <= 60000, "crystal chunk <= 60k vertices (%d)", maxVerts);
    for (int b = 0; b < CHUNK_BATCHES; b++) lg_free(&c.geo[b]);
    path_free(&p);
    env.unload(&env);
}

static void test_corridor(void)
{
    Environment env = env_corridor_create();
    ParamRegistry reg;
    params_clear(&reg);
    env.registerParams(&env, &reg);
    env.init(&env, 13, NULL);
    PathStyle st = env.pathStyle(&env);
    CHECK(!st.hasWalls && st.floorDepth > 0.0f && st.ceilingHeight > 0.0f, "corridor style is open with floor and ceiling");
    double worstAxis = 0.0, worstY = 0.0, minUp = 1.0, maxCurv = 0.0;
    for (int seed = 1; seed <= 5; seed++) {
        PathSystem p;
        path_init(&p, (uint64_t)seed * 7919u, st);
        for (double s = 0.0; s <= 20000.0; s += PATH_DS) {
            path_ensure(&p, s - 100.0, s + 50.0);
            PathSample q = path_sample(&p, s);
            if (fabs(q.pos.y) > worstY) worstY = fabs(q.pos.y);
            if (q.N.y < minUp) minUp = q.N.y;
            if (q.curvature > maxCurv) maxCurv = q.curvature;
            double yawDeg = atan2(q.T.x, -q.T.z) * 180.0 / 3.14159265358979;
            double err = fabs(yawDeg - 90.0 * floor(yawDeg / 90.0 + 0.5));
            if (err > worstAxis) worstAxis = err;
        }
        path_free(&p);
    }
    printf("  corridor path (5 seeds x 20000 units): max |y| %.2e, min N.y %.9f, max heading off-axis %.1f deg, maxCurv %.5f (limit %.5f)\n",
           worstY, minUp, worstAxis, maxCurv, path_curvature_limit(&st));
    CHECK(worstY < 1e-9, "corridor path stays level (max |y| %g)", worstY);
    CHECK(minUp > 0.999999, "corridor path N is world up (min N.y %.9f)", minUp);
    CHECK(worstAxis <= 12.0, "corridor heading within 12 deg of a grid axis (%.1f deg)", worstAxis);
    CHECK(maxCurv <= path_curvature_limit(&st), "corridor curvature within limit");
    env.unload(&env);
}

static int pclaim_cmp(const void *a, const void *b)
{
    const PillarClaim *x = (const PillarClaim *)a, *y = (const PillarClaim *)b;
    if (x->ix != y->ix) return x->ix < y->ix ? -1 : 1;
    if (x->iz != y->iz) return x->iz < y->iz ? -1 : 1;
    return 0;
}

// Corridor pillars: every lattice cell near the path has exactly one owner, no pillar enters
// the lane + clearance, merged pairs never overlap a neighbour, builds stay inside the budget.
static void test_corridor_pillars(void)
{
    Environment env = env_corridor_create();
    ParamRegistry reg;
    params_clear(&reg);
    env.registerParams(&env, &reg);
    env.init(&env, 29, NULL);
    PathStyle st = env.pathStyle(&env);
    PillarParams pp = { 0 };
    pp.cell = params_get(params_find(&reg, "corGridCell"));
    pp.latticeCells = (int)params_get(params_find(&reg, "corPillarLatticeCells"));
    pp.laneHalfWidth = st.baseRadius;
    pp.clearance = params_get(params_find(&reg, "corPillarClearance"));
    pp.wallBand = params_get(params_find(&reg, "corWallBand"));
    pp.bandMax = params_get(params_find(&reg, "corPillarBandMax"));
    pp.wallDensity = params_get(params_find(&reg, "corWallDensity"));
    pp.backDensity = params_get(params_find(&reg, "corBackDensity"));
    pp.backRamp = params_get(params_find(&reg, "corBackRamp"));
    pp.mergeChance = params_get(params_find(&reg, "corMergeChance"));
    pp.widthMin = params_get(params_find(&reg, "corPillarWidthMin"));
    pp.widthMax = params_get(params_find(&reg, "corPillarWidthMax"));
    pp.depthMin = params_get(params_find(&reg, "corPillarDepthMin"));
    pp.depthMax = params_get(params_find(&reg, "corPillarDepthMax"));
    pp.heightMin = params_get(params_find(&reg, "corPillarHeightMin"));
    pp.heightMax = params_get(params_find(&reg, "corPillarHeightMax"));
    pp.backHeightMul = params_get(params_find(&reg, "corBackHeightMul"));
    pp.stepsMin = (int)params_get(params_find(&reg, "corStepsMin"));
    pp.stepsMax = (int)params_get(params_find(&reg, "corStepsMax"));
    pp.stepMin = params_get(params_find(&reg, "corStepMin"));
    pp.stepMax = params_get(params_find(&reg, "corStepMax"));
    pp.slackMax = params_get(params_find(&reg, "corWallSlack"));
    pp.outlineChance = params_get(params_find(&reg, "corOutlineChance"));
    pp.floorY = -st.floorDepth;
    double reach = pillars_reach(&pp);

    enum { N = 200, MAXC = 200000, MAXP = 40000 };
    PathSystem p;
    path_init(&p, 29, st);
    static PillarClaim all[MAXC], chunkClaims[8192];
    static PillarDesc pillars[COR_MAX_PILLARS], allP[MAXP];
    static LineGeo body;
    int nAll = 0, nP = 0, nWall = 0, nMerged = 0, nOutline = 0, ownerOk = 1, laneOk = 1, stepped = 0;
    double worstMs = 0.0, minGap = 1e9;
    float inner = pp.laneHalfWidth + pp.clearance;
    double L = pp.latticeCells * (double)pp.cell;
    for (int k = 0; k < N; k++) {
        int nc = 0;
        path_ensure(&p, k * CHUNK_LEN - 100.0 - reach, (k + 1) * CHUNK_LEN + 100.0 + reach);
        double c0 = build_timer_seconds();
        int n = pillars_plan(&pp, NULL, 29, &p, k, pillars, COR_MAX_PILLARS, chunkClaims, 8192, &nc);
        lg_clear(&body);
        pillars_build(pillars, n, path_sample(&p, k * CHUNK_LEN).pos, &body);
        double ms = (build_timer_seconds() - c0) * 1000.0;
        if (ms > worstMs) worstMs = ms;
        for (int i = 0; i < nc && nAll < MAXC; i++) {
            if (chunk_index_at(chunkClaims[i].sOwner) != k) ownerOk = 0;
            all[nAll++] = chunkClaims[i];
        }
        for (int i = 0; i < n; i++) {
            const PillarDesc *d = &pillars[i];
            if (nP < MAXP) allP[nP++] = *d;
            nWall += d->wall;
            nOutline += d->outline;
            if (d->a1 - d->a0 > L) nMerged++;
            for (int t = 1; t < d->tiers; t++) if (d->cLane[t] != d->cLane[t - 1]) { stepped++; break; }
            double x0, z0, x1, z1;
            pillar_bounds(d, &x0, &z0, &x1, &z1);
            for (double s = fmax(0.0, d->sOwner - 200.0); s <= d->sOwner + 200.0; s += 0.5) {
                DVec3 q = path_sample(&p, s).pos;
                double dx = q.x < x0 ? x0 - q.x : (q.x > x1 ? q.x - x1 : 0.0);
                double dz = q.z < z0 ? z0 - q.z : (q.z > z1 ? q.z - z1 : 0.0);
                double dd = sqrt(dx * dx + dz * dz);
                if (dd < minGap) minGap = dd;
                if (dd < inner - 0.05) laneOk = 0;
            }
        }
    }
    qsort(all, (size_t)nAll, sizeof(PillarClaim), pclaim_cmp);
    int dups = 0;
    for (int i = 1; i < nAll; i++) if (!pclaim_cmp(&all[i - 1], &all[i])) dups++;
    // No two pillars overlap (merged slabs reach into their partner cell, which must stay empty).
    int overlaps = 0;
    for (int i = 0; i < nP; i++) {
        double ax0, az0, ax1, az1;
        pillar_bounds(&allP[i], &ax0, &az0, &ax1, &az1);
        for (int j = i + 1; j < nP && j < i + 400; j++) {
            double bx0, bz0, bx1, bz1;
            pillar_bounds(&allP[j], &bx0, &bz0, &bx1, &bz1);
            if (ax0 < bx1 - 1e-9 && bx0 < ax1 - 1e-9 && az0 < bz1 - 1e-9 && bz0 < az1 - 1e-9) overlaps++;
        }
    }
    printf("  corridor pillars: %d chunks, %d pillars (%d wall, %d merged, %d stepped, %d outlined), %d claimed cells; worst build %.2f ms; min lane gap %.2f (need %.1f)\n",
           N, nP, nWall, nMerged, stepped, nOutline, nAll, worstMs, minGap, inner);
    CHECK(nWall > N * 2 && nP > nWall, "wall and back pillars placed (%d wall of %d)", nWall, nP);
    CHECK(nMerged > 0 && stepped > nWall / 2, "merged slabs and stepped profiles occur (%d, %d)", nMerged, stepped);
    CHECK(ownerOk, "every pillar claim lies in its own chunk");
    CHECK(dups == 0, "no pillar cell claimed by two chunks (%d duplicates)", dups);
    CHECK(overlaps == 0, "no overlapping pillars (%d)", overlaps);
    CHECK(laneOk, "no pillar inside lane + clearance (min gap %.3f)", minGap);
    CHECK(worstMs < 3.0, "chunk pillar build < 3 ms (%.2f ms)", worstMs);

    // Brute-force ownership: every cell near the interior of the path is claimed by its owner.
    int nSeg = (int)((N * CHUNK_LEN + reach) / 2.0);
    double *px = (double *)malloc(sizeof(double) * (size_t)(nSeg + 1)), *pz = (double *)malloc(sizeof(double) * (size_t)(nSeg + 1));
    double minx = 1e300, maxx = -1e300, minz = 1e300, maxz = -1e300;
    path_reset(&p, 29, st);
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
    int checked = 0, missing = 0, wrongOwner = 0;
    for (int iz = (int)floor((minz - pp.bandMax) / L); iz <= (int)floor((maxz + pp.bandMax) / L); iz++) {
        for (int ix = (int)floor((minx - pp.bandMax) / L); ix <= (int)floor((maxx + pp.bandMax) / L); ix++) {
            double cx = (ix + 0.5) * L, cz = (iz + 0.5) * L, best = 1e300, bs = 0.0;
            for (int i = 0; i < nSeg; i++) {
                double dx = px[i + 1] - px[i], dz = pz[i + 1] - pz[i], l2 = dx * dx + dz * dz;
                double t = l2 > 0.0 ? ((cx - px[i]) * dx + (cz - pz[i]) * dz) / l2 : 0.0;
                t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
                double ex = px[i] + dx * t - cx, ez = pz[i] + dz * t - cz, d2 = ex * ex + ez * ez;
                if (d2 < best) { best = d2; bs = (i + t) * 2.0; }
            }
            int owner = chunk_index_at(bs);
            if (sqrt(best) > pp.bandMax - 0.01 || owner < 1 || owner > N - 2) continue;
            checked++;
            PillarClaim key = { ix, iz, 0.0, 0 };
            const PillarClaim *c = (const PillarClaim *)bsearch(&key, all, (size_t)nAll, sizeof(PillarClaim), pclaim_cmp);
            if (!c) missing++;
            else if (chunk_index_at(c->sOwner) != owner) wrongOwner++;
        }
    }
    free(px); free(pz);
    CHECK(checked > 1000 && missing == 0, "every pillar cell in the band claimed (%d of %d missing)", missing, checked);
    CHECK(wrongOwner == 0, "pillar cells claimed by the brute-force owner (%d wrong)", wrongOwner);

    static uint64_t h[N];
    CHECK(build_order_independent(&env, 29, N, h), "corridor chunk build independent of order (%d chunks)", N);
    lg_free(&body);
    path_free(&p);
    env.unload(&env);
}

// Full corridor chunks: outlines and kerbs stay outside the lane (the ship's lateral range is
// at most steerLimit x lane half-width), vertex budget, build time.
static void test_corridor_decor(void)
{
    Environment env = env_corridor_create();
    ParamRegistry reg;
    params_clear(&reg);
    env.registerParams(&env, &reg);
    env.init(&env, 37, NULL);
    PathStyle st = env.pathStyle(&env);
    PathSystem p;
    path_init(&p, 37, st);
    static ChunkCpu c;
    enum { N = 200, B_RAIN = 1, B_OUTLINE = 2, B_KERB = 3 };
    int maxVerts = 0, rainQuads = 0, outlineVerts = 0, kerbs = 0, props = 0, propsOut = 1, rings = 0, ringsOut = 1, arch[N];
    double worstMs = 0.0, minLat = 1e9;
    for (int k = 0; k < N; k++) {
        ensure_for_chunk(&p, &env, k);
        double c0 = build_timer_seconds();
        chunk_cpu_begin(&c, &p, k);
        env.buildChunk(&env, &p, k, &c);
        double ms = (build_timer_seconds() - c0) * 1000.0;
        if (ms > worstMs) worstMs = ms;
        int v = 0;
        for (int b = 0; b < CHUNK_BATCHES; b++) v += c.geo[b].vcount;
        if (v > maxVerts) maxVerts = v;
        rainQuads += c.geo[B_RAIN].vcount / 4;
        arch[k] = c.archetype;
        outlineVerts += c.geo[B_OUTLINE].vcount;
        kerbs += c.geo[B_KERB].vcount / 4;
        // Props: outside the lane, above the floor. Ring clusters: outside the lane or above the
        // ship's ceiling (keeps the play band clear).
        for (int i = 0; i < c.debrisCount; i++) {
            const DebrisInfo *d = &c.debris[i];
            PathSample ps = path_sample(&p, d->s);
            DVec3 rel = dv_sub(d->center, ps.pos);
            double lat = fabs(dv_dot(rel, ps.B)), up = dv_dot(rel, ps.N) + st.floorDepth;
            if (lat - d->radius < st.baseRadius || up - d->radius < 0.0) propsOut = 0;
            props++;
        }
        for (int slot = 0; slot < 16; slot++) {
            const float *rb = c.uniforms[16 + slot];
            if (rb[0] == 0.0f && rb[1] == 0.0f && rb[2] == 0.0f) continue;
            const float *ra = c.uniforms[slot];
            double x = c.origin.x + ra[0], y = c.origin.y + ra[1], z = c.origin.z + ra[2];
            double best = 1e9;
            for (double s = fmax(0.0, c.s0 - 40.0); s <= c.s1 + 40.0; s += 0.5) {
                DVec3 q = path_sample(&p, s).pos;
                double d = sqrt((q.x - x) * (q.x - x) + (q.z - z) * (q.z - z));
                if (d < best) best = d;
            }
            double rMax = params_get(params_find(&reg, "corRingRadiusMax"));
            double tilt = params_get(params_find(&reg, "corRingTilt")) * 3.14159265 / 180.0;
            if (best - rMax < st.baseRadius && y - rMax * sin(tilt) - 0.05 < st.ceilingHeight) ringsOut = 0;
            rings++;
        }
        const int bs[2] = { B_OUTLINE, B_KERB };
        for (int bi = 0; bi < 2; bi++) {
            const LineGeo *g = &c.geo[bs[bi]];
            for (int i = 0; i < g->vcount; i++) {
                double x = c.origin.x + g->pos[i * 3], z = c.origin.z + g->pos[i * 3 + 2];
                for (double s = fmax(0.0, c.s0 - 80.0); s <= c.s1 + 80.0; s += 0.5) {
                    DVec3 q = path_sample(&p, s).pos;
                    double d = sqrt((q.x - x) * (q.x - x) + (q.z - z) * (q.z - z));
                    if (d < minLat) minLat = d;
                }
            }
        }
    }
    printf("  corridor chunks: max %d verts/chunk, %d rain panels, %d outline verts, %d kerbs over %d chunks; worst full build %.2f ms; outline/kerb min distance to path %.2f (lane %.1f)\n",
           maxVerts, rainQuads, outlineVerts, kerbs, N, worstMs, minLat, st.baseRadius);
    CHECK(rainQuads > N * 10 && outlineVerts > 0 && kerbs > 0, "code rain, outlines and kerbs generated");
    CHECK(minLat >= st.baseRadius, "outlines and kerbs stay outside the lane (%.2f >= %.1f)", minLat, st.baseRadius);
    // Pacing: starts in a colonnade, every archetype appears, runs within bounds
    // (COLONNADE 4-9, SERVER HALL 3-6, ATRIUM 2-4; the last run may be cut off).
    static const int rmin[3] = { 4, 3, 2 }, rmax[3] = { 9, 6, 4 };
    int runsOk = 1, runs = 0, seen[3] = { 0 };
    for (int k = 0; k < N;) {
        int e = k;
        while (e < N && arch[e] == arch[k]) e++;
        int len = e - k, a = arch[k];
        if (a < 0 || a > 2 || len > rmax[a] || (e < N && len < rmin[a])) runsOk = 0;
        else seen[a] += len;
        runs++;
        k = e;
    }
    printf("  corridor pacing: %d runs over %d chunks; COLONNADE %d SERVER HALL %d ATRIUM %d chunks\n", runs, N, seen[0], seen[1], seen[2]);
    CHECK(arch[0] == 0, "corridor flight starts in a COLONNADE");
    CHECK(seen[0] && seen[1] && seen[2], "all three corridor archetypes appear");
    CHECK(runsOk, "corridor archetype runs within their length bounds");
    printf("  corridor props %d, ring clusters %d\n", props, rings);
    CHECK(props > 0 && propsOut, "corridor props outside the lane, above the floor");
    CHECK(rings > 0 && ringsOut, "ring clusters outside the lane or above the ship's ceiling");
    CHECK(maxVerts <= 60000, "corridor chunk vertex budget (%d <= 60000)", maxVerts);
    CHECK(worstMs < 3.0, "full corridor chunk build < 3 ms (%.2f ms)", worstMs);
    for (int b = 0; b < CHUNK_BATCHES; b++) lg_free(&c.geo[b]);
    path_free(&p);
    env.unload(&env);
}

typedef struct BeamKey { int32_t ix, iz, dir; int cap0, cap1; } BeamKey;
static int bkey_cmp(const void *a, const void *b)
{
    const BeamKey *x = (const BeamKey *)a, *y = (const BeamKey *)b;
    if (x->dir != y->dir) return x->dir < y->dir ? -1 : 1;
    if (x->ix != y->ix) return x->ix < y->ix ? -1 : 1;
    if (x->iz != y->iz) return x->iz < y->iz ? -1 : 1;
    return 0;
}
static int cell_cmp(const void *a, const void *b)
{
    const int32_t *x = (const int32_t *)a, *y = (const int32_t *)b;
    if (x[0] != y[0]) return x[0] < y[0] ? -1 : 1;
    if (x[1] != y[1]) return x[1] < y[1] ? -1 : 1;
    return 0;
}

// Corridor beams: cells owned once; a segment end is capped exactly when the next cell on its
// line has no segment (built by whichever chunk owns it), so beams join across chunks.
static void test_corridor_beams(void)
{
    Environment env = env_corridor_create();
    ParamRegistry reg;
    params_clear(&reg);
    env.registerParams(&env, &reg);
    PathStyle st = env.pathStyle(&env);
    BeamParams bp = { 0 };
    bp.cell = params_get(params_find(&reg, "corBeamCell"));
    bp.bandMax = params_get(params_find(&reg, "corBeamBandMax"));
    bp.chance = params_get(params_find(&reg, "corBeamChance"));
    bp.gapChance = params_get(params_find(&reg, "corBeamGap"));
    bp.widthMin = params_get(params_find(&reg, "corBeamWidthMin"));
    bp.widthMax = params_get(params_find(&reg, "corBeamWidthMax"));
    bp.thickMin = params_get(params_find(&reg, "corBeamThickMin"));
    bp.thickMax = params_get(params_find(&reg, "corBeamThickMax"));
    bp.heightMin = params_get(params_find(&reg, "corBeamHeight"));
    bp.levels = (int)params_get(params_find(&reg, "corBeamLevels"));
    bp.laneKeep = params_get(params_find(&reg, "corBeamLaneKeep"));
    bp.floorY = -st.floorDepth;
    double reach = beams_reach(&bp);
    enum { N = 200, MAXB = 40000, MAXC = 40000 };
    static BeamDesc beams[COR_MAX_BEAMS];
    static BeamKey keys[MAXB];
    static int32_t cells[MAXC][2], chunkCells[2048][2];
    int nk = 0, ncells = 0, overLane = 0;
    double worstMs = 0.0, minUnder = 1e9;
    PathSystem p;
    path_init(&p, 41, st);
    for (int k = 0; k < N; k++) {
        path_ensure(&p, k * CHUNK_LEN - 100.0 - reach, (k + 1) * CHUNK_LEN + 100.0 + reach);
        int nc = 0;
        double c0 = build_timer_seconds();
        int n = beams_plan(&bp, 41, &p, k, beams, COR_MAX_BEAMS, chunkCells, 2048, &nc);
        double ms = (build_timer_seconds() - c0) * 1000.0;
        if (ms > worstMs) worstMs = ms;
        for (int i = 0; i < nc && ncells < MAXC; i++) { cells[ncells][0] = chunkCells[i][0]; cells[ncells][1] = chunkCells[i][1]; ncells++; }
        for (int i = 0; i < n && nk < MAXB; i++) {
            keys[nk++] = (BeamKey){ beams[i].ix, beams[i].iz, beams[i].dir, beams[i].cap0, beams[i].cap1 };
            if (beams[i].yBottom - (-st.floorDepth) < minUnder) minUnder = beams[i].yBottom - (-st.floorDepth);
            // Along-lane beams never roof the lane.
            PathSample q = path_sample(&p, beams[i].sOwner);
            int laneZ = fabs(q.T.z) >= fabs(q.T.x);
            if ((beams[i].dir == 1) == laneZ) {
                double off = beams[i].dir == 1 ? fabs(beams[i].c - q.pos.x) : fabs(beams[i].c - q.pos.z);
                if (off - beams[i].width * 0.5 < bp.laneKeep - 1e-6) overLane++;
            }
        }
    }
    qsort(cells, (size_t)ncells, sizeof(cells[0]), cell_cmp);
    int dupCells = 0;
    for (int i = 1; i < ncells; i++) if (!cell_cmp(cells[i - 1], cells[i])) dupCells++;
    qsort(keys, (size_t)nk, sizeof(BeamKey), bkey_cmp);
    int dupSeg = 0, badJoin = 0, joins = 0, caps = 0;
    for (int i = 1; i < nk; i++) if (!bkey_cmp(&keys[i - 1], &keys[i])) dupSeg++;
    for (int i = 0; i < nk; i++) {
        const BeamKey *b = &keys[i];
        for (int e = 0; e < 2; e++) {
            int d = e ? 1 : -1;
            BeamKey nb = { b->ix + (b->dir == 0 ? d : 0), b->iz + (b->dir == 1 ? d : 0), b->dir, 0, 0 };
            int32_t nc[2] = { nb.ix, nb.iz };
            // Only judge neighbours whose cell was claimed (inside the streamed range).
            if (!bsearch(nc, cells, (size_t)ncells, sizeof(cells[0]), cell_cmp)) continue;
            int exists = bsearch(&nb, keys, (size_t)nk, sizeof(BeamKey), bkey_cmp) != NULL;
            int cap = e ? b->cap1 : b->cap0;
            if (cap == exists) badJoin++;
            if (exists) joins++; else caps++;
        }
    }
    printf("  corridor beams: %d segments in %d cells over %d chunks, %d joins, %d caps; worst plan %.2f ms; lowest underside %.1f above the floor (ship ceiling %.1f)\n",
           nk, ncells, N, joins, caps, worstMs, minUnder, st.ceilingHeight + st.floorDepth);
    CHECK(nk > N, "beam segments placed (%d)", nk);
    CHECK(dupCells == 0 && dupSeg == 0, "beam cells owned once (%d / %d duplicates)", dupCells, dupSeg);
    CHECK(badJoin == 0 && joins > 0, "beams continue across cells and chunks; caps only at real ends (%d wrong)", badJoin);
    CHECK(overLane == 0, "no along-lane beam above the lane (%d)", overLane);
    CHECK(minUnder > st.ceilingHeight + st.floorDepth, "beams above the ship's ceiling (%.1f)", minUnder);
    CHECK(worstMs < 3.0, "beam plan < 3 ms (%.2f)", worstMs);
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
    test_tunnel_detail();
    test_flight_framerate();
    test_city();
    test_city_towers();
    test_city_decor();
    test_corridor();
    test_corridor_pillars();
    test_corridor_decor();
    test_corridor_beams();
    test_greybox();
    test_crystal();
    test_crystal_field();
    test_params();
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
