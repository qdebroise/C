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
    uint32_t sorted[ALPHABET_SIZE]; // Sorted frequencies.
    uint32_t sorted_indices[ALPHABET_SIZE]; // Sorted indices of frequencies.
    uint32_t num_used_symbols;
} frequencies_t;

typedef struct codeword_t
{
    uint8_t num_bits;
    uint32_t bits;
} codeword_t;

// @Todo @Performance: we use the indirection quite a lot like lengths[sorted[i]], freqs[sorted[i]],
// etc. This cause quite a lot of jumps all over the place in memory. Alphabets are rather small,
// for my use case anyway so copying the frequencies in a sorted array should be rather cheap and
// faster to use afterwards. Then we only need use the indirection when building codelengths for
// symbols. Test and profile this.
// @Todo: better sorting.
// @Todo: improve the overall aspect of this function. Some things can be simplified.
void frequencies_count_and_sort(const uint8_t* input, size_t input_size, frequencies_t* freq)
{
    uint32_t count[ALPHABET_SIZE] = {0}; // Frequency of every symbol in the alphabet.
    for (size_t i = 0; i < input_size; ++i)
    {
        count[input[i]]++;
    }

    // Sorting initialization.
    for (int i = 0; i < ALPHABET_SIZE; ++i)
    {
        freq->sorted_indices[i] = i;
    }

    // Sort in ascending order, first by frequency then by symbol lexicographic order.
    for (size_t i = 0; i < ALPHABET_SIZE; ++i)
    {
        for (size_t j = 0; j < ALPHABET_SIZE - i - 1; ++j)
        {
            uint32_t f1 = count[freq->sorted_indices[j]];
            uint32_t f2 = count[freq->sorted_indices[j + 1]];
            // Sort by frequency and alphabetically. Important for canonical prefix codes.
            if (f1 > f2 || (f1 == f2 && freq->sorted_indices[j] < freq->sorted_indices[j + 1]))
            {
                size_t swap = freq->sorted_indices[j + 1];
                freq->sorted_indices[j + 1] = freq->sorted_indices[j];
                freq->sorted_indices[j] = swap;
            }
        }
    }

    for (size_t i = 0; i < ALPHABET_SIZE; ++i)
    {
        freq->sorted[i] = count[freq->sorted_indices[i]];
    }

    for (size_t i = 0; i < ALPHABET_SIZE; ++i)
    {
        if (count[freq->sorted_indices[i]] != 0)
        {
            freq->num_used_symbols = ALPHABET_SIZE - i;
            break;
        }
    }
}

// Takes the output of package-merge and transforms it to an array compatible with the prefix-code
// generation algorithm (see `package_merge.c` for more details). It converts the number of active
// leaves to a symbol count per code length.
// The ouput array `symbol_count_per_length` is the symbol count where the index is the code length.
// Index 0 is unused because no used symbol have a code length of 0.
void count_symbols_with_length(const uint32_t* active_leaves, uint8_t limit, uint32_t* count_per_length)
{
    uint8_t code_length = limit;
    count_per_length[code_length--] = active_leaves[0];
    for (uint8_t i = 1; i < limit; ++i)
    {
        count_per_length[code_length] = active_leaves[i] - active_leaves[i - 1];
        code_length--;
    }
}

// Assign code lengths to every symbol in the alphabet.
void assign_code_lengths(const uint32_t* sorted_indices, const uint32_t* count_per_length, codeword_t* codewords)
{
    int32_t sorted_index = ALPHABET_SIZE - 1;

    for (uint32_t code_length = 1; code_length <= MAX_CODE_LENGTH; ++code_length)
    {
        uint32_t symbols_left = count_per_length[code_length];
        while (symbols_left--)
        {
            codewords[sorted_indices[sorted_index]].num_bits = code_length;
            sorted_index--;
        }
    }

    // Fill the remaining with zeros.
    while (sorted_index >= 0)
    {
        codewords[sorted_indices[sorted_index]].num_bits = 0;
        sorted_index--;
    }
}

