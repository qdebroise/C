// References:
//  - https://courses.cs.duke.edu//spring03/cps296.5/papers/ziv_lempel_1977_universal_algorithm.pdf
//  - http://michael.dipperstein.com/lzss/
//

#include "lz.h"

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
//
// @Todo @Performance: change this function into something faster. Typically, see about using hashtables for fast prefix lookup.
static void _find_prefix(
    const uint8_t* pattern, size_t pattern_size,
    const uint8_t* str, size_t str_size,
    size_t* match_offset, size_t* match_length)
{
    size_t best_match_offset = 0;
    size_t best_match_length = 0;

    for (size_t i = 0; i < str_size; ++i)
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

// =====================
// ===     LZ77      ===
// =====================
//
// == Algorithm ==
//
// LZ77 is a lossless dictionary based compression algorithm using a sliding window.
//
// The LZ77 algorithm works by replacing uncompressed sequences by references to identical sequences already observed and compressed.
//
// The sliding window is divided into two parts: the search buffer and the look-ahead buffer.
// The look-ahead buffer contains the next visible bytes in the input to be encoded.
// The search buffer is a memory of the input bytes already encoded.
//
// The emitted triple is on 24 bits (3 bytes). A full byte is dedicated to the next character.
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


static void _emit_triple(uint8_t** compressed_data, size_t offset, size_t length, uint8_t next_byte)
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
        _find_prefix(current, look_ahead_buffer_content_size, search_buffer_ptr, search_buffer_content_size, &match_offset, &match_length);

        if (match_length == 0)
        {
            _emit_triple(&compressed_data, 0, 0, *current);
        }
        else
        {
            // The match offset must be backward from the end of the search buffer.
            match_offset = search_buffer_content_size - match_offset;
            // If we reached the end of the data stream then the next byte in the triple is NULL.
            uint8_t next_byte = current + match_length == current + size ? 0 : current[match_length];
            _emit_triple(&compressed_data, match_offset, match_length, next_byte);
        }

        index += match_length + 1;
    }

    return compressed_data;
}

uint8_t* lz77_uncompress(const uint8_t* compressed_data, size_t size)
{
    uint8_t* data = NULL; // Array.

    for (const uint8_t* byte = compressed_data; byte < compressed_data + size; byte+=3)
    {
        uint16_t combined = (byte[0] << 8) | byte[1];
        uint8_t next_byte = byte[2];

        uint16_t offset = combined >> ENCODED_LENGTH_BITS;
        uint16_t length = combined & (LOOK_AHEAD_BUFFER_SIZE - 1);

        // We reserve the required space up front. We cannot rely on `array_push()` to
        // reserve memory as it can modifiy the `data` pointer and thus invalidates `it` and `end`.
        array_reserve(data, array_size(data) + length);

        const uint8_t* it = data + array_size(data) - offset;
        const uint8_t* end = it + length;
        while (it != end)
        {
            array_push(data, *it);
            ++it;
        }

        array_push(data, next_byte);
    }

    return data;
}

// =====================
// ===     LZSS      ===
// =====================
//
// LZSS is an amelioration of LZ77 with the following improvements:
//
// LZSS uses a single bit to determine wether the following symbol is a single character or a pair,length distance.
// This allows to save quite a bit of memory as we no longer need to store a full triple when no matches were found.
//
// LZSS prohibe matches with a length smaller than 3. Indeed if we need 1 bit to determine the type of what follows
// in the stream and a character is a byte on 8 bits. Then we can easily see that:
// - A match of length N uses '1*N + 8*N' bits if coded as a sequence of single characters.
// - A match of length N uses '1 + 3*8' bits if coded as a distance,length pair.
// Thus we have the following table:
//
// Match Length     Coded as characters     Coded as distance,length pair
//      1                   9                           25
//      2                   18                          25
//      3                   27                          25
//
// Therefore matches with a length lower than take less space when coded as single characters.

#define LZSS_MIN_MATCH_LENGTH 3

uint8_t* lzss_compress(const uint8_t* data, size_t size)
{
    bitarray_t compressed_data = {0}; // Bit array.

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
        _find_prefix(current, look_ahead_buffer_content_size, search_buffer_ptr, search_buffer_content_size, &match_offset, &match_length);

        if (match_length < LZSS_MIN_MATCH_LENGTH)
        {
            bitarray_push(&compressed_data, 0);
            bitarray_push_bits_lsb(&compressed_data, *current, 8);

            index += 1;
        }
        else
        {
            // The match offset must be backward from the end of the search buffer.
            match_offset = search_buffer_content_size - match_offset;

            uint16_t truncated_offset = (SEARCH_BUFFER_SIZE - 1) & match_offset; // 12 bits
            uint16_t truncated_length = (LOOK_AHEAD_BUFFER_SIZE - 1) & match_length; // 4 bits

            bitarray_push(&compressed_data, 1);
            bitarray_push_bits_lsb(&compressed_data, truncated_offset, 12);
            bitarray_push_bits_lsb(&compressed_data, truncated_length, 4);

            index += match_length;
        }
    }

    return compressed_data.data;
}

uint8_t* lzss_uncompress(const uint8_t* compressed_data, size_t size)
{
    uint8_t* data = NULL; // Array.

    const bitarray_t ba = {
        .size = size * 8,
        .data = (uint8_t*)compressed_data,
    };

    for (size_t i = 0; i < ba.size;)
    {
        if (bitarray_bit(&ba, i) == 0)
        {
            uint8_t byte = bitarray_bits_lsb(&ba, i + 1, 8);
            array_push(data, byte);
            i += 9;
        }
        else
        {
            uint16_t offset = bitarray_bits_lsb(&ba, i + 1, 12);
            uint16_t length = bitarray_bits_lsb(&ba, i + 13, 4);
            i += 17;

            // We reserve the required space up front. We cannot rely on `array_push()` to
            // reserve memory as it can modifiy the `data` pointer and thus invalidates `it` and `end`.
            array_reserve(data, array_size(data) + length);

            const uint8_t* it = data + array_size(data) - offset;
            const uint8_t* end = it + length;
            while (it != end)
            {
                array_push(data, *it);
                ++it;
            }
        }
    }

    return data;
}
