#include "demoscene.h"

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
    static const Color palettes[][3] = {
        { { 0, 235, 255, 255 }, { 255, 35, 170, 255 }, { 255, 230, 70, 255 } },
        { { 80, 255, 150, 255 }, { 130, 75, 255, 255 }, { 255, 105, 55, 255 } },
        { { 100, 155, 255, 255 }, { 255, 70, 210, 255 }, { 120, 255, 245, 255 } },
        { { 255, 125, 45, 255 }, { 35, 225, 255, 255 }, { 255, 245, 120, 255 } },
        { { 185, 75, 255, 255 }, { 20, 255, 195, 255 }, { 255, 80, 120, 255 } },
        { { 70, 215, 255, 255 }, { 255, 80, 95, 255 }, { 190, 255, 70, 255 } }
    };
    int palette = (int)(DemoHash(seed ^ 0x94d31a7bu) % 6u);
    demo->seed = seed;
    demo->primary = palettes[palette][0];
    demo->secondary = palettes[palette][1];
    demo->hot = palettes[palette][2];
}

static void DrawScanlines(int screenWidth, int screenHeight, unsigned char alpha) {
    for (int y = 1; y < screenHeight; y += 6) {
        DrawRectangle(0, y, screenWidth, 1, (Color){ 0, 0, 8, alpha });
    }
}

