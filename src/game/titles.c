// Title and level-transition overlays (see titles.h).
#include "titles.h"
#include "story.h"
#include "timeline.h"
#include <math.h>
#include <string.h>

static Color with_alpha(Color c, float a)
{
    return (Color){ c.r, c.g, c.b, (unsigned char)(fminf(fmaxf(a, 0.0f), 1.0f) * (float)c.a) };
}

static Color mix_color(Color a, Color b, float t)
{
    t = fminf(fmaxf(t, 0.0f), 1.0f);
    return (Color){ (unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t),
                    (unsigned char)(a.b + (b.b - a.b) * t), (unsigned char)(a.a + (b.a - a.a) * t) };
}

// DrawText's spacing for the default font (integer, size / 10).
static float text_spacing(float size) { return floorf(fmaxf(size, 10.0f) / 10.0f); }

static float glyph_width(char ch, float size)
{
    char buf[2] = { ch, 0 };
    return MeasureTextEx(GetFontDefault(), buf, size, 0.0f).x;
}

static float spaced_width(const char *s, float size, float spacing)
{
    float w = 0.0f;
    int n = (int)strlen(s);
    for (int i = 0; i < n; i++) w += glyph_width(s[i], size) + (i + 1 < n ? spacing : 0.0f);
    return w;
}

static void draw_glyph(char ch, float x, float y, float size, Color c)
{
    char buf[2] = { ch, 0 };
    DrawTextEx(GetFontDefault(), buf, (Vector2){ x, y }, size, 0.0f, c);
}

static void draw_centered(const char *s, float cx, float y, float size, float spacing, Color c)
{
    float x = cx - spaced_width(s, size, spacing) * 0.5f;
    for (int i = 0; s[i]; i++) {
        if (s[i] != ' ') draw_glyph(s[i], x, y, size, c);
        x += glyph_width(s[i], size) + spacing;
    }
}

// ---------------------------------------------------------------------------
// Logo drop-in

// One letter's fall, in steps from its own start: ramps in from above,
// overshoots a tenth of a letter below the line and settles.
static const TlKey LETTER_Y[] = { { 0, -2.2f, TL_RAMP }, { 3, 0.10f, TL_SMOOTH }, { 4.5f, 0, TL_STEP } };
static const TlKey LETTER_A[] = { { 0, 0, TL_LINEAR }, { 1.5f, 1, TL_STEP } };
static const TlKey LETTER_HOT[] = { { 0, 0, TL_STEP }, { 3, 1, TL_SMOOTH }, { 8, 0, TL_STEP } };

float titles_logo_width(const char *text, float size)
{
    return spaced_width(text, size, text_spacing(size));
}

void titles_draw_logo(const char *text, float x, float y, float size, double steps, float stagger,
                      Color color, Color hot, float alpha)
{
    float spacing = text_spacing(size);
    for (int i = 0; text[i]; i++) {
        double lt = steps - (double)i * stagger;
        float a = tl_eval(TL_TRACK(LETTER_A), lt) * alpha;
        if (a > 0.0f && text[i] != ' ') {
            float dy = tl_eval(TL_TRACK(LETTER_Y), lt) * size;
            Color c = mix_color(color, hot, tl_eval(TL_TRACK(LETTER_HOT), lt));
            draw_glyph(text[i], x, y + dy, size, with_alpha(c, a));
        }
        x += glyph_width(text[i], size) + spacing;
    }
}

// ---------------------------------------------------------------------------
// State

void titles_reset(Titles *t, uint32_t seed, int depth, const MusicClock *c)
{
    *t = (Titles){ .seed = seed, .introDepth = depth, .introStart = c->step, .transRound = -1 };
}

void titles_update(Titles *t, const GameplaySystem *g, const GameplayEvents *e, const MusicClock *c)
{
    if (e->bossDefeated) {
        // Entering the next frame, from the next bar line.
        t->transRound = (g->boss.encounterIndex) % BOSSES_PER_RECURSION;
        t->transDepth = GetGameplayRecursionDepth(g);
        t->transRecurse = e->recursionCompleted;
        t->transStart = c->valid ? (floor(c->step / MUSIC_BAR) + 1.0) * MUSIC_BAR : c->step;
    }
    if (t->transRound >= 0 && c->step - t->transStart > TITLES_TRANSITION_STEPS) t->transRound = -1;
}

bool titles_logo_settled(const Titles *t, const MusicClock *c)
{
    return c->step - t->introStart >= TITLES_INTRO_STEPS;
}

// ---------------------------------------------------------------------------
// Intro

#define INTRO_LETTER_START 2.0f
#define INTRO_STAGGER 0.75f
static const TlKey INTRO_COPPER[] = { { 0, 0, TL_SMOOTH }, { 6, 1, TL_STEP }, { 32, 1, TL_SMOOTH }, { 42, 0, TL_STEP } };
static const TlKey INTRO_RULE[] = { { 0, 0, TL_STEP }, { 10, 0, TL_SMOOTH }, { 16, 1, TL_STEP }, { 34, 1, TL_SMOOTH },
                                    { 40, 0, TL_STEP } };
