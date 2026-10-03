#include "pacing.h"
#include "rng.h"

int pacing_archetype(const PacingTable *t, uint64_t base, int k)
{
    if (k < 0) k = 0;
    int a = t->start, start = 0;
    for (int r = 0;; r++) {
        Pcg32 rng = pcg32_seed(ct_hash64_2(base, (uint64_t)r));
        if (r > 0) a = pcg32_weighted(&rng, t->transition + a * t->count, t->count);
        int len = pcg32_int(&rng, t->runMin[a], t->runMax[a]);
        if (k < start + len) return a;
        start += len;
    }
}

void pacing_archetypes(const PacingTable *t, uint64_t base, int k0, int n, int *out)
{
    int a = t->start, start = 0, i = 0;
    for (; i < n && k0 + i < 0; i++) out[i] = -1;   // filled below with chunk 0's archetype
    int first = i;
    for (int r = 0; i < n; r++) {
        Pcg32 rng = pcg32_seed(ct_hash64_2(base, (uint64_t)r));
        if (r > 0) a = pcg32_weighted(&rng, t->transition + a * t->count, t->count);
        int len = pcg32_int(&rng, t->runMin[a], t->runMax[a]);
        while (i < n && k0 + i < start + len) out[i++] = a;
        start += len;
    }
    for (int j = 0; j < first; j++) out[j] = out[first];
}

float pacing_blend_t(double s, double s0, double len)
{
    float t = len > 0.0 ? (float)((s - s0) / len) : 1.0f;
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return t * t * (3.0f - 2.0f * t);
}

void pacing_lerp(float *out, const float *a, const float *b, int n, float t)
{
    for (int i = 0; i < n; i++) out[i] = a[i] + (b[i] - a[i]) * t;
}
