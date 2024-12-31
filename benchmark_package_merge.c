#include "package_merge.h"
#include "random.h"
#include "timing.h"

#include <math.h>
#include <stdlib.h>
#include <stdio.h>

#define BUFFER_SIZE (64 * (1 << 10)) // 64Ko

// Printable ASCII characters range [0x20, 0x7e]
#define RND_MIN 0x20
#define RND_MAX 0x7e
#define RND_RANGE_SIZE (RND_MAX - RND_MIN + 1)

static void generate_random_text(RngState32 *state, uint32_t *freqs, size_t n)
{
    for (size_t i = 0; i < RND_RANGE_SIZE; ++i)
    {
        freqs[i] = 0;
    }

    for (size_t i = 0; i < n; ++i)
    {
        uint32_t index;
        do
        {
            index = rand_norm_u32(state, 50, 15);
        } while (index >= RND_RANGE_SIZE);
        freqs[index]++;
    }
}

int main(int argc, char *argv[])
{
    RngState32 state;

    time_global_init();

    printf("Range size: %d\n", RND_RANGE_SIZE);
    uint32_t frequencies[RND_RANGE_SIZE];
    uint32_t code_lengths[RND_RANGE_SIZE];

    uint32_t iterations = 1000;
    uint64_t total_ticks = 0;
    for (uint32_t i = 0; i < iterations; ++i)
    {
        generate_random_text(&state, frequencies, BUFFER_SIZE);
        uint64_t tic = time_ticks();
        package_merge_any(frequencies, RND_RANGE_SIZE, 15, code_lengths);
        uint64_t elapsed = time_since(tic);
        total_ticks += elapsed;
    }

    printf("Total time: %.3f ms\n", time_ms(total_ticks));
    return 0;
}
