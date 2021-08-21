#include "huffman.h"

#include "bitarray.h"
#include "package_merge.h"

#include <stdbool.h>

// @Note @Todo: this will change when moving things around to make Deflate.
// This implementation of Huffman compression is designed to compress bytes. Thus a symbol of the
// alphabet ranges in [0x00, 0xff].
#define ALPHABET_SIZE 256

// @Todo: put this elsewhere. See when all this will be used in Deflate. Does Deflate always uses 15
// as the max code length in all its trees ?
#define MAX_CODE_LENGTH 32

typedef struct frequencies_t
{
    uint32_t count[ALPHABET_SIZE]; // Frequency of every symbol in the alphabet.
    uint32_t sorted[ALPHABET_SIZE]; // Sorted frequencies.
    uint32_t sorted_indices[ALPHABET_SIZE]; // Sorted indices of frequencies.
    uint32_t num_used_symbols;
} frequencies_t;

typedef struct codeword_t
{
    uint8_t num_bits;
    uint32_t bits;
} codeword_t;

void frequencies_init(frequencies_t* freq)
{
    for (int i = 0; i < ALPHABET_SIZE; ++i)
    {
        freq->count[i] = 0;
        freq->sorted_indices[i] = i;
    }
}

// @Todo @Performance; we use the indirection quite a lot like lengths[sorted[i]], freqs[sorted[i]],
// etc. This cause quite a lot of jumps all over the place in memory. Alphabets are rather small,
// for my use case anyway so copying the frequencies in a sorted array should be rather cheap and
// faster to use afterwards. Then we only need use the indirection when building codelengths for
// symbols. Test and profile this.
// @Todo: better sorting.
// @Todo: improve the overall aspect of this function. Some things can be simplified.
void frequencies_count_and_sort(const uint8_t* input, size_t input_size, frequencies_t* freq)
{
    for (size_t i = 0; i < input_size; ++i)
    {
        freq->count[input[i]]++;
    }

    // Sort frequencies in ascending order.
    for (size_t i = 0; i < ALPHABET_SIZE; ++i)
    {
        for (size_t j = 0; j < ALPHABET_SIZE - i - 1; ++j)
        {
            uint32_t f1 = freq->count[freq->sorted_indices[j]];
            uint32_t f2 = freq->count[freq->sorted_indices[j + 1]];
            // Sort by frequency and alphabetically. Important for canonical prefix codes.
            if (f1 > f2 || (f1 == f2 && freq->sorted_indices[j] > freq->sorted_indices[j + 1]))
            {
                size_t swap = freq->sorted_indices[j + 1];
                freq->sorted_indices[j + 1] = freq->sorted_indices[j];
                freq->sorted_indices[j] = swap;
            }
        }
    }

    for (size_t i = 0; i < ALPHABET_SIZE; ++i)
    {
        freq->sorted[i] = freq->count[freq->sorted_indices[i]];
    }

    for (size_t i = 0; i < ALPHABET_SIZE; ++i)
    {
        if (freq->count[freq->sorted_indices[i]] != 0)
        {
            freq->num_used_symbols = ALPHABET_SIZE - i;
            break;
        }
    }
}

// Constructs a canonical Huffman tree.
//
// A canonical Huffman tree is one Huffman tree amongst the several tree possibilities of a given
// alphabet. This tree fits additional rules making it very easy to describe in a compact format.
// https://en.wikipedia.org/wiki/Canonical_Huffman_code
void build_canonical_prefix_code(const uint32_t* lengths, const uint32_t* sorted, codeword_t* codewords)
{
    uint32_t code = 0;
    for (int i = ALPHABET_SIZE - 1; i >= 0; --i)
    {
        uint32_t length = lengths[sorted[i]];
        uint32_t next_length = i == 0 ? length : lengths[i - 1]; // @Todo: add one more element to avoid the overflow condition ?

        codewords[sorted[i]].num_bits = length;
        codewords[sorted[i]].bits = code;
        code = (code + 1) << (next_length - length);
    }
}

// @Todo: can't be lengths generated in the same order as the sorted frequencies ? Thus removing the
// indirection code_lengths[sorted_indices[i]]. Then everything is put back in order when building
// the actual prefix canonical code.
void package_merge_generate_lengths(const uint32_t* active_leaves, const uint32_t* sorted, uint8_t limit, uint32_t used_symbols, uint32_t alphabet_size, uint32_t* code_lengths)
{
    uint8_t code_len = limit;
    uint32_t symbol_index = alphabet_size - used_symbols;
    for (uint8_t i = 0; i < limit; ++i)
    {
        uint32_t num_symbols_with_len = i == 0
            ? active_leaves[i]
            : active_leaves[i] - active_leaves[i - 1];
        for (uint32_t j = 0; j < num_symbols_with_len; ++j)
        {
            code_lengths[sorted[symbol_index++]] = code_len;
        }
        assert(symbol_index <= alphabet_size && "It tried to add more code lengths than there are symbols.");
        code_len--;
    }
}

uint8_t* huffman_compress(const uint8_t* input, size_t size)
{
    frequencies_t freq;
    frequencies_init(&freq);
    frequencies_count_and_sort(input, size, &freq);

    // @Todo: the algorithm in the other file cannot be used directly here because of what it
    // outputs. Because it directly returns code lengths it needs to be aware of the total size of
    // the alphabet and not only the number non-zero frequencies.I don't really want to pass in this
    // parameter. I think we should change its output to return the array of active leaves as
    // presented in the paper and handle the actual code length creation in here.
    // @Note: feed package-merge only the frequencies that are non-zero.
    uint32_t active_leaves[MAX_CODE_LENGTH] = {0};
    package_merge(&freq.sorted[ALPHABET_SIZE - freq.num_used_symbols], freq.num_used_symbols, MAX_CODE_LENGTH, &active_leaves[0]);
    uint32_t lengths[ALPHABET_SIZE] = {0};
    package_merge_generate_lengths(&active_leaves[0], &freq.sorted_indices[0], MAX_CODE_LENGTH, freq.num_used_symbols, ALPHABET_SIZE, &lengths[0]);

    codeword_t codewords[ALPHABET_SIZE];
    build_canonical_prefix_code(&lengths[0], &freq.sorted_indices[0], &codewords[0]);

    // @Cleanup: for debug purposes only. Remove when done.
    // for (int i = 0; i < freq.num_used_symbols; ++i)
    // {
        // printf("0x%x %d\n", i, codewords[i].num_bits);
    // }

    // @Todo: We need to encode the huffman tree in the output as well if we ever want to decode
    // the data.

    bitarray_t output = {0};
    const uint8_t* it = input;
    const uint8_t* end = input + size;
    while (it != end)
    {
        // @Todo @Performance: see if it is better to have a buffer of bits on a uint64_t and
        // push only when it is full.
        bitarray_push_bits_msb(&output, codewords[*it].bits, codewords[*it].num_bits);
        it++;
    }

    bitarray_pad_last_byte(&output);
    return output.data;
}

uint8_t* huffman_uncompress(const uint8_t* compressed_data, size_t size)
{
    // @Todo: impl.
    return NULL;
}

