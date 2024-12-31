#ifndef TIMING_H_
#define TIMING_H_

#if defined(__linux__) && _POSIX_C_SOURCE < 199309L
#   define _POSIX_C_SOURCE 199309L // clock_gettime(), MONOTONIC_CLOCK
#endif

#include <stdint.h>

// Initialises time system. This should be called only once before using any other time functions.
void time_global_init(void);

// Returns an unqualified 'ticks' value representing a point in time.
uint64_t time_ticks(void);

// Convert time ticks to seconds, milliseconds or microsecronds.
static inline double time_sec(uint64_t ticks) { return (double)ticks * 0.000000001; }
static inline double time_ms(uint64_t ticks) { return (double)ticks * 0.000001; }
static inline double time_us(uint64_t ticks) { return (double)ticks * 0.001; }

// Returns the duration in ticks since 'start' ticks.
static inline uint64_t time_since(uint64_t start) { return time_ticks() - start; }

#endif // TIMING_H_


