#include "noise.h"
#include "rng.h"
#include <math.h>

static double lattice1(uint64_t seed, int64_t i) { return ct_hash01d(ct_hash64_2(seed, (uint64_t)i)) * 2.0 - 1.0; }
static double lattice2(uint64_t seed, int64_t i, int64_t j) { return ct_hash01d(ct_hash64_3(seed, (uint64_t)i, (uint64_t)j)) * 2.0 - 1.0; }
static double quintic(double t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }

double ct_noise1(uint64_t seed, double x)
{
    double fl = floor(x);
    int64_t i = (int64_t)fl;
    double t = quintic(x - fl);
    double a = lattice1(seed, i), b = lattice1(seed, i + 1);
    return a + (b - a) * t;
}

double ct_noise2(uint64_t seed, double x, double y)
{
    double fx = floor(x), fy = floor(y);
    int64_t i = (int64_t)fx, j = (int64_t)fy;
    double tx = quintic(x - fx), ty = quintic(y - fy);
    double a = lattice2(seed, i, j), b = lattice2(seed, i + 1, j);
    double c = lattice2(seed, i, j + 1), d = lattice2(seed, i + 1, j + 1);
    double ab = a + (b - a) * tx, cd = c + (d - c) * tx;
    return ab + (cd - ab) * ty;
}

double ct_fbm1(uint64_t seed, double x, int octaves)
{
    double sum = 0.0, amp = 0.5, norm = 0.0;
    for (int o = 0; o < octaves; o++) {
        sum += amp * ct_noise1(ct_hash64_2(seed, (uint64_t)o), x);
        norm += amp;
        amp *= 0.5;
        x *= 2.0;
    }
    return sum / norm;
}

double ct_fbm2(uint64_t seed, double x, double y, int octaves)
{
    double sum = 0.0, amp = 0.5, norm = 0.0;
    for (int o = 0; o < octaves; o++) {
        sum += amp * ct_noise2(ct_hash64_2(seed, (uint64_t)o), x, y);
        norm += amp;
        amp *= 0.5;
        x *= 2.0; y *= 2.0;
    }
    return sum / norm;
}