// Constructs a canonical prefix code.
//
// A canonical prefix code is a specific code amongst the several possibilities for a given alphabet
// and a set of frequencies. This prefix code fits additional rules making it very easy to describe
// in a compact format.
//
// The generation algorithm is from the Deflate's RFC section 3.2.2.
// https://datatracker.ietf.org/doc/html/rfc1951
//
// Basically, for each code length we compute its starting code. Then, knowing this, we can easily
// iterate over the alphabet and attribute every symbol a code given its code length using the array
// computed at the step before.
void generate_prefix_code(const uint32_t* count_per_length, codeword_t* codewords, uint8_t max_len, size_t alphabet_size)
{
    // Find the starting code value for every code length (code lengths of 0 are represented).
    uint32_t next_codes[MAX_CODE_LENGTH + 1] = {0};
    uint32_t code = 0;
    for (uint32_t bits = 1; bits <= max_len; ++bits)
    {
        code = (code + count_per_length[bits - 1]) << 1;
        next_codes[bits] = code;
    }

    // For each symbol, lookup its code length and assign it the next code for this given length.
    for (uint32_t i = 0; i < alphabet_size; ++i)
    {
        uint32_t num_bits = codewords[i].num_bits;
        if (num_bits != 0)
        {
            codewords[i].bits = next_codes[num_bits - 1]++;
        }
    }
}

uint8_t* encode_input(codeword_t* codewords, size_t alphabet_size, const uint8_t* input, size_t size)
{
    bitarray_t output = {0};

    // Send the code lengths to the ouput to be able to rebuild the prefix code when decompressing.
    for (uint32_t i = 0; i < alphabet_size; ++i)
    {
        // Code lengths are from 0-31 as of now. So we only need 6 bits to encode one length.
        assert(codewords[i].num_bits <= MAX_CODE_LENGTH);
        bitarray_push_bits_msb(&output, codewords[i].num_bits, 6);
    }

    const uint8_t* it = input;
    const uint8_t* end = input + size;
    while (it != end)
    {
        // @Todo @Performance: see if it is better to have a buffer of bits on a uint64_t and
        // push only when it is full. More generally, lookup fast bit IO methods.
        bitarray_push_bits_msb(&output, codewords[*it].bits, codewords[*it].num_bits);
        it++;
    }

    bitarray_pad_last_byte(&output);
    return output.data;
}

uint8_t* huffman_compress(const uint8_t* input, size_t size)
{
    frequencies_t freq;
    frequencies_count_and_sort(input, size, &freq);

    // Feed package-merge only the frequencies that are non-zero.
    uint32_t active_leaves[MAX_CODE_LENGTH] = {0};
    package_merge(&freq.sorted[ALPHABET_SIZE - freq.num_used_symbols], freq.num_used_symbols, MAX_CODE_LENGTH, active_leaves);

    uint32_t count_per_length[MAX_CODE_LENGTH + 1] = {0};
    count_symbols_with_length(active_leaves, MAX_CODE_LENGTH, count_per_length);

    codeword_t codewords[ALPHABET_SIZE];
    assign_code_lengths(freq.sorted_indices, count_per_length, codewords);
    generate_prefix_code(count_per_length, codewords, MAX_CODE_LENGTH, ALPHABET_SIZE);

#if 0
    // @Cleanup: debug only for checking prefix code reconstruction.
    for (int i = 0; i < ALPHABET_SIZE; ++i)
    {
        if (codewords[i].num_bits != 0)
            printf("0x%x %d %x\n", i, codewords[i].num_bits, codewords[i].bits);
    }
#endif

    return encode_input(codewords, ALPHABET_SIZE, input, size);
}

uint8_t* huffman_uncompress(const uint8_t* compressed_data, size_t size)
{
    // @Todo:
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

    // Tally the number of symbols per code length.
    uint32_t l[MAX_CODE_LENGTH] = {0};
    codeword_t codewords[ALPHABET_SIZE] = {0};
    uint32_t bits_read = 0;
    for (uint32_t i = 0; i < ALPHABET_SIZE; ++i)
    {
        uint32_t len = bitarray_bits_msb(&input, bits_read, 6);
        if (len == 0)
        {
            codewords[i].num_bits = 0;
        }
        else
        {
            codewords[i].num_bits = len;
            l[len - 1]++;
        }
        bits_read += 6;
    }

    generate_prefix_code(l, codewords, MAX_CODE_LENGTH, ALPHABET_SIZE);

#if 0
    // @Cleanup: debug only for checking prefix code reconstruction.
    for (int i = 0; i < ALPHABET_SIZE; ++i)
    {
        if (codewords[i].num_bits != 0)
            printf("0x%x %d %x\n", i, codewords[i].num_bits, codewords[i].bits);
    }
#endif

    // Bruteforce decompression to quicky test things.
    // @Todo: change this, it is only for testing purposes! This is dirty and really slow but proves
    // that decoding works fine.
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

