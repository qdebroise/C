// LZ77 is a lossless dictionary based compression algorithm using a sliding window.
//
// == References ==
//
// - https://courses.cs.duke.edu//spring03/cps296.5/papers/ziv_lempel_1977_universal_algorithm.pdf
// - https://docs.microsoft.com/en-us/openspecs/windows_protocols/ms-wusp/fb98aa28-5cd7-407f-8869-a6cef1ff1ccb?redirectedfrom=MSDN
// - https://web.stanford.edu/class/ee376a/files/irena_lecture.pdf
// - http://jens.quicknote.de/comp/LZ77-JensMueller.pdf
// - http://michael.dipperstein.com/lzss/
//
// == Algorithm ==
//
// The LZ77 algorithm works by replacing uncompressed sequences by references to identical sequences that has already been
// compressed. The reference is a triplet with a backward offset indicating the position of the sequence already observed from
// the current position in the uncompressed stream, the length of the sequence and the next first character that isn't matching the sequence.
//
// The algorithm uses a sliding window that is composed of two parts. The left side of the sliding window is the search buffer. It contains
// data from the uncompressed stream that has already been compressed. It is the search buffer that is analyzed to find already seen sequences
// and replace them with a reference. The right part of the sliding window is the look-ahead buffer and it contains uncompressed data that
// hasn't been read yet. The sliding window is of fixed size but the two parts aren't necessarily of the same size.
//
// ---
//
// The sliding window is divided into two parts: the search buffer and the look-ahead buffer.
// The look-ahead buffer contains the next visible bytes in the input to be encoded.
// The search buffer is a memory of the input bytes already encoded.
//
// The emitted triplet is on 24 bits (3 bytes). A full byte is dedicated to the next character.
// This leaves 16 bits to encode both the backward offset and the match length.
// The repartition is as follows, 12 bits for the backward offset and 4 bits for the match length. This
// gives us a search window of 2^12 = 4096 bytes and a maximum match length of 2^4 = 16 bytes.
//
// +-------+----+
// |       |abra|cadabra        -> (0, 0, a) -> a
//
//  +-------+----+
//  |      a|brac|adabra        -> (0, 0, b) -> b
//
//   +-------+----+
//   |     ab|raca|dabra        -> (0, 0, r) -> r
//
//    +-------+----+
//    |    abr|acad|abra        -> (3, 1, c) -> ac // First symbol sequence found in the dictionnary.
//
//      +-------+----+
//      |  abrac|adab|ra        -> (2, 1, d) -> ad // Go to the closest 'a' character in the dictionnary.
//
//        +-------+----+
//        |abracad|abra|        -> (7, 4, NULL) -> abra

#include "lz77.h"

#include "array.h"
#include "bitarray.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>

#define ENCODED_LENGTH_BITS 4   // Number of bits used to encode the length of a reference.
#define ENCODED_OFFSET_BITS 12  // Number of bits used to encode the offset of a reference.

_Static_assert(ENCODED_OFFSET_BITS + ENCODED_LENGTH_BITS == 16, "The pair (offset, length) must fit exactly on 2 bytes.");

static const size_t LOOK_AHEAD_BUFFER_SIZE = 1 << ENCODED_LENGTH_BITS;
static const size_t SEARCH_BUFFER_SIZE = 1 << ENCODED_OFFSET_BITS;

static inline size_t min(size_t a, size_t b)
{
    return a < b ? a : b;
}

// Find the longest prefix of `pattern` into `str`. This is a brute force method in O(n*m).
//
// @Note: this is designed for the LZ77 algorithm and cannot be reused as is externally.
//
// The specificities are the following:
//  - when there are several same length matches then the one at the end of `str` is preferred since
//  it will be the closest to the look-ahead buffer.
//  - The search can go beyong `str` size to allow overlapping the look-ahead buffer. The only constraint
//  is the match must not be longer than `pattern_size`.
static void find_prefix(
    const uint8_t* pattern, size_t pattern_size,
    const uint8_t* str, size_t str_size,
    size_t* match_offset, size_t* match_length)
{
    size_t best_match_offset = 0;
    size_t best_match_length = 0;

    for (int i = 0; i < str_size; ++i)
    {
        if (pattern[0] != str[i]) continue;

        size_t length = 1;
        while (pattern[length] == str[i + length] && length < pattern_size - 1)
        {
            ++length;
        }

        if (length >= best_match_length)
        {
            best_match_length = length;
            best_match_offset = i;
        }
    }

    *match_offset = best_match_offset;
    *match_length = best_match_length;
}