static const TlKey INTRO_TYPE[] = { { 0, 0, TL_STEP }, { 14, 0, TL_LINEAR }, { 22, 1, TL_STEP } };
static const TlKey INTRO_LINES[] = { { 0, 1, TL_STEP }, { 34, 1, TL_SMOOTH }, { 40, 0, TL_STEP } };
static const TlKey INTRO_LOADER[] = { { 0, 0, TL_STEP }, { 18, 0, TL_SMOOTH }, { 22, 1, TL_STEP }, { 34, 1, TL_SMOOTH },
                                      { 40, 0, TL_STEP } };
static const TlKey INTRO_FLY[] = { { 0, 0, TL_STEP }, { 38, 0, TL_SMOOTH }, { 48, 1, TL_STEP } };
#define CORNER_X 24.0f
#define CORNER_Y 20.0f
#define CORNER_SIZE 20.0f

// Copper bars: three gradient bands bouncing around a line, rolling once a bar.
static void draw_copper(float cy, float span, float barH, int w, double steps, float alpha, const GameplaySystem *g)
{
    Color cols[3] = { g->primaryColor, g->secondaryColor, g->hotColor };
    float phase = (float)(steps / MUSIC_BAR) * 6.2831853f;
    for (int k = 0; k < 3; k++) {
        float y = cy + sinf(phase + (float)k * 2.1f) * span;
        Color c = with_alpha(cols[k], 0.30f * alpha), clear = with_alpha(cols[k], 0.0f);
        DrawRectangleGradientV(0, (int)(y - barH * 0.5f), w, (int)(barH * 0.5f), clear, c);
        DrawRectangleGradientV(0, (int)y, w, (int)(barH * 0.5f), c, clear);
    }
}

static void draw_intro(const Titles *t, const GameplaySystem *g, const MusicClock *c, int w, int h,
                       const char *keyName, float bpm)
{
    double s = c->step - t->introStart;
    if (s >= TITLES_INTRO_STEPS) return;
    const char *title = "RECURSION_94";
    float big = fmaxf(40.0f, (float)h / 9.0f);
    float fly = tl_eval(TL_TRACK(INTRO_FLY), s);
    float size = big + (CORNER_SIZE - big) * fly;
    float cx = (float)w * 0.5f, cy = (float)h * 0.26f;
    float x = (cx - titles_logo_width(title, big) * 0.5f) * (1.0f - fly) + CORNER_X * fly;
    float y = (cy - big * 0.5f) * (1.0f - fly) + CORNER_Y * fly;
    draw_copper(cy, big * 0.85f, big * 0.9f, w, c->step, tl_eval(TL_TRACK(INTRO_COPPER), s), g);
    Color corner = { g->primaryColor.r, g->primaryColor.g, g->primaryColor.b, 170 };
    titles_draw_logo(title, x, y, size, s - INTRO_LETTER_START, INTRO_STAGGER,
                     mix_color(g->primaryColor, corner, fly), g->hotColor, 1.0f);

    float rule = tl_eval(TL_TRACK(INTRO_RULE), s);
    float rw = titles_logo_width(title, big) * 1.15f * rule;
    float ry = cy + big * 0.62f;
    DrawRectangle((int)(cx - rw * 0.5f), (int)ry, (int)rw, 2, with_alpha(g->secondaryColor, rule));

    float lines = tl_eval(TL_TRACK(INTRO_LINES), s);
    float sub = big * 0.30f;
    // Soft dark band so the small lines read over a bright corridor.
    float band = tl_eval(TL_TRACK(INTRO_LOADER), s) * 0.6f + tl_eval(TL_TRACK(INTRO_TYPE), s) * lines * 0.4f;
    if (band > 0.0f) {
        float top = ry + 4.0f, mid = ry + sub * 1.8f, bottom = ry + sub * 3.4f;
        Color dark = { 0, 0, 0, (unsigned char)(150.0f * fminf(band, 1.0f)) }, clear = { 0, 0, 0, 0 };
        DrawRectangleGradientV(0, (int)top, w, (int)(mid - top), clear, dark);
        DrawRectangleGradientV(0, (int)mid, w, (int)(bottom - mid), dark, clear);
    }
    char full[64], frame[64];
    int len = story_corrupt(TextFormat("STACK FRAME 1  //  %s", story_level_name(0)), t->seed ^ 0x7171u,
                            t->introDepth, full, sizeof(full));
    int shown = (int)(tl_eval(TL_TRACK(INTRO_TYPE), s) * (float)len + 0.999f);
    if (shown > 0 && lines > 0.0f) {
        // Typed from the left edge of the full line, so it doesn't re-centre as it grows.
        memcpy(frame, full, sizeof(frame));
        frame[shown < len ? shown : len] = '\0';
        float fx = cx - spaced_width(full, sub, text_spacing(sub)) * 0.5f;
        DrawTextEx(GetFontDefault(), frame, (Vector2){ fx, ry + sub * 0.7f }, sub, text_spacing(sub),
                   with_alpha(g->hotColor, lines));
    }
    float loader = tl_eval(TL_TRACK(INTRO_LOADER), s);
    if (loader > 0.0f) {
        float ls = fmaxf(12.0f, sub * 0.55f);
        const char *l = TextFormat("LOADER OK  //  SEED %08X  //  %s  //  %.0f BPM  //  DEPTH %d",
                                   (unsigned int)t->seed, keyName, bpm, t->introDepth);
        draw_centered(l, cx, ry + sub * 2.1f, ls, text_spacing(ls), with_alpha((Color){ 150, 190, 210, 220 }, loader));
    }
}

