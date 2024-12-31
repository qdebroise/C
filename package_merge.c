// Package-merge algorithm.
//
// An algorithm to generate optimal length-limited prefix codes.
//
// Ref:
// - "A Fast and Space-Economical Algorithm for Length-Limited Coding", 1995, Moffat et. al.
// https://doi.org/10.1007/BFb0015404
// - https://create.stephan-brumme.com/length-limited-prefix-codes/
//
// == Package-merge ==
//
// Package-merge takes a list of symbol frequencies in input. Then, it creates L lists where L is
// the maximum code length of a symbol.
// In the simplest form of package-merge, a list is computed as follow:
// Use the previous generated list (the list of frequencies initially) and make packages of two
// successive frequencies (discarding any singleton remaining). **Then merge the obtained packages
// with the initial list of frequencies**.
// Example of the first two lists for the input frequencies [1, 1, 5, 7, 10, 14].
//
//              | Previous: [(1, 1), (5, 7), (10, 14)]
// Iteration 1  | Packages: [2, 12, 24]
//              | New list: [1, 1, (2), 5, 7, 10, (12), 14, (24)]
//
//              | Previous: [(1, 1), (2, 5), (7, 10), (12, 14), 24]
// Iteration 2  | Packages: [2, 7, 17, 26]
//              | New list: [1, 1, (2), 5, (7), 7, 10, 14, (17), (26)]
//
// When L lists are created the process stops. Then, active nodes in each list are counted from the
// last list to the first one. An active node is a node that is not a package and that is less than
// N where N is twice the number packages in list l+1 (with N=2*n-2 for the last list where n is the
// number of frequencies).
// For example, with the same frequencies as before and L=4, we have the following lists (The symbol
// '|' indicates the limit N in each list):
//
// L=1, N=2  [1, 1,| 5, 7, 10, 14]
// L=2, N=4  [1, 1, (2), 5,| 7, 10, (12), 14, (24)]
// L=3, N=8  [1, 1, (2), 5, (7), 7, 10, 14,| (17), (26)]
// L=4, N=10 [1, 1, (2), 5, (7), 7, 10, (14), 14, (24),| (43)]
//
// Number of active nodes:
// L            = 4 3 2 1
// Active nodes = 6 6 3 2
//
// From the number of actives nodes we get the number of symbols by code length.
// Given the array of active leaves [2, 3, 6, 6], the code lengths are retrieved with:
// - 2 symbols (2 - 0) of length 4
// - 1 symbols (3 - 2) of length 3
// - 3 symbols (6 - 3) of length 2
// - 0 symbols (6 - 6) of length 1
//
// In the end we have:
// Frequencies:  [1, 1, 5, 7, 10, 14]
// Code lengths: [4, 4, 3, 2,  2,  2]
//
// The implemented package-merge uses a slightly more advanced version with on demand node creation
// called similar to Lazy PM.

#include "package_merge.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

// Allows for 2^32 symbols which is more symbols than what Unicode can handle so :shrug:.
#define MAX_DEPTH 32