void DrawDemosceneBackdrop(const DemosceneSystem *demo, float time, float intensity,
                           int screenWidth, int screenHeight) {
    if (intensity < 0.0f) intensity = 0.0f;
    if (intensity > 1.0f) intensity = 1.0f;

    // Deep atmospheric gradient gives the architecture a readable silhouette
    // and avoids the unfinished flat-black void of the original scene.
    Color zenith = MixColor((Color){ 1, 2, 10, 255 }, demo->secondary, 0.035f, 255);
    Color horizon = MixColor((Color){ 4, 7, 20, 255 }, demo->primary,
                             0.075f + intensity * 0.025f, 255);
    DrawRectangleGradientV(0, 0, screenWidth, screenHeight, zenith, horizon);

    int horizonY = (int)((float)screenHeight * 0.47f);
    DrawRectangleGradientV(0, horizonY - 125, screenWidth, 185,
                           (Color){ demo->secondary.r, demo->secondary.g,
                                    demo->secondary.b, 0 },
                           (Color){ demo->primary.r, demo->primary.g,
                                    demo->primary.b,
                                    (unsigned char)(16.0f + intensity * 14.0f) });

    // A seed-positioned eclipsed data moon is deliberately off-axis and does
    // not pulse with the beat; it reads as scenery rather than a HUD effect.
    uint32_t moonHash = DemoHash(demo->seed ^ 0x4d4f4f4eu);
    int moonX = (moonHash & 1u) ? screenWidth - 178 : 178;
    int moonY = 122 + (int)((moonHash >> 5) % 62u);
    float moonRadius = 48.0f + (float)((moonHash >> 11) % 27u);
    for (int halo = 5; halo >= 1; halo--) {
        DrawCircle(moonX, moonY, moonRadius + (float)halo * 10.0f,
                   (Color){ demo->secondary.r, demo->secondary.g,
                            demo->secondary.b, (unsigned char)(2 + halo * 2) });
    }
    DrawCircle(moonX, moonY, moonRadius,
               MixColor((Color){ 15, 22, 42, 255 }, demo->secondary, 0.16f, 210));
    int eclipseShift = (moonHash & 2u) ? 18 : -18;
    DrawCircle(moonX + eclipseShift, moonY - 5, moonRadius * 0.92f, zenith);
    DrawCircleLines(moonX, moonY, moonRadius,
                    (Color){ demo->primary.r, demo->primary.g,
                             demo->primary.b, 92 });

    // Seeded star strata: near points are crisp, distant dust is dim. Motion
    // is almost imperceptible at cruise and opens slightly with intensity.
    for (int i = 0; i < 104; i++) {
        uint32_t h = DemoHash(demo->seed + (uint32_t)i * 0x9e3779b9u + 0x53544152u);
        float drift = time * (0.8f + intensity * 2.2f) * (float)(1u + (h & 3u));
        int x = (int)fmodf((float)(h % (uint32_t)(screenWidth + 80)) + drift,
                           (float)(screenWidth + 80)) - 40;
        int y = 18 + (int)((h >> 10) % (uint32_t)(horizonY - 34));
        int radius = ((h >> 22) & 15u) == 0u ? 2 : 1;
        unsigned char alpha = (unsigned char)(45u + ((h >> 16) & 95u));
        Color star = (h & 0x100u)
            ? (Color){ demo->primary.r, demo->primary.g, demo->primary.b, alpha }
            : (Color){ 185, 210, 235, alpha };
        DrawCircle(x, y, (float)radius, star);
    }

    // Layered aurora ribbons use long, slow waves unrelated to the kick. They
    // break up the sky while staying quiet enough for enemies to read clearly.
    for (int ribbon = 0; ribbon < 4; ribbon++) {
        float phase = DemoHash01(demo->seed ^ (uint32_t)(0xa711u + ribbon * 97)) * 6.2831853f;
        float baseY = 150.0f + (float)ribbon * 29.0f;
        Color ribbonColor = MixColor(demo->primary, demo->secondary,
                                     (float)ribbon / 3.0f,
                                     (unsigned char)(10.0f + intensity * 13.0f));
        int previousX = -48;
        int previousY = (int)baseY;
        for (int x = 0; x <= screenWidth + 48; x += 48) {
            float fx = (float)x / (float)screenWidth;
            int y = (int)(baseY + sinf(fx * (4.2f + (float)ribbon * 0.37f) +
                                       phase + time * (0.055f + (float)ribbon * 0.012f)) *
                                      (17.0f + (float)ribbon * 5.0f));
            DrawLineEx((Vector2){ (float)previousX, (float)previousY },
                       (Vector2){ (float)x, (float)y }, 8.0f + (float)ribbon * 3.0f,
                       ribbonColor);
            previousX = x;
            previousY = y;
        }
    }

    // Low-contrast megacity silhouettes establish scale behind the playable
    // trench without adding geometry or draw distance pressure.
    for (int i = 0; i < 54; i++) {
        uint32_t h = DemoHash(demo->seed ^ ((uint32_t)i * 0x85ebca6bu) ^ 0x43495459u);
        int width = 11 + (int)(h & 23u);
        int x = (i * (screenWidth + 80) / 53) - 40;
        int height = 18 + (int)((h >> 8) & 83u);
        Color silhouette = MixColor((Color){ 2, 4, 12, 255 }, demo->secondary,
                                    0.045f, 215);
        DrawRectangle(x, horizonY - height, width, height + 42, silhouette);
        if (((h >> 20) & 3u) == 0u) {
            DrawRectangle(x + width / 2, horizonY - height - 14, 1, 14,
                          (Color){ demo->primary.r, demo->primary.g,
                                   demo->primary.b, 62 });
        }
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
    DrawText(title, centerX - MeasureText(title, 54) / 2, 112, 54, demo.primary);
    DrawText(subtitle, centerX - MeasureText(subtitle, 17) / 2, 174, 17,
             (Color){ 150, 190, 215, 255 });

    DrawRectangle(centerX - 265, centerY - 82, 530, 164, (Color){ 2, 7, 18, 225 });
    DrawRectangleLines(centerX - 265, centerY - 82, 530, 164, demo.secondary);
    const char *seedHex = TextFormat("SEED  %08X", (unsigned int)seed);
    const char *seedDec = TextFormat("DECIMAL  %u", (unsigned int)seed);
    DrawText(seedHex, centerX - MeasureText(seedHex, 38) / 2, centerY - 52, 38, demo.hot);
    DrawText(seedDec, centerX - MeasureText(seedDec, 17) / 2, centerY + 2, 17,
             (Color){ 170, 205, 225, 255 });
    const char *music = TextFormat("TONALITY  %s     TEMPO  %.0f BPM", keyName, bpm);
    DrawText(music, centerX - MeasureText(music, 17) / 2, centerY + 36, 17, demo.primary);

    const char *adjust = "LEFT / RIGHT  +/- 1        UP / DOWN  +/- 100";
    const char *start = "PRESS ENTER TO COMPILE RUN";
    DrawText(adjust, centerX - MeasureText(adjust, 18) / 2, screenHeight - 120, 18,
             (Color){ 150, 185, 210, 255 });
    DrawText(start, centerX - MeasureText(start, 24) / 2, screenHeight - 78, 24, demo.secondary);
    DrawScanlines(screenWidth, screenHeight, 42);
}

void DrawDemosceneOverlay(const DemosceneSystem *demo, float time, float intensity,
                          float beatPulse, int screenWidth, int screenHeight) {
    (void)beatPulse;
    if (intensity < 0.0f) intensity = 0.0f;
    if (intensity > 1.0f) intensity = 1.0f;

    int centerX = screenWidth / 2;
    int centerY = screenHeight / 2;
    float streakAmount = fmaxf(0.0f, (intensity - 0.42f) / 0.58f);
    for (int i = 0; i < 34; i++) {
        uint32_t h = DemoHash(demo->seed + (uint32_t)i * 0x85ebca6bu);
        float angle = DemoHash01(h) * 6.2831853f;
        float phase = fmodf(time * (180.0f + (float)(h & 63u)) + DemoHash01(h ^ 0x41u) * 900.0f, 900.0f);
        float inner = 35.0f + phase * 0.48f;
        float length = (18.0f + DemoHash01(h ^ 0x99u) * 95.0f) * streakAmount;
        int x0 = centerX + (int)(cosf(angle) * inner);
        int y0 = centerY + (int)(sinf(angle) * inner * 0.58f);
        int x1 = centerX + (int)(cosf(angle) * (inner + length));
        int y1 = centerY + (int)(sinf(angle) * (inner + length) * 0.58f);
        unsigned char alpha = (unsigned char)(streakAmount * 115.0f);
        DrawLine(x0, y0, x1, y1, MixColor(demo->primary, demo->secondary, DemoHash01(h), alpha));
    }

    int previousTop = 0;
    int previousBottom = screenHeight;
    for (int x = 0; x <= screenWidth; x += 20) {
        float phase = (float)x * 0.025f + time * 2.2f + DemoHash01(demo->seed) * 6.28f;
        int top = 7 + (int)(sinf(phase) * (3.0f + intensity * 7.0f));
        int bottom = screenHeight - 8 + (int)(sinf(phase + 1.7f) * (3.0f + intensity * 7.0f));
        if (x > 0) {
            DrawLine(x - 20, previousTop, x, top, (Color){ demo->primary.r, demo->primary.g, demo->primary.b, 92 });
            DrawLine(x - 20, previousBottom, x, bottom,
                     (Color){ demo->secondary.r, demo->secondary.g, demo->secondary.b, 92 });
        }
        previousTop = top;
        previousBottom = bottom;
    }

    DrawScanlines(screenWidth, screenHeight, (unsigned char)(18.0f + intensity * 20.0f));
    DrawText(TextFormat("SEED %08X", (unsigned int)demo->seed), screenWidth - 142, screenHeight - 24,
             12, (Color){ demo->primary.r, demo->primary.g, demo->primary.b, 145 });
}
