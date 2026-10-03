// Pacing archetypes shared by environments: a hash-driven Markov chain over runs of chunks.
// Run r draws its archetype from the transition row of run r-1 (run 0 is `start`) and its
// length from [runMin, runMax]; everything derives from (base hash, r), so the archetype of
// any chunk is deterministic and independent of build order. Environments keep their own
// tables and blend a chunk's parameters from the previous archetype across the chunk.
#ifndef CT_PACING_H
#define CT_PACING_H

#include <stdint.h>

typedef struct PacingTable {
    int          count;          // number of archetypes
    int          start;          // archetype of run 0
    const int   *runMin, *runMax;   // [count], in chunks
    const float *transition;     // [count * count], row = from
} PacingTable;

// Archetype of chunk k (k < 0 is treated as 0). base: per-seed, per-environment hash.
int   pacing_archetype(const PacingTable *t, uint64_t base, int k);
// Smoothstep blend factor 0..1 of s across [s0, s0 + len] (1 when len <= 0).
float pacing_blend_t(double s, double s0, double len);
// out[i] = a[i] + (b[i] - a[i]) * t for n floats (archetype structs made only of floats).
void  pacing_lerp(float *out, const float *a, const float *b, int n, float t);

#endif
