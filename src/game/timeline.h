// Curve timeline, GNU Rocket style: an animated value is a track of keyframes,
// evaluated at a time (in practice the music clock, in 16th steps). Each key
// sets the value at its time and the interpolation from it to the next key:
//
//   TL_STEP     hold the value until the next key
//   TL_LINEAR   straight line
//   TL_SMOOTH   smoothstep ease in and out
//   TL_RAMP     quadratic ease in (accelerates into the next key)
//   TL_HERMITE  cubic Hermite with Catmull-Rom tangents from the neighbours,
//               so a run of hermite keys passes through every value smoothly
//
// Before the first key a track holds the first value; after the last, the last.
// Tracks are tiny const arrays in code: a curve, not stored data.
#ifndef R94_TIMELINE_H
#define R94_TIMELINE_H

typedef enum { TL_STEP, TL_LINEAR, TL_SMOOTH, TL_RAMP, TL_HERMITE } TlInterp;

typedef struct TlKey {
    float    t;       // time (music steps); keys sorted by t
    float    v;       // value at t
    TlInterp interp;  // interpolation from this key to the next
} TlKey;

typedef struct TlTrack {
    const TlKey *keys;
    int          count;
} TlTrack;

#define TL_TRACK(arr) ((TlTrack){ (arr), (int)(sizeof(arr) / sizeof((arr)[0])) })

static inline float tl_eval(TlTrack track, double t)
{
    const TlKey *k = track.keys;
    int n = track.count;
    if (n <= 0) return 0.0f;
    if (t <= k[0].t) return k[0].v;
    if (t >= k[n - 1].t) return k[n - 1].v;
    int i = 0;
    while (i + 1 < n && t >= k[i + 1].t) i++;
    const TlKey *a = &k[i], *b = &k[i + 1];
    float span = b->t - a->t;
    float u = span > 0.0f ? (float)((t - a->t) / span) : 1.0f;
    switch (a->interp) {
        case TL_STEP:   return a->v;
        case TL_LINEAR: return a->v + (b->v - a->v) * u;
        case TL_SMOOTH: return a->v + (b->v - a->v) * u * u * (3.0f - 2.0f * u);
        case TL_RAMP:   return a->v + (b->v - a->v) * u * u;
        case TL_HERMITE: {
            // Catmull-Rom tangents (per unit u), one-sided at the ends of the track.
            float p0 = i > 0 ? k[i - 1].v : a->v, t0 = i > 0 ? k[i - 1].t : a->t;
            float p3 = i + 2 < n ? k[i + 2].v : b->v, t3 = i + 2 < n ? k[i + 2].t : b->t;
            float m0 = (b->t > t0) ? (b->v - p0) / (b->t - t0) * span : 0.0f;
            float m1 = (t3 > a->t) ? (p3 - a->v) / (t3 - a->t) * span : 0.0f;
            float u2 = u * u, u3 = u2 * u;
            return (2.0f * u3 - 3.0f * u2 + 1.0f) * a->v + (u3 - 2.0f * u2 + u) * m0
                 + (-2.0f * u3 + 3.0f * u2) * b->v + (u3 - u2) * m1;
        }
    }
    return a->v;
}

#endif // R94_TIMELINE_H
