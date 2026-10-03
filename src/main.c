// Sandbox harness: window, input, CLI. Talks to the module only through cybertunnel.h.
#include "cybertunnel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct Cli {
    unsigned long long seed;
    int    w, h, vsync, fps, env;
    double bench, speed;
    double shot;
    bool   hasShot, hdr8, noShip, ui, whiteout;
    const char *preset;
    const char *shotOut;
    const char *writePreset;
    bool   listParams;
} Cli;

static void usage(void)
{
    printf("cybertunnel [--seed N] [--w W --h H] [--vsync 0|1] [--fps N] [--env 0|1|2]\n"
           "            [--bench SECONDS [--speed U]] [--shot S [--out FILE]] [--preset NAME] [--write-preset NAME] [--list-params] [--hdr8] [--noship] [--ui] [--whiteout]\n");
}

static Cli parse(int argc, char **argv)
{
    Cli c = { 1, 1280, 720, 1, 0, 0, 0.0, 120.0, 0.0, false, false, false, false, false, NULL, NULL, NULL, false };
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--seed") && v) { c.seed = strtoull(v, NULL, 10); i++; }
        else if (!strcmp(a, "--w") && v) { c.w = atoi(v); i++; }
        else if (!strcmp(a, "--h") && v) { c.h = atoi(v); i++; }
        else if (!strcmp(a, "--vsync") && v) { c.vsync = atoi(v); i++; }
        else if (!strcmp(a, "--fps") && v) { c.fps = atoi(v); i++; }
        else if (!strcmp(a, "--env") && v) { c.env = atoi(v); i++; }
        else if (!strcmp(a, "--bench") && v) { c.bench = atof(v); i++; }
        else if (!strcmp(a, "--speed") && v) { c.speed = atof(v); i++; }
        else if (!strcmp(a, "--shot") && v) { c.shot = atof(v); c.hasShot = true; i++; }
        else if (!strcmp(a, "--out") && v) { c.shotOut = v; i++; }
        else if (!strcmp(a, "--preset") && v) { c.preset = v; i++; }
        else if (!strcmp(a, "--write-preset") && v) { c.writePreset = v; i++; }
        else if (!strcmp(a, "--list-params")) c.listParams = true;
        else if (!strcmp(a, "--hdr8")) c.hdr8 = true;
        else if (!strcmp(a, "--noship")) c.noShip = true;
        else if (!strcmp(a, "--ui")) c.ui = true;
        else if (!strcmp(a, "--whiteout")) c.whiteout = true;
        else if (!strcmp(a, "--help") || !strcmp(a, "-h")) { usage(); exit(0); }
        else { fprintf(stderr, "unknown argument: %s\n", a); usage(); exit(1); }
    }
    return c;
}

static int cmp_float(const void *a, const void *b)
{
    float x = *(const float *)a, y = *(const float *)b;
    return (x > y) - (x < y);
}

static void save_screenshot(CtTunnel *ct, const char *explicitPath)
{
    char name[256], path[512];
    if (explicitPath) snprintf(path, sizeof(path), "%s", explicitPath);
    else {
        mkdir("shots", 0755);
        ct_screenshot_name(ct, name, sizeof(name));
        snprintf(path, sizeof(path), "shots/%s", name);
    }
    Image img = LoadImageFromScreen();
    ExportImage(img, path);
    UnloadImage(img);
    printf("screenshot: %s\n", path);
}

