// Seeded value noise. Output range approx [-1, 1]. Quintic interpolation (C2).
#ifndef CT_NOISE_H
#define CT_NOISE_H

#include <stdint.h>

double ct_noise1(uint64_t seed, double x);
double ct_noise2(uint64_t seed, double x, double y);
double ct_fbm1(uint64_t seed, double x, int octaves);
double ct_fbm2(uint64_t seed, double x, double y, int octaves);

#endif
