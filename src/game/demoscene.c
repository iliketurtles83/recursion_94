#include "demoscene.h"
#include "palette.h"
#include "titles.h"
#include "timeline.h"

#include <math.h>

static uint32_t DemoHash(uint32_t value) {
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

static float DemoHash01(uint32_t value) {
    return (float)(DemoHash(value) & 0x00ffffffu) / 16777215.0f;
}

static unsigned char ByteLerp(unsigned char a, unsigned char b, float amount) {
    return (unsigned char)((float)a + ((float)b - (float)a) * amount);
}

static Color MixColor(Color a, Color b, float amount, unsigned char alpha) {
    if (amount < 0.0f) amount = 0.0f;
    if (amount > 1.0f) amount = 1.0f;
    return (Color){ ByteLerp(a.r, b.r, amount), ByteLerp(a.g, b.g, amount),
                    ByteLerp(a.b, b.b, amount), alpha };
}

void InitDemoscene(DemosceneSystem *demo, uint32_t seed) {
    const PaletteProfile *pal = GetPaletteForSeed(seed);
    demo->seed = seed;
    demo->name = pal->name;
    demo->primary = pal->primary;
    demo->secondary = pal->secondary;
    demo->hot = pal->hot;
    demo->threat = pal->threat;
    demo->shadowBody = pal->shadowBody;
    demo->skyZenith = pal->skyZenith;
    demo->skyHorizon = pal->skyHorizon;
}

static void DrawScanlines(int screenWidth, int screenHeight, unsigned char alpha) {
    for (int y = 1; y < screenHeight; y += 6) {
        DrawRectangle(0, y, screenWidth, 1, (Color){ 0, 0, 8, alpha });
    }
}

void DrawSeedSelector(uint32_t seed, float time, float bpm, const char *keyName,
                      int screenWidth, int screenHeight) {
    DemosceneSystem demo;
    InitDemoscene(&demo, seed);

    for (int y = 0; y < screenHeight; y += 10) {
        float wave = sinf((float)y * 0.026f + time * 0.75f + DemoHash01(seed) * 6.28f) * 0.5f + 0.5f;
        DrawRectangle(0, y, screenWidth, 10,
                      MixColor((Color){ 2, 4, 13, 255 }, demo.secondary, wave, 18));
    }

    for (int i = 0; i < 84; i++) {
        uint32_t h = DemoHash(seed + (uint32_t)i * 0x9e3779b9u);
        int x = (int)(h % (uint32_t)screenWidth);
        float fall = fmodf((float)((h >> 12) % (uint32_t)screenHeight) + time * (12.0f + (float)(h & 15u)),
                           (float)screenHeight);
        int size = 1 + (int)((h >> 8) & 1u);
        DrawRectangle(x, (int)fall, size, size, MixColor(demo.primary, demo.hot, DemoHash01(h), 150));
    }

    int centerX = screenWidth / 2;
    int centerY = screenHeight / 2 - 16;
    for (int i = 0; i < 13; i++) {
        float travel = fmodf(time * 92.0f + (float)i * 54.0f, 700.0f);
        float aspect = (float)screenWidth / (float)screenHeight;
        int halfH = 18 + (int)(travel * 0.43f);
        int halfW = 32 + (int)(travel * 0.43f * aspect);
        float fade = 1.0f - travel / 700.0f;
        unsigned char alpha = (unsigned char)(fade * 82.0f);
        DrawRectangleLines(centerX - halfW, centerY - halfH, halfW * 2, halfH * 2,
                           MixColor(demo.primary, demo.secondary, (float)i / 12.0f, alpha));
    }

    const char *title = "RECURSION_94";
    const char *subtitle = "DETERMINISTIC AUDIOVISUAL RUN COMPILER";
    // The title drops in on a fixed 120 BPM grid (8 steps a second): no music plays here.
    double steps = (double)time * 8.0;
    titles_draw_logo(title, (float)centerX - titles_logo_width(title, 54.0f) * 0.5f, 112.0f, 54.0f,
                     steps - 1.0, 0.75f, demo.primary, demo.hot, 1.0f);
    static const TlKey SUBTITLE[] = { { 0, 0, TL_STEP }, { 12, 0, TL_LINEAR }, { 20, 1, TL_STEP } };
    int subLen = TextLength(subtitle);
    int shown = (int)(tl_eval(TL_TRACK(SUBTITLE), steps) * (float)subLen + 0.999f);
    DrawText(TextSubtext(subtitle, 0, shown), centerX - MeasureText(subtitle, 17) / 2, 174, 17,
             (Color){ 150, 190, 215, 255 });

    DrawRectangle(centerX - 275, centerY - 95, 550, 192,
                  (Color){ demo.shadowBody.r, demo.shadowBody.g, demo.shadowBody.b, 235 });
    DrawRectangleLines(centerX - 275, centerY - 95, 550, 192, demo.secondary);
    const char *seedHex = TextFormat("SEED  %08X", (unsigned int)seed);
    const char *seedDec = TextFormat("DECIMAL  %u", (unsigned int)seed);
    DrawText(seedHex, centerX - MeasureText(seedHex, 38) / 2, centerY - 72, 38, demo.hot);
    DrawText(seedDec, centerX - MeasureText(seedDec, 15) / 2, centerY - 25, 15,
             (Color){ 170, 205, 225, 255 });
    const char *profileText = TextFormat("PALETTE // %s", demo.name);
    DrawText(profileText, centerX - MeasureText(profileText, 16) / 2, centerY + 3, 16, demo.secondary);
    const char *music = TextFormat("TONALITY  %s     TEMPO  %.0f BPM", keyName, bpm);
    DrawText(music, centerX - MeasureText(music, 16) / 2, centerY + 35, 16, demo.primary);

    const char *adjust = "LEFT / RIGHT  +/- 1        UP / DOWN  +/- 100";
    const char *start = "PRESS ENTER TO COMPILE RUN";
    DrawText(adjust, centerX - MeasureText(adjust, 18) / 2, screenHeight - 120, 18,
             (Color){ 150, 185, 210, 255 });
    DrawText(start, centerX - MeasureText(start, 24) / 2, screenHeight - 78, 24, demo.secondary);
    DrawScanlines(screenWidth, screenHeight, 42);
}
