// Debug panel (F1) driven by the parameter registry, plus the stats overlay.
#include "ct_internal.h"
#include <stdio.h>
#include <string.h>

#define RAYGUI_IMPLEMENTATION
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-result"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wenum-compare"
#endif
#include "raygui.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#define PANEL_W   420
#define ROW_H     20
#define LABEL_W   150
#define SLIDER_W  170

// Groups whose params change chunk content (rebuild chunks) or the path (full rebuild).
static bool group_rebuilds(const char *g) { return !strcmp(g, "Rings") || !strcmp(g, "Ribbons") || !strcmp(g, "Debris") || !strcmp(g, "Pacing") || !strcmp(g, "City") || !strcmp(g, "Towers") || !strcmp(g, "Floor"); }
static bool group_rebuilds_path(const char *g) { return !strcmp(g, "Path") || !strcmp(g, "Template"); }

static void style_once(void)
{
    static bool done = false;
    if (done) return;
    done = true;
    GuiSetStyle(DEFAULT, TEXT_SIZE, 10);
    GuiSetStyle(DEFAULT, BACKGROUND_COLOR, 0x0B1220E0);
    GuiSetStyle(DEFAULT, BASE_COLOR_NORMAL, 0x14243AFF);
    GuiSetStyle(DEFAULT, BORDER_COLOR_NORMAL, 0x2E5F7AFF);
    GuiSetStyle(DEFAULT, TEXT_COLOR_NORMAL, 0xBFEFFFFF);
    GuiSetStyle(DEFAULT, BASE_COLOR_FOCUSED, 0x1D3E5CFF);
    GuiSetStyle(DEFAULT, BORDER_COLOR_FOCUSED, 0x4FEAF5FF);
    GuiSetStyle(DEFAULT, TEXT_COLOR_FOCUSED, 0xFFFFFFFF);
    GuiSetStyle(DEFAULT, BASE_COLOR_PRESSED, 0x2B6C8CFF);
    GuiSetStyle(DEFAULT, BORDER_COLOR_PRESSED, 0x8FF4FFFF);
    GuiSetStyle(DEFAULT, TEXT_COLOR_PRESSED, 0xFFFFFFFF);
    GuiSetStyle(DEFAULT, LINE_COLOR, 0x2E5F7AFF);
}

static int count_rows(const CtTunnel *ct)
{
    int rows = 6;
    const char *group = NULL;
    int gi = -1;
    for (int i = 0; i < ct->reg.count; i++) {
        const Param *p = &ct->reg.items[i];
        if (!group || strcmp(group, p->group) != 0) { group = p->group; gi++; rows++; }
        if (gi < 32 && ct->groupOpen[gi]) rows++;
    }
    return rows;
}

void ct_debug_panel(CtTunnel *ct, int width, int height)
{
    style_once();
    Rectangle bounds = { 0, 0, PANEL_W, (float)height };
    int rows = count_rows(ct);
    Rectangle content = { 0, 0, PANEL_W - 16, (float)(rows * (ROW_H + 2) + 20) };
    Rectangle view = { 0 };
    GuiScrollPanel(bounds, NULL, content, &ct->panelScroll, &view);

    BeginScissorMode((int)view.x, (int)view.y, (int)view.width, (int)view.height);
    float x = bounds.x + 8 + ct->panelScroll.x;
    float y = bounds.y + 8 + ct->panelScroll.y;

    GuiLabel((Rectangle){ x, y, 400, ROW_H }, TextFormat("CYBERTUNNEL  env: %s  seed: %llu", ct->env->name, (unsigned long long)ct->seed));
    y += ROW_H + 2;
    if (GuiButton((Rectangle){ x, y, 70, ROW_H }, "Reseed (R)")) ct_reseed(ct, ct->seed + 1);
    if (GuiButton((Rectangle){ x + 74, y, 60, ROW_H }, "Rebuild")) ct_rebuild(ct);
    if (GuiButton((Rectangle){ x + 138, y, 70, ROW_H }, ct->paused ? "Resume (P)" : "Pause (P)")) ct->paused = !ct->paused;
    if (GuiButton((Rectangle){ x + 212, y, 80, ROW_H }, "Whiteout (W)")) ct_trigger_whiteout(ct);
    if (GuiButton((Rectangle){ x + 296, y, 100, ROW_H }, TextFormat("Env: %s", ct->env->name))) ct_set_environment(ct, (ct->envId + 1) % ENV_COUNT);
    y += ROW_H + 2;
    GuiLabel((Rectangle){ x, y, LABEL_W, ROW_H }, "jump to s");
    GuiSliderBar((Rectangle){ x + LABEL_W, y, SLIDER_W, ROW_H }, NULL, NULL, &ct->jumpTarget, 0.0f, 100000.0f);
    if (GuiButton((Rectangle){ x + LABEL_W + SLIDER_W + 6, y, 60, ROW_H }, TextFormat("%.0f", ct->jumpTarget))) ct_jump_to(ct, ct->jumpTarget);
    y += ROW_H + 2;
    GuiLabel((Rectangle){ x, y, 60, ROW_H }, "presets:");
    const char *presets[3] = { "default", "calm", "intense" };
    for (int i = 0; i < 3; i++)
        if (GuiButton((Rectangle){ x + 60 + i * 64, y, 60, ROW_H }, presets[i])) ct_preset_load(ct, presets[i]);
    if (GuiButton((Rectangle){ x + 60 + 3 * 64, y, 80, ROW_H }, "save custom")) ct_preset_save(ct, "custom");
    y += ROW_H + 2;
    if (GuiButton((Rectangle){ x, y, 120, ROW_H }, "reset defaults")) { params_reset_defaults(&ct->reg); ct_rebuild(ct); }
    y += ROW_H + 8;

    const char *group = NULL;
    int gi = -1;
    for (int i = 0; i < ct->reg.count; i++) {
        Param *p = &ct->reg.items[i];
        if (!group || strcmp(group, p->group) != 0) {
            group = p->group;
            gi++;
            bool open = gi < 32 && ct->groupOpen[gi];
            if (GuiButton((Rectangle){ x, y, PANEL_W - 32, ROW_H }, TextFormat("%s %s", open ? "-" : "+", group)) && gi < 32)
                ct->groupOpen[gi] = !ct->groupOpen[gi];
            y += ROW_H + 2;
        }
        if (gi >= 32 || !ct->groupOpen[gi]) continue;
        float before = params_get(p);
        if (p->type == PARAM_BOOL) {
            bool b = before > 0.5f;
            GuiCheckBox((Rectangle){ x + 4, y + 3, 14, 14 }, p->name, &b);
            params_set(p, b ? 1.0f : 0.0f);
        } else {
            float v = before;
            GuiLabel((Rectangle){ x + 4, y, LABEL_W, ROW_H }, p->name);
            GuiSliderBar((Rectangle){ x + LABEL_W, y, SLIDER_W, ROW_H }, NULL, NULL, &v, p->min, p->max);
            GuiLabel((Rectangle){ x + LABEL_W + SLIDER_W + 6, y, 70, ROW_H }, p->type == PARAM_INT ? TextFormat("%d", (int)v) : TextFormat("%.3f", v));
            if (v != before) params_set(p, v);
        }
        if (params_get(p) != before) {
            if (group_rebuilds_path(p->group)) { ct->rebuildPending = true; ct->rebuildPathPending = true; }
            else if (group_rebuilds(p->group)) ct->rebuildPending = true;
        }
        y += ROW_H + 2;
    }
    EndScissorMode();
}

