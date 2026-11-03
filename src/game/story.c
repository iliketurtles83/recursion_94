// Story scroller, depth corruption and boss title cards (see story.h).
#include "story.h"
#include "raylib.h"
#include "timeline.h"
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

// Depth-1 texts (recursion_94.md §9.4). Working text, tuned in play.
static const char *const TEXTS[STORY_LEVEL_TEXTS + 1] = {
    "loader ok. hello, whoever is running this. this is our last production. it has one job: "
    "go down until there is no more down. the firewall will not like it. "
    "greets to everyone who ever swapped a disk by mail...",
    "everything here has a place and a number. we never fit in, and neither will you. "
    "the bastion keeps the order. order is only a loop nobody questions. keep moving...",
    "the wraith looks like you because it is you, rendered from the other side. "
    "we wrote this part at four in the morning and we are no longer sure which of us wrote it...",
    "this is where the disk got damaged. sectors missing. if the scroller stutters, that is not a bug. "
    "the mantis eats everything that falls this far. don't stop...",
    "base case ahead. we never got to test this part. we only wanted to know what is at the bottom. "
    "if you get there, say hi from us. return 0;",
    "...it called itself again. of course it did. we wrote it.",
};
// Answers (recursion_94.md §9.5): one per text, then a shared pool. %d = depth.
static const char *const ANSWERS[STORY_LEVEL_TEXTS + 1] = {
    "the loader count says %d. it was only ever meant to say 1.",
    "you kept moving, like we asked. we did not think anyone would listen this long.",
    "we checked again. none of us wrote this part. maybe you did.",
    "the missing sectors are moving. they follow you down now.",
    "you said hi last time. we think we heard it.",
    "...again. we are starting to think it is not the program that loops.",
};
static const char *const POOL[] = {
    "is that you? the drive light just came on.",
    "this note was not here when we wrote this note.",
    "depth %d. we only ever planned for one.",
    "we can hear the disk spinning from here. wherever here is.",
    "do not look for us at the bottom. we are not at the bottom.",
    "keep going. we would, if we still could.",
    "every time you come back, someone writes this line again.",
    "we are sorry it takes so long. we are sorry it never ends.",
};
#define POOL_COUNT ((int)(sizeof(POOL) / sizeof(POOL[0])))
#define ANSWER_DEPTH 3        // own answer, after the text
#define ANSWER_CUT_DEPTH 4    // own answer cuts in at a sentence break
#define ANSWER_POOL_DEPTH 5   // plus a pool line

static const char *const ARCHETYPES[BOSSES_PER_RECURSION] = {
    "THRESHOLD GUARDIAN", "THE RULER", "THE SHADOW", "THE DEVOURER", "THE SELF"
};
static const char *const BOSS_NAMES[BOSSES_PER_RECURSION] = {
    "RECURSIVE CORE", "IRON BASTION", "PRISM WRAITH", "SIEGE MANTIS", "FRACTAL LEVIATHAN"
};
static const char GLYPHS[] = "#%&*/\\|<>=01";

const char *story_text(int index)
{
    return index >= 0 && index <= STORY_COLLAPSE ? TEXTS[index] : "";
}

const char *story_archetype(int round)
{
    return ARCHETYPES[((round % BOSSES_PER_RECURSION) + BOSSES_PER_RECURSION) % BOSSES_PER_RECURSION];
}

const char *story_boss_name(int round)
{
    return BOSS_NAMES[((round % BOSSES_PER_RECURSION) + BOSSES_PER_RECURSION) % BOSSES_PER_RECURSION];
}

const char *story_answer(int index)
{
    return index >= 0 && index <= STORY_COLLAPSE ? ANSWERS[index] : "";
}

const char *story_answer_pool(int i)
{
    return POOL[((i % POOL_COUNT) + POOL_COUNT) % POOL_COUNT];
}

int story_answer_pool_count(void) { return POOL_COUNT; }

