#include "package_merge.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <time.h>

void get_lengths(const uint32_t* active_leaves, uint8_t limit, uint32_t used_symbols, uint32_t alphabet_size, uint32_t* code_lengths)
{
    uint8_t code_len = limit;
    uint32_t symbol_index = 0;

    for (uint32_t i = 0; i < alphabet_size - used_symbols; ++i)
    {
        code_lengths[symbol_index++] = 0;
    }

    for (uint8_t i = 0; i < limit; ++i)
    {
        uint32_t num_symbols_with_len = i == 0
            ? active_leaves[i]
            : active_leaves[i] - active_leaves[i - 1];
        for (uint32_t j = 0; j < num_symbols_with_len; ++j)
        {
            code_lengths[symbol_index++] = code_len;
        }
        assert(symbol_index <= alphabet_size && "It tried to add more code lengths than there are symbols.");
        code_len--;
    }
}

void print_array(const uint32_t* arr, size_t n)
{
    printf("[");
    for (size_t i = 0; i < n; ++i) printf("%d, ", arr[i]);
    printf("]\n");
}

bool compare_arrays(const uint32_t* results, const uint32_t* expected, size_t n)
{
    for (size_t i = 0; i < n; ++i)
    {
        if (results[i] != expected[i])
        {
            printf("Expected: ");
            print_array(expected, n);
            printf("Got: ");
            print_array(results, n);

            return false;
        }
    }
    return true;
}

void test_leaves_paper_example_limit_3()
{
    uint32_t frequencies[] = {1, 1, 5, 7, 10, 14};
    uint32_t n = sizeof(frequencies) / sizeof(frequencies[0]);
    uint32_t limit = 3;
    uint32_t active_leaves[limit];

    package_merge(&frequencies[0], n, limit, &active_leaves[0]);
    assert(compare_arrays(active_leaves, (uint32_t[]){4, 6, 6}, limit));
}

void test_leaves_paper_example_limit_4()
{
    uint32_t frequencies[] = {1, 1, 5, 7, 10, 14};
    uint32_t n = sizeof(frequencies) / sizeof(frequencies[0]);
    uint32_t limit = 4;
    uint32_t active_leaves[limit];

    package_merge(&frequencies[0], n, limit, &active_leaves[0]);
    assert(compare_arrays(active_leaves, (uint32_t[]){2, 3, 6, 6}, limit));
}

void test_leaves_paper_example_limit_7()
{
    uint32_t frequencies[] = {1, 1, 5, 7, 10, 14};
    uint32_t n = sizeof(frequencies) / sizeof(frequencies[0]);
    uint32_t limit = 7;
    uint32_t active_leaves[limit];

    package_merge(&frequencies[0], n, limit, &active_leaves[0]);
    assert(compare_arrays(active_leaves, (uint32_t[]){0, 0, 2, 3, 4, 5, 6}, limit));
}

void test_leaves_paper_example_zero_freqs()
{
    uint32_t frequencies[] = {0, 0, 0, 0, 0, 1, 1, 5, 7, 10, 14};
    uint32_t limit = 4;
    uint32_t active_leaves[limit];

    package_merge(&frequencies[5], 6, limit, &active_leaves[0]);
    assert(compare_arrays(active_leaves, (uint32_t[]){2, 3, 6, 6}, limit));
}

void test_length_paper_example_limit_3()
{
    uint32_t frequencies[] = {1, 1, 5, 7, 10, 14};
    uint32_t n = sizeof(frequencies) / sizeof(frequencies[0]);
    uint32_t limit = 3;
    uint32_t active_leaves[limit];
    uint32_t code_length[n];

    package_merge(&frequencies[0], n, limit, &active_leaves[0]);
    get_lengths(&active_leaves[0], limit, n, n, &code_length[0]);
    assert(compare_arrays(code_length, (uint32_t[]){3, 3, 3, 3, 2, 2}, n));
}

