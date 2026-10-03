// Deterministic RNG and hashing. No global state: every generator is a value.
#ifndef CT_RNG_H
#define CT_RNG_H

#include <stdint.h>

// splitmix64 finalizer, used as a 64-bit mixing function.
static inline uint64_t ct_mix64(uint64_t x)
{
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

static inline uint64_t ct_hash64_2(uint64_t a, uint64_t b) { return ct_mix64(ct_mix64(a) ^ (b * 0xD6E8FEB86659FD93ull)); }
static inline uint64_t ct_hash64_3(uint64_t a, uint64_t b, uint64_t c) { return ct_hash64_2(ct_hash64_2(a, b), c); }

// Hash to float in [0,1).
static inline float ct_hash01(uint64_t h) { return (float)(ct_mix64(h) >> 40) * (1.0f / 16777216.0f); }
static inline double ct_hash01d(uint64_t h) { return (double)(ct_mix64(h) >> 11) * (1.0 / 9007199254740992.0); }

// PCG32 (O'Neill), XSH-RR variant.
typedef struct Pcg32 { uint64_t state, inc; } Pcg32;

static inline uint32_t pcg32_next(Pcg32 *r)
{
    uint64_t old = r->state;
    r->state = old * 6364136223846793005ull + r->inc;
    uint32_t xorshifted = (uint32_t)(((old >> 18u) ^ old) >> 27u);
    uint32_t rot = (uint32_t)(old >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
}

static inline Pcg32 pcg32_seed(uint64_t seed)
{
    Pcg32 r = { 0u, (ct_mix64(seed ^ 0xA5A5A5A5ull) << 1u) | 1u };
    pcg32_next(&r);
    r.state += ct_mix64(seed);
    pcg32_next(&r);
    return r;
}

static inline float pcg32_float(Pcg32 *r) { return (float)(pcg32_next(r) >> 8) * (1.0f / 16777216.0f); }
static inline float pcg32_range(Pcg32 *r, float lo, float hi) { return lo + (hi - lo) * pcg32_float(r); }
// Integer in [lo, hi] inclusive.
static inline int pcg32_int(Pcg32 *r, int lo, int hi)
{
    if (hi <= lo) return lo;
    return lo + (int)(pcg32_next(r) % (uint32_t)(hi - lo + 1));
}
static inline int pcg32_chance(Pcg32 *r, float p) { return pcg32_float(r) < p; }

// Pick an index from a weight table.
static inline int pcg32_weighted(Pcg32 *r, const float *w, int n)
{
    float total = 0.0f;
    for (int i = 0; i < n; i++) total += w[i];
    float x = pcg32_float(r) * total;
    for (int i = 0; i < n; i++) { if (x < w[i]) return i; x -= w[i]; }
    return n - 1;
}

#endif