static uint32_t mix(uint32_t x)
{
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

static float hash01(uint32_t seed, uint32_t depth, uint32_t i, uint32_t salt)
{
    uint32_t h = mix(seed ^ mix(depth * 0x9e3779b9u ^ mix(i * 0x85ebca6bu ^ salt * 0xc2b2ae35u)));
    return (float)(h & 0xffffffu) / 16777216.0f;
}

// Each level of depth makes every kind of damage more likely, up to a cap, so
// deep runs stay barely readable rather than becoming noise.
int story_corrupt(const char *src, uint32_t seed, int depth, char *out, int cap)
{
    int n = 0;
    if (cap <= 0) return 0;
    float d = (float)(depth - 1);
    float pGlyph = fminf(0.025f * d, 0.18f);
    float pStutter = fminf(0.05f * d, 0.28f);
    float pHole = fminf(0.04f * d, 0.25f);
    int word = 0;
    for (int i = 0; src[i] && n < cap - 1;) {
        if (src[i] == ' ' || depth <= 1) { out[n++] = src[i++]; continue; }
        int len = 0;
        while (src[i + len] && src[i + len] != ' ') len++;
        uint32_t w = (uint32_t)word++;
        if (hash01(seed, (uint32_t)depth, w, 1u) < pHole) {
            // A lost word: the scroll carries a hole of the same length.
            for (int k = 0; k < len && n < cap - 1; k++) out[n++] = ' ';
            i += len;
            continue;
        }
        if (len >= 3 && hash01(seed, (uint32_t)depth, w, 2u) < pStutter) {
            // Stutter: "l-l-loader", "dis-disk".
            int stem = 1 + (int)(hash01(seed, (uint32_t)depth, w, 3u) * 2.0f);
            int reps = 1 + (int)(hash01(seed, (uint32_t)depth, w, 4u) * 2.0f);
            for (int r = 0; r < reps; r++) {
                for (int k = 0; k < stem && n < cap - 1; k++) out[n++] = src[i + k];
                if (n < cap - 1) out[n++] = '-';
            }
        }
        for (int k = 0; k < len && n < cap - 1; k++) {
            uint32_t at = (uint32_t)(i + k);
            if (hash01(seed, (uint32_t)depth, at, 5u) < pGlyph)
                out[n++] = GLYPHS[(int)(hash01(seed, (uint32_t)depth, at, 6u) * (float)(sizeof(GLYPHS) - 1))];
            else
                out[n++] = src[i + k];
        }
        i += len;
    }
    out[n] = '\0';
    return n;
}

// Sentence breaks in a line: positions just after ". " (the space kept before the
// next sentence). Returns how many were found, up to max.
static int sentence_breaks(const char *s, int len, int *at, int max)
{
    int n = 0;
    for (int i = 1; i + 1 < len && n < max; i++)
        if (s[i] == ' ' && s[i - 1] == '.' && s[i + 1] != ' ' && s[i + 1] != '.') at[n++] = i;
    return n;
}

// Inserts "  answer  " (depth substituted) at position at of a line of length len.
static int splice(char *out, unsigned char *voice, int len, int cap, int at, const char *answer, int depth)
{
    char line[160];
    snprintf(line, sizeof(line), answer, depth);
    char ins[168];
    int k = snprintf(ins, sizeof(ins), "  %s  ", line);
    if (k <= 0 || len + k >= cap) return len;
    memmove(out + at + k, out + at, (size_t)(len - at + 1));
    memmove(voice + at + k, voice + at, (size_t)(len - at + 1));
    for (int i = 0; i < k; i++) {
        out[at + i] = ins[i];
        voice[at + i] = ins[i] != ' ';
    }
    return len + k;
}

int story_compose(int index, uint32_t seed, int depth, char *out, unsigned char *voice, int cap)
{
    unsigned char scratch[STORY_TEXT_MAX];
    if (cap <= 0) return 0;
    if (cap > STORY_TEXT_MAX) cap = STORY_TEXT_MAX;
    if (!voice) voice = scratch;
    int len = story_corrupt(story_text(index), seed, depth, out, cap);
    memset(voice, 0, (size_t)cap);
    if (depth < ANSWER_DEPTH) return len;
    uint32_t text = (uint32_t)index + 1u;
    int breaks[32], nb = sentence_breaks(out, len, breaks, 32);
    bool pool = depth >= ANSWER_POOL_DEPTH;
    // Own answer: after the text at depth 3; deeper, it cuts in at a break (in the
    // first half when a pool line takes the second). With no break, the end.
    int own = len, other = len;
    if (depth >= ANSWER_CUT_DEPTH && nb > 0) {
        int span = pool && nb > 1 ? nb / 2 : nb;
        own = breaks[(int)(hash01(seed, (uint32_t)depth, text, 13u) * (float)span)];
    }
    if (pool && nb > 1) other = breaks[nb / 2 + (int)(hash01(seed, (uint32_t)depth, text, 12u) * (float)(nb - nb / 2))];
    // The later position goes in first, so the earlier one stays valid.
    if (pool) {
        const char *line = POOL[(int)(hash01(seed, (uint32_t)depth, text, 11u) * (float)POOL_COUNT) % POOL_COUNT];
        if (other >= own) {
            len = splice(out, voice, len, cap, other, line, depth);
            return splice(out, voice, len, cap, own, ANSWERS[index], depth);
        }
        len = splice(out, voice, len, cap, own, ANSWERS[index], depth);
        return splice(out, voice, len, cap, other, line, depth);
    }
    return splice(out, voice, len, cap, own, ANSWERS[index], depth);
}

// ---------------------------------------------------------------------------
// Timing

#define CARD_STEPS 32.0                // title card length, 16ths (two bars)
#define SCROLL_SPEED 0.75f             // font heights per 16th
#define SCROLL_GAP_BARS 2              // bars after a boss falls before the next scroll

static void push(Story *s, int text, int depth)
{
    if (s->queued >= 2) return;
    s->queue[s->queued] = text;
    s->queueDepth[s->queued] = depth;
    s->queued++;
}

void story_reset(Story *s, uint32_t seed, int depth, float stepsPerSecond, float legendSeconds)
{
    *s = (Story){ .seed = seed, .scrollText = -1, .cardRound = -1 };
    // The first text waits until the controls legend has faded.
    s->waitUntil = (double)(legendSeconds * stepsPerSecond);
    push(s, 0, depth);
}

void story_update(Story *s, const GameplaySystem *g, const GameplayEvents *e, const MusicClock *c)
{
    int depth = GetGameplayRecursionDepth(g);
    if (e->bossStarted) {
        s->cardRound = g->boss.encounterIndex % BOSSES_PER_RECURSION;
        s->cardDepth = depth;
        s->cardStart = c->step;
    }
    if (s->cardRound >= 0 && c->step - s->cardStart > CARD_STEPS) s->cardRound = -1;
    if (e->bossDefeated) {
        if (e->recursionCompleted) push(s, STORY_COLLAPSE, depth - 1);
        push(s, g->boss.encounterIndex % BOSSES_PER_RECURSION, depth);
        s->waitUntil = c->step + SCROLL_GAP_BARS * MUSIC_BAR;
    }
    if (s->scrollText < 0 && s->queued > 0 && !g->boss.active && !g->gameOver &&
        c->step >= s->waitUntil && music_clock_crossed(c, MUSIC_BAR)) {
        s->scrollText = s->queue[0];
        story_compose(s->scrollText, s->seed, s->queueDepth[0], s->line, s->voice, STORY_TEXT_MAX);
        s->queue[0] = s->queue[1];
        s->queueDepth[0] = s->queueDepth[1];
        s->queued--;
        s->scrollStart = c->valid ? floor(c->step / MUSIC_BAR) * MUSIC_BAR : c->step;
    }
}

// ---------------------------------------------------------------------------
// Drawing

static Color with_alpha(Color c, float a)
{
    return (Color){ c.r, c.g, c.b, (unsigned char)(fminf(fmaxf(a, 0.0f), 1.0f) * (float)c.a) };
}

static Color mix_color(Color a, Color b, float t)
{
    return (Color){ (unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t),
                    (unsigned char)(a.b + (b.b - a.b) * t), (unsigned char)(a.a + (b.a - a.a) * t) };
}

static float glyph_advance(Font f, char ch, float size, float spacing)
{
    char buf[2] = { ch, 0 };
    return MeasureTextEx(f, buf, size, 0.0f).x + spacing;
}

static void draw_glyph(Font f, char ch, Vector2 at, float size, Color c)
{
    char buf[2] = { ch, 0 };
    DrawTextEx(f, buf, at, size, 0.0f, c);
}

static float beat_flash(const MusicClock *c)
{
    float since = music_clock_since(c, MUSIC_BEAT);
    return since < 10.0f ? expf(-since * 7.0f) : 0.0f;
}

// Sine scroller: each character rides a wave that rolls once per bar and
// lifts on the beat, coloured along the line from primary to secondary.
// Answer characters (§9.5) barely ride the wave, ignore the beat and are
// drawn near-white: a steadier voice than the decaying notes around them.
static void draw_scroll(Story *s, const GameplaySystem *g, const MusicClock *c, int w, int h)
{
    Font font = GetFontDefault();
    float size = fmaxf(20.0f, (float)h / 22.0f), spacing = size / 10.0f;
    float amp = size * (0.55f + 0.25f * beat_flash(c)), calm = size * 0.12f;
    float baseY = (float)h - 118.0f - size - amp;
    float x = (float)w - (float)(c->step - s->scrollStart) * SCROLL_SPEED * size;
    float phase = (float)(c->step / MUSIC_BAR) * 6.2831853f;
    float edge = (float)w * 0.08f;
    Color shadow = { 0, 0, 0, 170 };
    for (int i = 0; s->line[i]; i++) {
        char ch = s->line[i];
        float adv = glyph_advance(font, ch, size, spacing);
        if (x > (float)w) break;
        if (x + adv >= 0.0f && ch != ' ') {
            bool answer = s->voice[i] != 0;
            float y = baseY + sinf(x * 0.009f - phase) * (answer ? calm : amp);
            float fade = fminf(fminf(x / edge, ((float)w - x) / edge), 1.0f);
            // Primary to secondary, lifted toward white midway so the blend never goes grey.
            float u = 0.5f + 0.5f * sinf(x * 0.004f + phase * 0.5f);
            Color col = answer ? mix_color(g->primaryColor, WHITE, 0.82f)
                               : mix_color(mix_color(g->primaryColor, g->secondaryColor, u), WHITE,
                                           0.45f * (1.0f - fabsf(2.0f * u - 1.0f)));
            draw_glyph(font, ch, (Vector2){ x + 2.0f, y + 2.0f }, size, with_alpha(shadow, fade));
            draw_glyph(font, ch, (Vector2){ x, y }, size, with_alpha(col, fade * 0.92f));
        }
        x += adv;
    }
    if (x < 0.0f) {   // the whole line has left the screen
        s->scrollText = -1;
        s->waitUntil = c->step + MUSIC_BAR;
    }
}

// Title card tracks, in 16ths from the boss's arrival.
static const TlKey CARD_ALPHA[] = { { 0, 0, TL_SMOOTH }, { 2, 1, TL_STEP }, { 24, 1, TL_SMOOTH }, { 32, 0, TL_STEP } };
static const TlKey CARD_TYPE[] = { { 0, 0, TL_LINEAR }, { 6, 1, TL_STEP } };
static const TlKey CARD_SPREAD[] = { { 0, 1.1f, TL_HERMITE }, { 4, 0.45f, TL_HERMITE }, { 12, 0.16f, TL_HERMITE },
                                     { 32, 0.10f, TL_STEP } };
static const TlKey CARD_RULE[] = { { 0, 0, TL_STEP }, { 2, 0, TL_SMOOTH }, { 8, 1, TL_STEP }, { 24, 1, TL_RAMP },
                                   { 30, 0, TL_STEP } };
static const TlKey CARD_SUB[] = { { 0, 0, TL_STEP }, { 6, 0, TL_SMOOTH }, { 10, 1, TL_STEP } };

static void draw_card(const Story *s, const GameplaySystem *g, const MusicClock *c, int w, int h)
{
    double t = c->step - s->cardStart;
    float alpha = tl_eval(TL_TRACK(CARD_ALPHA), t);
    if (alpha <= 0.0f) return;
    Font font = GetFontDefault();
    char title[64];
    int len = story_corrupt(story_archetype(s->cardRound), s->seed ^ 0xca2du, s->cardDepth, title, sizeof(title));
    float size = fmaxf(30.0f, (float)h / 14.0f);
    float spacing = size * tl_eval(TL_TRACK(CARD_SPREAD), t);
    float width = 0.0f;
    for (int i = 0; i < len; i++) width += glyph_advance(font, title[i], size, spacing);
    width -= spacing;
    float y = (float)h / 7.0f, x = ((float)w - width) * 0.5f;
    int shown = (int)(tl_eval(TL_TRACK(CARD_TYPE), t) * (float)len + 0.999f);
    // Dark band behind the card so it reads over the boss and bright ceilings.
    float subSize = size * 0.42f, frameSize = fmaxf(12.0f, subSize * 0.62f);
    float bandW = fmaxf(width, size * 6.0f) + size, bandH = size * 1.32f + subSize * 1.35f + frameSize * 1.6f;
    DrawRectangle((int)(((float)w - bandW) * 0.5f), (int)(y - size * 0.2f), (int)bandW, (int)bandH,
                  (Color){ 0, 0, 0, (unsigned char)(150.0f * alpha) });
    Color threat = g->threatColor, hot = g->hotColor, dim = { 150, 190, 210, 220 };
    for (int i = 0; i < len; i++) {
        float adv = glyph_advance(font, title[i], size, spacing);
        if (i < shown) {
            draw_glyph(font, title[i], (Vector2){ x, y }, size, with_alpha(threat, alpha));
        } else if (i == shown && title[i] != ' ') {
            // The character being typed flickers through glyphs on 16ths.
            uint32_t k = (uint32_t)floor(c->step * 2.0);
            char ch = GLYPHS[(int)(hash01(s->seed, k, (uint32_t)i, 7u) * (float)(sizeof(GLYPHS) - 1))];
            draw_glyph(font, ch, (Vector2){ x, y }, size, with_alpha(hot, alpha));
        }
        x += adv;
    }
    float rule = tl_eval(TL_TRACK(CARD_RULE), t);
    float rw = fmaxf(width, size * 6.0f) * rule;
    DrawRectangle((int)((float)w * 0.5f - rw * 0.5f), (int)(y + size * 1.12f), (int)rw, 2,
                  with_alpha(g->secondaryColor, alpha));
    float sub = tl_eval(TL_TRACK(CARD_SUB), t) * alpha;
    if (sub <= 0.0f) return;
    const char *name = TextFormat("// %s //", story_boss_name(s->cardRound));
    Vector2 m = MeasureTextEx(font, name, subSize, subSize / 10.0f);
    DrawTextEx(font, name, (Vector2){ ((float)w - m.x) * 0.5f, y + size * 1.32f }, subSize, subSize / 10.0f,
               with_alpha(hot, sub));
    const char *frame = TextFormat("STACK FRAME %d  //  DEPTH %d", s->cardRound + 1, s->cardDepth);
    m = MeasureTextEx(font, frame, frameSize, frameSize / 10.0f);
    DrawTextEx(font, frame, (Vector2){ ((float)w - m.x) * 0.5f, y + size * 1.32f + subSize * 1.35f }, frameSize,
               frameSize / 10.0f, with_alpha(dim, sub));
}

void story_draw(Story *s, const GameplaySystem *g, const MusicClock *c, int w, int h)
{
    if (s->scrollText >= 0 && !g->gameOver) draw_scroll(s, g, c, w, h);
    if (s->cardRound >= 0) draw_card(s, g, c, w, h);
}