void ct_debug_overlay(CtTunnel *ct, int width, int height)
{
    CtStats s;
    ct_get_stats(ct, &s);
    int x = width - 300, y = 10, lh = 16;
    DrawRectangle(x - 8, y - 6, 300, lh * 14 + 12, (Color){ 0, 0, 0, 150 });
    Color c = (Color){ 180, 240, 255, 255 };
    DrawText(TextFormat("FPS %d   frame %.2f ms", GetFPS(), s.frameMs), x, y, 10, c); y += lh;
    DrawText(TextFormat("cpu: update %.2f  scene %.2f  bloom %.2f  comp %.2f", s.msUpdate, s.msScene, s.msBloom, s.msComposite), x, y, 10, c); y += lh;
    DrawText(TextFormat("chunks %d  build %d  upload %d  meshes %d", s.activeChunks, s.buildsThisFrame, s.uploadsThisFrame, s.gpuMeshes), x, y, 10, c); y += lh;
    DrawText(TextFormat("chunk build last %.2f ms  max %.2f ms", s.lastChunkBuildMs, s.maxChunkBuildMs), x, y, 10, c); y += lh;
    DrawText(TextFormat("verts %d   draw calls %d", s.vertices, s.drawCalls), x, y, 10, c); y += lh;
    DrawText(TextFormat("s %.1f   speed %.1f", s.sShip, s.speed), x, y, 10, c); y += lh;
    DrawText(TextFormat("origin %.0f %.0f %.0f", s.renderOrigin[0], s.renderOrigin[1], s.renderOrigin[2]), x, y, 10, c); y += lh;
    DrawText(TextFormat("max GL coord %.1f", s.maxRenderCoord), x, y, 10, c); y += lh;
    DrawText(TextFormat("intensity %.2f  archetype %s", s.intensity, s.chunkLabel ? s.chunkLabel : ""), x, y, 10, c); y += lh;
    DrawText(TextFormat("env %s  seed %llu%s%s", ct->env->name, (unsigned long long)ct->seed, ct->paused ? "  PAUSED" : "", ct->freeCam ? "  FREECAM" : ""), x, y, 10, c); y += lh;
    DrawText(TextFormat("shader reloads %d", ct->sl.reloadCount), x, y, 10, c); y += lh;
    DrawText("F1 panel F2 freecam F3 overlay F12 shot", x, y, 10, (Color){ 120, 170, 190, 255 }); y += lh;
    DrawText("arrows steer  Shift boost  Q/E roll  T env", x, y, 10, (Color){ 120, 170, 190, 255 }); y += lh;
    DrawText("R reseed  P pause  W whiteout  1-9 layers", x, y, 10, (Color){ 120, 170, 190, 255 });
}
