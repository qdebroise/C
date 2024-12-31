#include "timing.h"

#include <assert.h>
#include <stdbool.h>

#if defined(_WIN32)
#   include <windows.h>
#elif defined(__linux__)
#   include <time.h>
#else
#   error "Unsupported platform"
#endif

struct GlobalTimeInfo
{
    bool initialized;
#if defined(_WIN32)
    LARGE_INTEGER Frequency;
    LARGE_INTEGER Epoch;
#elif defined(__linux__)
    uint64_t epoch;
#endif
};

static struct GlobalTimeInfo _gti = {0};

void time_global_init(void)
{
    assert(!_gti.initialized);
    _gti.initialized = true;

#if defined(_WIN32)
    QueryPerformanceFrequency(&_gti.Frequency);
    QueryPerformanceCounter(&_gti.Epoch);
#elif defined(__linux__)
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    _gti.epoch = time_ticks();
#endif
}

// Returns the time since epoch in nanoseconds.
uint64_t time_ticks(void)
{
    assert(_gti.initialized);

    uint64_t now;
#if defined(_WIN32)
    LARGE_INTEGER Now;
    QueryPerformanceCounter(&Now);
    now = (uint64_t)((Now.QuadPart - _gti.Epoch.QuadPart) * 1000000000 / _gti.Frequency.QuadPart);
#elif defined(__linux__)
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    now = (uint64_t)(ts.tv_sec * 1000000000 + (uint64_t)ts.tv_nsec) - _gti.epoch;
#endif

    return now;
}

/*
#if defined(__linux__)
#include <sys/time.h>
#endif
int main()
{
    time_global_init();

#if defined(_WIN32)
    Sleep(2000);
#elif defined(__linux__)
    sleep(2);
#endif

    printf("%lf sec\n", time_sec(time_ticks()));
    printf("%lf ms\n", time_ms(time_ticks()));
    printf("%lf us\n", time_us(time_ticks()));
    printf("%llu ticks\n", time_ticks());
}
*/
