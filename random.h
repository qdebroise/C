#ifndef RANDOM_H_
#define RANDOM_H_

#include <pcg/pcg_basic.h>

#include <math.h>

typedef pcg32_random_t RngState32;

// Global random when we don't want to bother with all the RNG state fuss.
// static inline void rand_srand(uint64_t seed) { pcg32_srandom(seed, 0); }
// static inline uint32_t rand_u32() { return pcg32_random(); }

// Initialises pseudo-random number generator.
static inline void rand_srand(RngState32 *state, uint64_t init_state, uint64_t init_seq)
{
    pcg32_srandom_r(state, init_state, init_seq);
}

// Generates a random 32-bits unsigned integer.
static inline uint32_t rand_u32(RngState32 *state) { return pcg32_random_r(state); }

// Generates an unsigned 32-bits integer in the provided bounds following a uniform distribution.
// The upper bound is excluded (The generation range is [low, up[).
static inline uint32_t rand_bounded_u32(RngState32 *state, uint32_t low, uint32_t up)
{
    return low + pcg32_boundedrand_r(state, up - low);
}

// Generates a signed 32-bits integer in the provided bounds following a uniform distribution. The
// upper bound is excluded (The generation range is [low, up[).
static inline int32_t rand_bounded_i32(RngState32 *state, int32_t low, int32_t up)
{
    return low + pcg32_boundedrand_r(state, up - low);
}

// Generates a random 32-bits floating point number in [0, 1[ range.
static inline float rand_f32(RngState32 *state) { return ldexpf(pcg32_random_r(state), -32); }

// Generates a random 32-bits floating point number in the provided bounds following a uniform
// distribution. The upper bound is excluded (The generation range is [low, up[).
static inline float rand_bounded_f32(RngState32 *state, float low, float up)
{
    return low + rand_f32(state) * (up - low);
}

// Generates a random 32-bits floating point number following a normal distribution of the given
// parameters.
// https://en.wikipedia.org/wiki/Box%E2%80%93Muller_transform
static inline float rand_norm_f32(RngState32 *state, float mean, float deviation)
{
    // float u1 = rand_f32(state);
    // float u2 = rand_f32(state);
    // Returns only one of the two possible normal variables.
    // return mean + deviation * sqrt(-2 * log(u1)) * cos(2*3.1415926535f * u2);
    float u, v, s;
    do
    {
        u = rand_bounded_f32(state, -1.f, 1.f);
        v = rand_bounded_f32(state, -1.f, 1.f);
        s = u*u + v*v;
    } while (s >= 1.f);
    return mean + deviation * (u * sqrt((-2.f * log(s)) / s));
}

// Generates a 32-bits unsigned integer following a normal distribution of the given parameters.
static inline uint32_t rand_norm_u32(RngState32 *state, float mean, float deviation)
{
    return floor(rand_norm_f32(state, mean, deviation));
}

// Generates a 32-bits signed integer following a normal distribution of the given parameters.
static inline int32_t rand_norm_i32(RngState32 *state, float mean, float deviation)
{
    return floor(rand_norm_f32(state, mean, deviation));
}

#endif // RANDOM_H_