// Computes optimal length-limited prefix code lengths from an ordered set of frequencies using
// package-merge algorithm. The frequencies **MUST** not contain 0s and **MUST** be sorted in
// ascending order.
// Code length for each frequency is returned in the `code_lengths` parameter which *MUST* be
// allocated by the user and large enough to receive `n` number of values.
// `limit` is the maximum code length allowed for the symbols.
void package_merge(const uint32_t *freqs, uint32_t n, uint8_t limit, uint32_t *code_lengths)
{
    assert(n > 0 && "The list of frequencies is empty.");
    assert(limit <= MAX_DEPTH && "The code length limit is too big.");
    // The maximum depth of the Huffman tree is represented by `limit`. A binary tree of depth
    // `limit` can have at most '2^`limit`' leaves. If `n`, the number of symbols, is more than the
    // number of leaves then constructing the tree is impossible as we don't have enough leaf nodes
    // for all the symbols.
    assert((1ull << limit) > n && "The code length limit is too small.");

    if (n <= 2)
    {
        code_lengths[0] = 1;
        code_lengths[1] = 1;
        return;
    }

    // Per list data.
    uint32_t data[4 * limit];
    uint32_t *a = data; // Active leaves in the lth list.
    uint32_t *w = a + limit; // Sum of the two rightmost look-ahead nodes in the lth list.
    uint32_t *c = w + limit; // The number of non-package nodes in the lth list (number of 1 & 3a).
    uint32_t *j = c + limit; // The total number of nodes in the lth list.

    // A bitmask stores the information of the nodes at one position in all the lists.
    // - The index in the array indicates is the index of node in a list.
    // - The index of a bit in the mask is the index of a list.
    //
    //                   Number of lists (limit)
    //                 <------------------------>
    //                ^ .[0] = mask_0
    // Number of      | .[1] = mask_1
    // symbols/freqs  | .[2] = mask_2
    //                | ...
    //                v
    uint32_t masks[2*n - 2];
    memset(masks, 0, sizeof(masks));

    uint32_t stack[MAX_DEPTH];
    uint32_t stack_size = 0;

    // Initialise data.
    assert(freqs[0] > 0 && freqs[1] > 0 && "Frequencies of 0 are not allowed.");
    for (uint32_t i = 0; i < limit; ++i)
    {
        w[i] = freqs[0] + freqs[1];
        c[i] = 2;
        j[i] = 2;
    }

    // Run lazy package-merge.
    uint32_t l = limit - 1;
    for (uint32_t i = 2; i < 2*n - 2;)
    {
        uint32_t freq = c[l] < n ? freqs[c[l]] : UINT32_MAX;
        uint32_t s = l != 0 ? w[l - 1] : 0;

        assert(freq > 0 && "Frequencies of 0 are not allowed.");

        if (l == 0 || s > freq)
        {
            // Step 1 & 3a (c.f. paper).
            c[l] = c[l] + 1;
            w[l] += freq;
        }
        else
        {
            // Step 3b (c.f. paper).
            w[l - 1] = 0;
            w[l] += s;

            masks[j[l]] |= (1 << l);

            stack[stack_size++] = l - 1;
            stack[stack_size++] = l - 1;
        }

        j[l]++;

        // Paper quote: "The need for 2n-2 chains in list L again drives the process."
        if (l == limit - 1) ++i;

        l = stack_size != 0
            ? stack[--stack_size]
            : limit - 1;
    }

    // Count active leaves by summing set bits in each list.
    uint32_t num_to_use = 2*n - 2;
    for (int i = limit - 1; i >= 0; --i)
    {
        // We can start at index 2 because we know for sure that the first two nodes are not
        // packages.
        uint32_t num_packages = 0;
        for (uint32_t k = 2; k < num_to_use; ++k)
        {
            num_packages += ((masks[k] >> i) & 0x1);
        }

        a[i] = num_to_use - num_packages;
        num_to_use = 2 * num_packages;
    }

    // Generate code length per symbol.
    uint32_t sym = 0;
    for (uint32_t l = 0; l < limit; ++l)
    {
        uint32_t count = l == 0 ? a[0] : a[l] - a[l - 1];
        for (uint32_t k = 0; k < count; ++k)
        {
            code_lengths[sym++] = limit - l;
        }
    }
}

void package_merge_any(const uint32_t *freqs, uint32_t n, uint8_t limit, uint32_t *code_lengths)
{
    uint32_t *buffer = malloc(3 * n * sizeof(uint32_t));
    uint32_t *sorted = buffer;
    uint32_t *sorted_indices = sorted + n;
    uint32_t *sorted_code_lengths = sorted_indices + n;

    for (uint32_t i = 0; i < n; ++i)
    {
        sorted_indices[i] = i;
    }

    // Sort frequencies
    // @Todo: Use something else than a bubble sort. qsort from stdlib or something else.
    for (uint32_t i = 0; i < n; ++i)
    {
        for (uint32_t j = 0; j < n - i - 1; ++j)
        {
            uint32_t f1 = freqs[sorted_indices[j]];
            uint32_t f2 = freqs[sorted_indices[j + 1]];
            if (f1 > f2 || (f1 == f2 && sorted_indices[j] > sorted_indices[j + 1]))
            {
                uint32_t swap = sorted_indices[j + 1];
                sorted_indices[j + 1] = sorted_indices[j];
                sorted_indices[j] = swap;
            }
        }
    }

    // Initialize package-merge input data.
    uint32_t first = 0;
    for (uint32_t i = 0; i < n; ++i)
    {
        sorted[i] = freqs[sorted_indices[i]];
        if (sorted[i] == 0)
        {
            first++;
            sorted_code_lengths[i] = 0;
        }
    }

    // Run PM.
    package_merge(sorted + first, n - first, limit, sorted_code_lengths + first);

    // Assign code lengths in the order of the orginal array of frequencies.
    for (uint32_t i = 0; i < n; ++i)
    {
        code_lengths[sorted_indices[i]] = sorted_code_lengths[i];
    }

    free(buffer);
}
