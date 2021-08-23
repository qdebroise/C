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
#define MAX_CODE_LENGTH 31 // 31 so that it can fit on 6 bits.

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

// @Todo @Performance: we use the indirection quite a lot like lengths[sorted[i]], freqs[sorted[i]],
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
        uint32_t length = lengths[i];
        uint32_t next_length = i == 0 ? length : lengths[i - 1]; // @Todo: add one more element to avoid the overflow condition ?

        codewords[sorted[i]].num_bits = length;
        codewords[sorted[i]].bits = code;
        code = (code + 1) << (next_length - length);
    }
}

void package_merge_generate_lengths(const uint32_t* active_leaves, uint8_t limit, uint32_t used_symbols, uint32_t alphabet_size, uint32_t* code_lengths)
{
    uint8_t code_len = limit;
    uint32_t symbol_index = 0;

    // First fill the symbols with a frequency of 0 with a code length of 0.
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
    while (symbol_index < alphabet_size) code_lengths[symbol_index--] = 0;
}

uint8_t* huffman_compress(const uint8_t* input, size_t size)
{
    frequencies_t freq;
    frequencies_init(&freq);
    frequencies_count_and_sort(input, size, &freq);

    // @Note: feed package-merge only the frequencies that are non-zero.
    uint32_t active_leaves[MAX_CODE_LENGTH] = {0};
    package_merge(&freq.sorted[ALPHABET_SIZE - freq.num_used_symbols], freq.num_used_symbols, MAX_CODE_LENGTH, &active_leaves[0]);
    uint32_t lengths[ALPHABET_SIZE] = {0};
    package_merge_generate_lengths(&active_leaves[0], MAX_CODE_LENGTH, freq.num_used_symbols, ALPHABET_SIZE, &lengths[0]);

    codeword_t codewords[ALPHABET_SIZE];
    build_canonical_prefix_code(&lengths[0], &freq.sorted_indices[0], &codewords[0]);

    // @Cleanup: for debug purposes only. Remove when done.
    // for (int i = 0; i < freq.num_used_symbols; ++i)
    // {
        // printf("0x%x %d\n", i, codewords[i].num_bits);
    // }

    // @Cleanup: debug only for checking prefix code reconstruction.
    /*
    for (int i = 0; i < ALPHABET_SIZE; ++i)
    {
        if (codewords[i].num_bits != 0)
            printf("0x%x %d %x\n", i, codewords[i].num_bits, codewords[i].bits);
    }
    */

    bitarray_t output = {0};

    // Send the code lengths to the ouput to be able to rebuild the prefix code when decompressing.
    for (uint32_t i = 0; i < ALPHABET_SIZE; ++i)
    {
        // Code lengths are from 0-32 as of now. So we only need 6 bits to encode one length.
        assert(codewords[i].num_bits <= MAX_CODE_LENGTH);
        bitarray_push_bits_msb(&output, codewords[i].num_bits, 6);
    }

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
    // @Todo:
    //  - check/test that the codes built are indeed canonical prefix codes.
    //  - lookup fast decoders and LUT ideas for fast decoding.
    // @Note: Huffman compression output has its last byte padded. Thus, when decompression it
    // is likely that few extra bytes, not originally present in the stream, are added (Max 7 more
    // if decoding 7 times a symbol with a code length 1). This won't be an issue when putting all
    // this together for Delfate.

    assert(size * 8 >= 256 * 6 && "Compressed data doesn't have a prefix code.");

    bitarray_t input = {
        .size = size * 8,
        .data = (uint8_t*)compressed_data,
    };

    codeword_t codewords[ALPHABET_SIZE];
    uint32_t bits_read = 0;
    for (uint32_t i = 0; i < ALPHABET_SIZE; ++i)
    {
        codewords[i].num_bits = bitarray_bits_msb(&input, bits_read, 6);
        bits_read += 6;
    }

    // Sort by length.
    uint32_t sorted[ALPHABET_SIZE];
    for (uint32_t i = 0; i < ALPHABET_SIZE; ++i)
    {
        sorted[i] = i;
    }

    for (size_t i = 0; i < ALPHABET_SIZE; ++i)
    {
        for (size_t j = 0; j < ALPHABET_SIZE - i - 1; ++j)
        {
            uint32_t f1 = codewords[sorted[j]].num_bits;
            uint32_t f2 = codewords[sorted[j + 1]].num_bits;
            // Sort by frequency and alphabetically. Important for canonical prefix codes.
            if (f1 < f2 || (f1 == f2 && sorted[j] > sorted[j + 1]))
            {
                size_t swap = sorted[j + 1];
                sorted[j + 1] = sorted[j];
                sorted[j] = swap;
            }
        }
    }

    // Rebuild prefix code.
    uint32_t code = 0;
    for (int i = ALPHABET_SIZE - 1; i >= 0; --i)
    {
        if (codewords[sorted[i]].num_bits == 0) continue;

        uint32_t length = codewords[sorted[i]].num_bits;
        // @Todo: add one more element to avoid the overflow condition ?
        uint32_t next_length = i == 0 ? length : codewords[sorted[i - 1]].num_bits;

        // codewords[sorted[i]].bits = code;
        codewords[sorted[i]].bits = code;
        code = (code + 1) << (next_length - length);
    }

    // @Cleanup: debug only for checking prefix code reconstruction.
    /*
    for (int i = 0; i < ALPHABET_SIZE; ++i)
    {
        if (codewords[i].num_bits != 0)
            printf("0x%x %d %x\n", i, codewords[i].num_bits, codewords[i].bits);
    }
    */

    // Bruteforce decompression to quicky test things out O(N*M). N=bits_count, M=ALPHABET_SIZE.
    // @Todo: change! This is dirty and really slow but proves that decoding works fine.
    uint8_t* output = NULL; // Array.
    uint32_t bit = 0;
    uint32_t len = 0;
    uint8_t tmp[ALPHABET_SIZE]; memset(tmp, 1, sizeof(tmp));
    while (bits_read < size * 8)
    {
        bit = bitarray_bit(&input, bits_read);
        bits_read++;
        len++;

        uint32_t count = 0;
        uint32_t idx = 0;
        for (uint32_t i = 0; i < ALPHABET_SIZE; ++i)
        {
            if (tmp[i] == 0) continue;

            if (codewords[i].num_bits == 0)
            {
                tmp[i] = 0;
                continue;
            }

            if (bit == ((codewords[i].bits >> (codewords[i].num_bits - len)) & 0x1))
            {
                count++;
                idx = i;
            }
            else
            {
                tmp[i] = 0;
            }
        }

        if (count == 1)
        {
            array_push(output, (uint8_t)idx);
            len = 0;
            memset(tmp, 1, sizeof(tmp));
        }
    }

    return output;
}