static void emit_triplet(uint8_t** compressed_data, size_t offset, size_t length, uint8_t next_byte)
{
    assert(offset <= SEARCH_BUFFER_SIZE);
    assert(length <= LOOK_AHEAD_BUFFER_SIZE);

    uint16_t truncated_offset = (SEARCH_BUFFER_SIZE - 1) & offset;
    uint16_t truncated_length = (LOOK_AHEAD_BUFFER_SIZE - 1) & length;
    uint16_t combined = (truncated_offset << ENCODED_LENGTH_BITS) | truncated_length;

    uint8_t low = combined & 0x00ff;
    uint8_t high = (combined & 0xff00) >> 8;

    array_push(*compressed_data, high);
    array_push(*compressed_data, low);
    array_push(*compressed_data, next_byte);
}

uint8_t* lz77_compress(const uint8_t* data, size_t size)
{
    uint8_t* compressed_data = NULL; // Array.
    bitarray_t* ba = NULL; // Bit array.

    size_t bits = 0; // Tmp, count emitted bits.

    size_t index = 0;
    while (index < size)
    {
        const uint8_t* current = data + index;
        // Here, we compute how many elements are in the search buffer and in the look-ahead buffer.
        // - The search buffer is full most of the time except at the start of the compression where it is empty and fills as the sliding window moves.
        // - The look-ahead buffer, at the opposite, starts to empty towards the end of the compression process.
        //
        //                                  search_buffer_ptr
        //  search_buffer_ptr/current               |  current
        //              |                           |    |
        //       +-----+v--+                      +-v---+v--+
        //       |     |abc|def                   | abcd|ef |
        // Size: |[ 0 ]|[3]|                      |[ 4 ]|[2]|
        //
        size_t look_ahead_buffer_content_size = min(LOOK_AHEAD_BUFFER_SIZE, size - index);
        size_t search_buffer_content_size = min(index, SEARCH_BUFFER_SIZE);
        const uint8_t* search_buffer_ptr = current - search_buffer_content_size;

        size_t match_offset, match_length;
        find_prefix(current, look_ahead_buffer_content_size, search_buffer_ptr, search_buffer_content_size, &match_offset, &match_length);

        // Match length 1 -> 1b + 3*8b = 25b VS 1*9b = 9b       25 - 9  Bad
        // Match length 2 -> 1b + 3*8b = 25b VS 2*9b = 18b      25 - 18 Bad
        // Match length 3 -> 1b + 3*8b = 25b VS 2*9b = 27b      25 - 27 Good -> start encoding pair <length, offset> when match length is at least 3 caracters long.
        //

        // if (match_length == 0)
        if (match_length < 3)
        {
            match_length = 0;
            bitarray_push(ba, 0);
            bitarray_push_bits_lsb(ba, *current, 8);
            // emit_triplet(&compressed_data, 0, 0, *current);
        }
        else
        {
            // The match offset must be backward from the end of the search buffer.
            match_offset = search_buffer_content_size - match_offset;
            // If we reached the end of the data stream then the next byte in the triplet is NULL.
            uint8_t next_byte = current + match_length == current + size ? 0 : current[match_length];
            // emit_triplet(&compressed_data, match_offset, match_length, next_byte);

            uint16_t truncated_offset = (SEARCH_BUFFER_SIZE - 1) & match_offset; // 12 bits
            uint16_t truncated_length = (LOOK_AHEAD_BUFFER_SIZE - 1) & match_length; // 4 bits
            uint16_t combined = (truncated_offset << ENCODED_LENGTH_BITS) | truncated_length;

            /*
            uint8_t low = combined & 0x00ff;
            uint8_t high = (combined & 0xff00) >> 8;

            array_push(*compressed_data, high);
            array_push(*compressed_data, low);
            array_push(*compressed_data, next_byte);
            */

            bitarray_push(ba, 1);
            bitarray_push_bits_lsb(ba, truncated_offset, 12);
            bitarray_push_bits_lsb(ba, truncated_length, 4);
            bitarray_push_bits_lsb(ba, next_byte, 8);
        }

        index += match_length + 1;
    }

    printf("===> %d\n", bitarray_size(ba) / 8);

    // return compressed_data;
    return ba;
}