// ---------------------------------------------------------------------------
// Level transition

static const TlKey TR_WIPE[] = { { 0, -0.1f, TL_RAMP }, { 4, 1.1f, TL_STEP } };
static const TlKey TR_ALPHA[] = { { 0, 1, TL_STEP }, { 24, 1, TL_SMOOTH }, { 32, 0, TL_STEP } };
static const TlKey TR_TYPE[] = { { 0, 0, TL_STEP }, { 2, 0, TL_LINEAR }, { 8, 1, TL_STEP } };
static const TlKey TR_SPREAD[] = { { 0, 1.2f, TL_STEP }, { 6, 1.2f, TL_HERMITE }, { 10, 0.5f, TL_HERMITE },
                                   { 16, 0.25f, TL_HERMITE }, { 32, 0.2f, TL_STEP } };
static const TlKey TR_SUB[] = { { 0, 0, TL_STEP }, { 6, 0, TL_SMOOTH }, { 10, 1, TL_STEP } };

static void draw_transition(const Titles *t, const GameplaySystem *g, const MusicClock *c, int w, int h)
{
    double s = c->step - t->transStart;
    if (t->transRound < 0 || s < 0.0 || s > TITLES_TRANSITION_STEPS) return;
    // Raster wipe: a bright line sweeping down with a fading wake above it.
    float wipe = tl_eval(TL_TRACK(TR_WIPE), s);
    if (wipe > -0.1f && wipe < 1.1f) {
        float y = wipe * (float)h, tail = (float)h * 0.12f;
        DrawRectangleGradientV(0, (int)(y - tail), w, (int)tail, with_alpha(g->secondaryColor, 0.0f),
                               with_alpha(g->secondaryColor, 0.35f));
        DrawRectangle(0, (int)y - 1, w, 3, with_alpha(WHITE, 0.85f));
    }
    float alpha = tl_eval(TL_TRACK(TR_ALPHA), s);
    float size = fmaxf(28.0f, (float)h / 15.0f);
    float cx = (float)w * 0.5f, y = (float)h * 0.34f;
    char line[64], name[64];
    const char *src = t->transRecurse ? "> CALL RECURSION_94" : TextFormat("> CALL FRAME %d", t->transRound + 1);
    int len = story_corrupt(src, t->seed ^ 0x3a11u, t->transDepth, line, sizeof(line));
    float sp = text_spacing(size);
    float x = cx - spaced_width(line, size, sp) * 0.5f;
    int shown = (int)(tl_eval(TL_TRACK(TR_TYPE), s) * (float)len + 0.999f);
    float bandH = size * 2.6f;
    DrawRectangle(0, (int)(y - size * 0.35f), w, (int)bandH, (Color){ 0, 0, 0, (unsigned char)(120.0f * alpha) });
    for (int i = 0; i < len && i < shown; i++) {
        if (line[i] != ' ') draw_glyph(line[i], x, y, size, with_alpha(g->hotColor, alpha));
        x += glyph_width(line[i], size) + sp;
    }
    // Block cursor after the typed text, blinking on 8ths.
    if (((int)floor(c->step / MUSIC_EIGHTH) & 1) == 0)
        DrawRectangle((int)(x + sp), (int)y, (int)(size * 0.5f), (int)size, with_alpha(g->primaryColor, alpha));
    float sub = tl_eval(TL_TRACK(TR_SUB), s) * alpha;
    if (sub <= 0.0f) return;
    const char *nsrc = t->transRecurse ? TextFormat("DEPTH %d", t->transDepth) : story_level_name(t->transRound);
    story_corrupt(nsrc, t->seed ^ 0x3a12u, t->transDepth, name, sizeof(name));
    float ns = size * 0.55f;
    draw_centered(name, cx, y + size * 1.3f, ns, ns * tl_eval(TL_TRACK(TR_SPREAD), s), with_alpha(g->primaryColor, sub));
}

void titles_draw(const Titles *t, const GameplaySystem *g, const MusicClock *c, int w, int h,
                 const char *keyName, float bpm)
{
    draw_intro(t, g, c, w, h, keyName, bpm);
    if (!g->gameOver) draw_transition(t, g, c, w, h);
}
