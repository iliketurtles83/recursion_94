// Sandbox harness: window, input, CLI. Talks to the module only through cybertunnel.h.
#include "cybertunnel.h"
#include "game/game.h"
#include "game/techno.h"
#include "core/resources.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

typedef struct Cli {
    unsigned long long seed;
    int    w, h, vsync, fps, env, missions, startMission;
    double bench, speed;
    double shot;
    bool   hasShot, hdr8, noShip, ui, whiteout, boost, hidden;
    const char *preset;
    const char *shotOut;
    const char *writePreset;
    const char *musicPreview;
    bool   listParams;
    bool   sandbox, autoplay, idleBot, audioCheck, startupCheck;
    double missionShot, transitionShot;
    const char *sets[32];        // --set name=value, applied after --preset
    int    setCount;
} Cli;

static void usage(void)
{
    printf("cybertunnel [--seed N] [--w W --h H] [--vsync 0|1] [--fps N] [--env 0|1|2|3]\n"
           "            [--bench SECONDS [--speed U]] [--shot S [--out FILE]] [--preset NAME] [--write-preset NAME] [--list-params] [--hdr8] [--noship] [--ui] [--whiteout] [--boost]\n"
           "            [--set NAME=VALUE ...] [--hidden] [--sandbox]\n"
           "            [--mission-shot SECONDS --out FILE] [--autoplay] [--bot-idle]\n"
           "            [--music-preview FILE.wav] [--startup-check]\n            [--missions N] [--mission N] [--transition-shot SECONDS --out FILE]\n");
}

static Cli parse(int argc, char **argv)
{
    Cli c = { .seed = 1, .w = 1600, .h = 900, .vsync = 1, .speed = 120.0, .missionShot = -1, .transitionShot = -1, .missions = 1, .startMission = 1 };
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
        else if (!strcmp(a, "--set") && v) { if (c.setCount < 32) c.sets[c.setCount++] = v; i++; }
        else if (!strcmp(a, "--list-params")) c.listParams = true;
        else if (!strcmp(a, "--hdr8")) c.hdr8 = true;
        else if (!strcmp(a, "--noship")) c.noShip = true;
        else if (!strcmp(a, "--ui")) c.ui = true;
        else if (!strcmp(a, "--whiteout")) c.whiteout = true;
        else if (!strcmp(a, "--boost")) c.boost = true;
        else if (!strcmp(a, "--hidden")) c.hidden = true;
        else if (!strcmp(a, "--sandbox")) c.sandbox = true;
        else if (!strcmp(a, "--autoplay")) c.autoplay = true;
        else if (!strcmp(a, "--bot-idle")) c.idleBot = true;
        else if (!strcmp(a, "--audio-check")) c.audioCheck = true;
        else if (!strcmp(a, "--startup-check")) c.startupCheck = true;
        else if (!strcmp(a,"--music-preview")&&v){c.musicPreview=v;i++;}
        else if (!strcmp(a,"--missions")&&v){c.missions=atoi(v);i++;}
        else if (!strcmp(a,"--mission")&&v){c.startMission=atoi(v);i++;}
        else if (!strcmp(a,"--transition-shot")&&v){c.transitionShot=atof(v);i++;}
        else if (!strcmp(a, "--mission-shot") && v) {c.missionShot=atof(v);i++;}
        else if (!strcmp(a, "--help") || !strcmp(a, "-h")) { usage(); exit(0); }
        else { fprintf(stderr, "unknown argument: %s\n", a); usage(); exit(1); }
    }
    if(c.missions<1)c.missions=1;
    if(c.missions>100)c.missions=100;
    if(c.startMission<1)c.startMission=1;
    if(c.startMission>10000)c.startMission=10000;
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
#ifdef _WIN32
        _mkdir("shots");
#else
        mkdir("shots", 0755);