uint8_t* lz77_uncompress(const uint8_t* compressed_data, size_t size)
{
    uint8_t* data = NULL; // Array.

    for (size_t i = 0; i < bitarray_size(compressed_data);)
    {
        if (bitarray_bit(compressed_data, i) == 0)
        {
            uint8_t byte = bitarray_bits_lsb(compressed_data, i + 1, 8);
            i += 9;
            array_push(data, byte);
        }
        else
        {
            uint16_t offset = bitarray_bits_lsb(compressed_data, i + 1, 12);
            uint16_t length = bitarray_bits_lsb(compressed_data, i + 13, 4);
            uint8_t byte = bitarray_bits_lsb(compressed_data, i + 17, 8);
            i += 25;

            const uint8_t* it = data + array_size(data) - offset;
            const uint8_t* end = it + length;
            while (it != end)
            {
                array_push(data, *it);
                ++it;
            }

            array_push(data, byte);
        }
    }

    /*
    for (const uint8_t* byte = compressed_data; byte < compressed_data + size; byte+=3)
    {
        uint16_t combined = (byte[0] << 8) | byte[1];
        uint8_t next_byte = byte[2];

        uint16_t offset = combined >> ENCODED_LENGTH_BITS;
        uint16_t length = combined & (LOOK_AHEAD_BUFFER_SIZE - 1);

        const uint8_t* it = data + array_size(data) - offset;
        const uint8_t* end = it + length;
        while (it != end)
        {
            array_push(data, *it);
            ++it;
        }

        array_push(data, next_byte);
    }
    */

    return data;
}


#include <stdio.h>

// https://go-compression.github.io/algorithms/lzss/
// https://gist.github.com/fogus/5401265
// https://github.com/cstdvd/lz77

int main(int argc, char* argv[])
{
    // static const char str[] = "abcabcabcabc";
    // static const char str[] = "abracadabra";
    // static const char str[] = "les chaussettes de l'archiduchesse sont elles seches archiseches.";
    // static const char str[] = "aacaacabcabaaac";
    // static const size_t size = sizeof(str) / sizeof(str[0]);

    FILE* f = fopen("trythemsource.txt", "rb");
    if (!f) return 1;
    fseek(f, 0, SEEK_END);
    size_t end = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* content = malloc(end);
    fread(content, 1, end, f);
    fclose(f);
    uint8_t* compressed_data = lz77_compress(content, end);

    // printf("Compressing: \"%s\"\n\n", str);

    // uint8_t* compressed_data = lz77_compress(str, size);
    uint8_t* uncompressed_data = lz77_uncompress(compressed_data, array_size(compressed_data));

    printf("Compressed data stream:\n");
    /*
    for (uint8_t* byte = compressed_data; byte < compressed_data + array_size(compressed_data); ++byte)
    {
        printf("0x%x ", *byte);
    }
    printf("\nCompressed size: %lu bytes\n\n", array_size(compressed_data));
    */
    for (int i = 0; i < bitarray_size(compressed_data); i+=8)
    {
        uint8_t byte = bitarray_bits_lsb(compressed_data, i, 8);
        printf("0x%x ", byte);
    }
    printf("\nCompressed size: %lu bytes\n\n", bitarray_size(compressed_data) / 8);

    printf("Uncompressed data stream:\n");
    for (uint8_t* byte = uncompressed_data; byte < uncompressed_data + array_size(uncompressed_data); ++byte)
    {
        printf("%c", *byte);
    }
    printf("\nUncompressed size: %lu bytes\n", array_size(uncompressed_data));

    printf("\nCompression rate: %.3f%\n", (1 - (bitarray_size(compressed_data) / 8) / (float)array_size(uncompressed_data)) * 100);

    return 0;
}