void test_length_paper_example_limit_4()
{
    uint32_t frequencies[] = {1, 1, 5, 7, 10, 14};
    uint32_t n = sizeof(frequencies) / sizeof(frequencies[0]);
    uint32_t limit = 4;
    uint32_t active_leaves[limit];
    uint32_t code_length[n];

    package_merge(&frequencies[0], n, limit, &active_leaves[0]);
    get_lengths(&active_leaves[0], limit, n, n, &code_length[0]);
    assert(compare_arrays(code_length, (uint32_t[]){4, 4, 3, 2, 2, 2}, n));
}

void test_length_paper_example_limit_7()
{
    uint32_t frequencies[] = {1, 1, 5, 7, 10, 14};
    uint32_t n = sizeof(frequencies) / sizeof(frequencies[0]);
    uint32_t limit = 7;
    uint32_t active_leaves[limit];
    uint32_t code_length[n];

    package_merge(&frequencies[0], n, limit, &active_leaves[0]);
    get_lengths(&active_leaves[0], limit, n, n, &code_length[0]);
    assert(compare_arrays(code_length, (uint32_t[]){5, 5, 4, 3, 2, 1}, n));
}

void test_length_paper_example_zero_freqs()
{
    uint32_t frequencies[] = {0, 0, 0, 0, 0, 1, 1, 5, 7, 10, 14};
    uint32_t n = sizeof(frequencies) / sizeof(frequencies[0]);
    uint32_t limit = 4;
    uint32_t active_leaves[limit];
    uint32_t code_length[n];

    package_merge(&frequencies[5], 6, limit, &active_leaves[0]);
    get_lengths(&active_leaves[0], limit, 6, n, &code_length[0]);
    assert(compare_arrays(code_length, (uint32_t[]){0, 0, 0, 0, 0, 4, 4, 3, 2, 2, 2}, n));
}

int main(int argc, char* argv[])
{
    test_leaves_paper_example_limit_3();
    test_leaves_paper_example_limit_4();
    test_leaves_paper_example_limit_7();
    test_leaves_paper_example_zero_freqs();
    test_length_paper_example_limit_3();
    test_length_paper_example_limit_4();
    test_length_paper_example_limit_7();
    test_length_paper_example_zero_freqs();

    // Fibonnaci sequence, 42 terms, worst case scenario for building Huffman tree.
    uint32_t frequencies2[] = {1, 1, 2, 3, 5, 8, 13, 21, 34, 55, 89, 144, 233, 377, 610, 987, 1597, 2584, 4181, 6765, 10946, 17711, 28657, 46368, 75025, 121393, 196418, 317811, 514229, 832040, 1346269, 2178309, 3524578, 5702887, 9227465, 14930352, 24157817, 39088169, 63245986, 102334155, 165580141, 267914296};
    uint32_t n2 = sizeof(frequencies2) / sizeof(frequencies2[0]);
    uint32_t code_length2[n2];
    clock_t tic, toc;
    tic = clock();
    package_merge(&frequencies2[0], n2, 32, &code_length2[0]);
    toc = clock();
    float elapsed_s = (float)(toc - tic) / CLOCKS_PER_SEC;
    // printf("Elapsed: %f\n", elapsed_s);

    uint32_t frequencies3[] = {1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143, 144, 145, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 156, 157, 158, 159, 160, 161, 162, 163, 164, 165, 166, 167, 168, 169, 170, 171, 172, 173, 174, 175, 176, 177, 178, 179, 180, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191, 192, 193, 194, 195, 196, 197, 198, 199, 200, 201, 202, 203, 204, 205, 206, 207, 208, 209, 210, 211, 212, 213, 214, 215, 216, 217, 218, 219, 220, 221, 222, 223, 224, 225, 226, 227, 228, 229, 230, 231, 232, 233, 234, 235, 236, 237, 238, 239, 240, 241, 242, 243, 244, 245, 246, 247, 248, 249, 250, 251, 252, 253, 254, 255};
    uint32_t n3 = sizeof(frequencies3) / sizeof(frequencies3[0]);
    uint32_t code_length3[n3];
    package_merge(&frequencies3[0], n3, 32, &code_length3[0]);

    printf("Tests passed.\n");
}