int main(int argc, char **argv)
{
    Cli cli = parse(argc, argv);
    bool bench = cli.bench > 0.0;
    unsigned int flags = FLAG_WINDOW_RESIZABLE;
    if (cli.vsync && !bench) flags |= FLAG_VSYNC_HINT;
    SetConfigFlags(flags);
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(cli.w, cli.h, "cybertunnel sandbox");
    SetExitKey(KEY_NULL);
    if (cli.fps > 0) SetTargetFPS(cli.fps);

    CtConfig cfg = ct_default_config();
    cfg.seed = cli.seed;
    cfg.width = GetRenderWidth();
    cfg.height = GetRenderHeight();
    cfg.environment = cli.env;
    cfg.hdr16 = !cli.hdr8;
    cfg.drawShip = !cli.noShip;
    CtTunnel *ct = ct_create(&cfg);
    if (cli.preset) ct_preset_load(ct, cli.preset);
    if (cli.listParams) {
        ct_params_write_markdown(ct, stdout);
        ct_destroy(ct);
        CloseWindow();
        return 0;
    }
    if (cli.writePreset) {
        int ok = ct_preset_save(ct, cli.writePreset);
        ct_destroy(ct);
        CloseWindow();
        return ok ? 0 : 1;
    }

    float *frameTimes = NULL;
    int frameCount = 0, frameCap = 0;
    if (bench) {
        ct_param_set(ct, "autopilot", 1.0f);
        ct_param_set(ct, "baseSpeed", (float)cli.speed);
        ct_param_set(ct, "boostSpeed", (float)cli.speed);
        frameCap = (int)(cli.bench * 2000.0) + 1024;
        frameTimes = (float *)malloc(sizeof(float) * (size_t)frameCap);
        printf("bench: %.0f s at speed %.0f, seed %llu, %dx%d\n", cli.bench, cli.speed, cli.seed, cfg.width, cfg.height);
    }
    if (cli.hasShot) ct_jump_to(ct, cli.shot);
    if (cli.ui) ct_debug_show(ct, true, true);

    double start = GetTime();
    double maxHitch = 0.0;
    bool shotTaken = false, whiteoutFired = false, statsReset = false;
    int meshMin = 1 << 30, meshMax = 0;
    int shotFrames = 0;
    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        double now = GetTime() - start;
        // Shot mode runs on a fixed step and frame count so renders are reproducible.
        if (cli.hasShot) { dt = 1.0f / 60.0f; now = shotFrames++ * (double)dt; }

        if (!bench && !cli.hasShot) {
            CtInput in = { 0 };
            in.steerX = (float)(IsKeyDown(KEY_RIGHT) - IsKeyDown(KEY_LEFT));
            in.steerY = (float)(IsKeyDown(KEY_UP) - IsKeyDown(KEY_DOWN));
            in.boost = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_SPACE);
            in.rollLeft = IsKeyPressed(KEY_Q);
            in.rollRight = IsKeyPressed(KEY_E);
            ct_set_input(ct, &in);
            ct_debug_handle_input(ct);
            if (IsKeyPressed(KEY_ESCAPE)) break;
        }

        if (cli.whiteout && !whiteoutFired && now >= 1.25) { ct_trigger_whiteout(ct); whiteoutFired = true; }
        if (bench && !statsReset && now > 1.0) { ct_reset_stats(ct); statsReset = true; }
        ct_update(ct, dt);

        BeginDrawing();
        ct_render(ct, NULL, GetRenderWidth(), GetRenderHeight());
        if (cli.hasShot && cli.ui) ct_debug_draw(ct, GetScreenWidth(), GetScreenHeight());
        if (cli.hasShot && now >= 2.0 && !shotTaken) {
            save_screenshot(ct, cli.shotOut);
            shotTaken = true;
        }
        if (!bench && !cli.hasShot) {
            ct_debug_draw(ct, GetScreenWidth(), GetScreenHeight());
            if (ct_debug_wants_screenshot(ct)) save_screenshot(ct, NULL);
        }
        EndDrawing();

        if (bench && now > 1.0) {
            if (frameCount < frameCap) frameTimes[frameCount++] = dt * 1000.0f;
            CtStats s;
            ct_get_stats(ct, &s);
            if (dt * 1000.0 > maxHitch) maxHitch = dt * 1000.0;
            if (s.gpuMeshes < meshMin) meshMin = s.gpuMeshes;
            if (s.gpuMeshes > meshMax) meshMax = s.gpuMeshes;
        }
        if (bench && now >= cli.bench + 1.0) break;
        if (cli.hasShot && shotTaken) break;
    }

    if (bench && frameCount > 0) {
        double sum = 0.0;
        for (int i = 0; i < frameCount; i++) sum += frameTimes[i];
        qsort(frameTimes, (size_t)frameCount, sizeof(float), cmp_float);
        CtStats s;
        ct_get_stats(ct, &s);
        printf("bench: frames %d  avg %.3f ms  p50 %.3f ms  p99 %.3f ms  max %.3f ms\n", frameCount, sum / frameCount,
               frameTimes[frameCount / 2], frameTimes[(int)(frameCount * 0.99)], frameTimes[frameCount - 1]);
        printf("bench: max chunk build %.3f ms  max streaming per frame %.3f ms  final s %.0f  max GL coord %.1f\n",
               s.maxChunkBuildMs, s.maxStreamFrameMs, s.sShip, s.maxRenderCoord);
        printf("bench: gpu meshes min %d max %d  draw calls %d  verts %d  cpu update %.3f ms scene %.3f ms\n",
               meshMin, meshMax, s.drawCalls, s.vertices, s.msUpdate, s.msScene);
    }
    free(frameTimes);
    ct_destroy(ct);
    CloseWindow();
    return 0;
}