#endif
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
    if(cli.musicPreview)return techno_preview(cli.musicPreview,cli.seed,cli.startMission)?0:1;
    char resourceRoot[1024];
    if(!resources_prepare(resourceRoot,sizeof(resourceRoot))) {
        fprintf(stderr,"DATA-RUN: game resources are unavailable\n");return 2;
    }
    bool bench = cli.bench > 0.0;
    bool mission = !bench && !cli.hasShot && !cli.sandbox && !cli.listParams && !cli.writePreset && (cli.env==CT_ENV_VORTEX||cli.env==CT_ENV_CITY);
    unsigned int flags = FLAG_WINDOW_RESIZABLE;
    if (cli.hidden) flags |= FLAG_WINDOW_HIDDEN;
    if (cli.vsync && !bench && !cli.autoplay && cli.missionShot<0&&cli.transitionShot<0) flags |= FLAG_VSYNC_HINT;
    SetConfigFlags(flags);
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(cli.w, cli.h, mission ? "DATA // RUN - Data Vortex" : "cybertunnel sandbox");
    SetWindowMinSize(800,450);
    SetExitKey(KEY_NULL);
    if (cli.fps > 0) SetTargetFPS(cli.fps);

    CtConfig cfg = ct_default_config();
    cfg.seed = cli.seed;
    cfg.width = GetRenderWidth();
    cfg.height = GetRenderHeight();
    cfg.environment = cli.env;
    cfg.hdr16 = !cli.hdr8;
    cfg.drawShip = !cli.noShip;
    // Packaged assets live beside the executable, independent of the working directory.
    char shaderDir[1200], presetDir[1200], assetDir[1200];
    snprintf(shaderDir, sizeof(shaderDir), "%s/shaders", resourceRoot);
    snprintf(presetDir, sizeof(presetDir), "%s/presets", resourceRoot);
    snprintf(assetDir, sizeof(assetDir), "%s/assets", resourceRoot);
    cfg.shaderDir=shaderDir;cfg.presetDir=presetDir;cfg.assetDir=assetDir;
    CtTunnel *ct = ct_create(&cfg);
    if (mission && !ct_preset_load(ct,cli.env==CT_ENV_CITY?"mission":"mission_tunnel")) {ct_destroy(ct);CloseWindow();return 2;}
    if (cli.preset) ct_preset_load(ct, cli.preset);
    for (int i = 0; i < cli.setCount; i++) {
        char name[128];
        const char *eq = strchr(cli.sets[i], '=');
        size_t n = eq ? (size_t)(eq - cli.sets[i]) : 0;
        if (!eq || n >= sizeof(name)) { fprintf(stderr, "--set expects NAME=VALUE: %s\n", cli.sets[i]); continue; }
        memcpy(name, cli.sets[i], n);
        name[n] = 0;
        if (!ct_param_set(ct, name, (float)atof(eq + 1))) fprintf(stderr, "--set: unknown parameter %s\n", name);
    }
    if (cli.setCount) ct_rebuild(ct);   // path/content params only take effect on rebuild
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
    if (mission) {
        GameOptions options={.hidden=cli.hidden,.shotSeconds=cli.missionShot,.transitionShot=cli.transitionShot,.autoplay=cli.autoplay,.idleBot=cli.idleBot,.audioCheck=cli.audioCheck,.startupCheck=cli.startupCheck,.missions=cli.missions,.startMission=cli.startMission,.shotOut=cli.shotOut};
        int result=game_run(ct,&options);
        ct_destroy(ct);CloseWindow();return result;
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
    double prevStreamMs = 0.0;
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
        if (cli.hasShot && cli.boost) { CtInput in = { 0 }; in.boost = true; ct_set_input(ct, &in); }   // shot held at boost
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
            // Hitch log: dt is the previous frame's duration, so report both frames' work.
            if (dt * 1000.0f > 20.0f)
                printf("bench: hitch %.1f ms at t=%.1f s, s=%.0f; prev frame stream %.2f ms; this frame builds %d uploads %d stream %.2f ms update %.2f scene %.2f\n",
                       dt * 1000.0f, now, s.sShip, prevStreamMs, s.buildsThisFrame, s.uploadsThisFrame, s.lastStreamFrameMs, s.msUpdate, s.msScene);
            prevStreamMs = s.lastStreamFrameMs;
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
