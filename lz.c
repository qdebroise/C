// References:
//  - https://courses.cs.duke.edu//spring03/cps296.5/papers/ziv_lempel_1977_universal_algorithm.pdf
//  - http://michael.dipperstein.com/lzss/
//
// https://github.com/ebiggers/libdeflate/blob/fbada10aa9da4ed8145a026a80cedff9601fb874/lib/hc_matchfinder.h

#include "lz.h"

#include "array.h"

#include <stdint.h>

// Use a 32K sliding window.
#define WIN_BITS 15
#define WIN_SIZE (1 << WIN_BITS)
#define WIN_MASK (WIN_SIZE - 1)

// Minimum match length required to ouptput a reference.
#define MIN_MATCH_LEN 3

// In the output stream a reference is coded on 24 bits (a 16 bits value and an 8 bits value).
// The 16 bits value holds the match offset on 15 bits. The extra bit is part of the match length
// which is coded on the next 8 bits + this extra bit.
//
// Thus, we have a match offset of 15 bits and a match length of 9 bits.
//
// |                 | 8 bits |
// |  16 bits value  | value  |  Memory layout
// +---------------+-+--------+
// |010101011011110|1|00010110|
// +---------------+-+--------+
// |    15 bits    |  9 bits  |  Actual representation
// |    offset     |  length  |
//
// @Todo: 9 bits for the length offers a max length of 512 but this is really huge
// and I think its is too much. 256 is already big. There is a better way to organize
// this. Like LZ4 sequences and stuff.
#define MATCH_OFFSET_BITS WIN_BITS
#define MATCH_OFFSET_MAX (1 << MATCH_OFFSET_BITS) - 1
#define MATCH_LENGTH_BITS 9
#define MATCH_LENGTH_MAX (1 << MATCH_LENGTH_BITS) - 1

#define HASHTABLE_EMPTY_BUCKET ((int16_t)(1ull << 15)) // Max negative value for a int16_t.

#define MIN(a, b) ((a) < (b) ? (a) : (b))

typedef struct lz_context_t lz_context_t;

struct lz_context_t
{
    const uint8_t* lookahead; // Pointer to the next bytes to compress.
    const uint8_t* input_end; // Pointer to the end of the input + 1.

    // Pointer from which bytes positions inside the sliding window are computed.
    const uint8_t* base;

    uint8_t* output; // Array.
    uint32_t flag; // Flag byte index (we cannot use direct pointer because it will be invalidated on output resize).
    uint8_t flag_count; // Flags written so far in the current flag byte.

    int16_t head[WIN_SIZE]; // Hashtable buckets.
    int16_t prev[WIN_SIZE]; // Hashtable linked list.

    // Specifies how far in the linked list we sould go when searching for a match.
    uint16_t match_search_depth;

    // Rolling hash to avoid recalculations. @Todo
    uint32_t rolling_hash;
};

static inline void init_lz_context(lz_context_t* ctx, const uint8_t* input, size_t size)
{
    *ctx = (lz_context_t){
        .lookahead = input,
        .input_end = input + size,
        .base = input,
        .output = NULL,
        .match_search_depth = 64, // @Todo: compression level selection.
        .rolling_hash = 0,
        .flag_count = 8, // So that it automatically "allocates" a new flag byte at the start.
    };

    for (int i = 0; i < WIN_SIZE; ++i)
    {
        ctx->head[i] = HASHTABLE_EMPTY_BUCKET;
        ctx->prev[i] = HASHTABLE_EMPTY_BUCKET;
    }
}

static inline uint32_t hash(const uint8_t* lookahead)
{
    // @Todo: rolling hash and/or better hashing :p.
    return ((3483  * ((uint32_t)*(lookahead + 0))) +
            (23081 * ((uint32_t)*(lookahead + 1))) +
            (6954  * ((uint32_t)*(lookahead + 2))));
}

static void reindex_hashtable(lz_context_t* ctx, uint32_t cur_relpos)
{
    for (uint32_t i = 0; i < WIN_SIZE; ++i)
    {
        // Negative values will end up outside the window so we directly set the
        // value to the empty value.
        if (ctx->head[i] < 0) ctx->head[i] = HASHTABLE_EMPTY_BUCKET;
        else ctx->head[i] -= cur_relpos;

        if (ctx->prev[i] < 0) ctx->prev[i] = HASHTABLE_EMPTY_BUCKET;
        else ctx->prev[i] -= cur_relpos;
    }
}

void find_longest_match(const lz_context_t* ctx, uint32_t* out_match_offset, uint32_t* out_match_length)
{
    uint32_t best_match_length = 0;
    uint32_t best_match_offset = 0;

    int16_t cur_relpos = ctx->lookahead - ctx->base;
    int16_t limit = cur_relpos - WIN_SIZE; // Don't search beyond the sliding window.
    uint16_t search_depth = ctx->match_search_depth;
    uint32_t max_length = MATCH_LENGTH_MAX;

    assert(cur_relpos >= 0 && "Lookahead pointer is behind the base pointer.");

    // Reduce the maximum match length as we approach the end of the input end.
    if (max_length > ctx->input_end - ctx->lookahead)
    {
        max_length = ctx->input_end - ctx->lookahead;
    }

    // When there is very little input left we only need to output literals so we can return
    // early. Furthermore, we avoid doing hash computation which would lead to invalid memory
    // reads beyond the input end.
    if (max_length < MIN_MATCH_LEN)
    {
        *out_match_offset = 0;
        *out_match_length = 0;
        return;
    }

    uint16_t slot = hash(ctx->lookahead) & WIN_MASK;

    const uint8_t* match = ctx->base + cur_relpos;
    int16_t match_pos = ctx->head[slot];

    while (match_pos > limit && search_depth-- != 0)
    {
        const uint8_t* candidate = ctx->base + match_pos;
        uint32_t candidate_length = 0;

        // @Todo @Performance: check first and last byte of best match for speedup. We can then start/stop one byte later/earlier.

        while (candidate_length < max_length && candidate[candidate_length] == match[candidate_length])
        {
            ++candidate_length;
        }

        if (candidate_length > best_match_length)
        {
            best_match_length = candidate_length;
            best_match_offset = cur_relpos - match_pos;
        }

        match_pos = ctx->prev[match_pos];
    }

    *out_match_offset = best_match_offset;
    *out_match_length = best_match_length;
}

// Saves `count` input bytes into the directionnary.
uint32_t record_bytes(lz_context_t* ctx, uint32_t num_bytes, uint32_t cur_relpos)
{
    assert(num_bytes > 0 && "Invalid number of bytes to record.");

    uint32_t relpos = cur_relpos;
    uint32_t remaining = num_bytes;
    uint32_t skip = 0;

    // We don't want to record values in the hashtable as we approach the end of
    // the input. Firstly, because we will only record literals from now and secondly,
    // we would need to compute a hash value using bytes beyond the input end.
    // To palliate this problem we simply reduce the number of remaining bytes to
    // actually record and for the rest simply skip the values.
    // @Todo @Performance: unlikely.
    if (ctx->lookahead + num_bytes + MIN_MATCH_LEN > ctx->input_end)
    {
        uint32_t penetration = ctx->lookahead + num_bytes + MIN_MATCH_LEN - ctx->input_end;
        skip = MIN(penetration, remaining);
        remaining -= (penetration > remaining ? remaining : penetration);
    }

    while (remaining--)
    {

        uint32_t slot = hash(ctx->lookahead) & WIN_MASK;
        ctx->prev[relpos] = ctx->head[slot];
        ctx->head[slot] = relpos;

        ctx->lookahead++;
        relpos++;

        if (relpos == MATCH_OFFSET_MAX)
        {
            reindex_hashtable(ctx, relpos);
            ctx->base = ctx->base + relpos;
            relpos = 0;
        }
    }

    while (skip--) // @Todo @Performance: unlikely.
    {
        ctx->lookahead++;
        relpos++;

        if (relpos == MATCH_OFFSET_MAX)
        {
            reindex_hashtable(ctx, relpos);
            ctx->base = ctx->base + relpos;
            relpos = 0;
        }
    }

    return relpos;
}

void emit_literal(lz_context_t* ctx, uint8_t byte)
{
    array_push(ctx->output, byte);
    ctx->flag_count++;
}

void emit_reference(lz_context_t* ctx, uint16_t offset, uint16_t length)
{
    // Offset is on 15 bits, length is on 9 bits. Store the length 9th bit in the highest bit of the offset 16 bit value.
    uint8_t shift = WIN_BITS;

    // @Todo: length range currently from 0 to 511, since we have a min length of 3 before emitting a reference we can
    // have lengths in the range 0 + MIN_LEN to 511 + MIN_LEN. We simply shift averything by MIN_LEN. Thus an encoded length
    // of 0 is actualy MIN_LEN and more generally an encoded length of L really is L + MIN_LEN.
    uint16_t len_extra_bit = (length & (1 << 8)) >> 8;
    uint8_t len = length & ((1 << 8) - 1);
    uint16_t off = (offset & WIN_MASK) | (len_extra_bit << shift);

    uint8_t b1 = len;
    uint8_t b2 = off >> 8;
    uint8_t b3 = off & ((1 << 8) - 1);

    array_push(ctx->output, b1);
    array_push(ctx->output, b2);
    array_push(ctx->output, b3);

    ctx->output[ctx->flag] |= (1 << ctx->flag_count);
    ctx->flag_count++;
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
//
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
// Match Length     Coded as literals           Coded as reference
//      1                   9                           25
//      2                   18                          25
//      3                   27                          25
//
// Therefore matches with a length lower than take less space when coded as single characters.
uint8_t* lz_compress(const uint8_t* input, size_t size)
{
    lz_context_t ctx;
    init_lz_context(&ctx, input, size);

    uint16_t cur_relpos = 0;
    uint32_t match_length;
    uint32_t match_offset;

    while (ctx.lookahead != ctx.input_end)
    {
        if (ctx.flag_count == 8)
        {
            ctx.flag_count = 0;
            ctx.flag = array_size(ctx.output);
            array_push(ctx.output, 0);
        }

        find_longest_match(&ctx, &match_offset, &match_length);

        if (match_length < MIN_MATCH_LEN)
        {
            // Emit literals.
            uint8_t num_literals = match_length == 0 ? 1 : match_length;
            for (uint8_t i = 0; i < num_literals; ++i)
            {
                emit_literal(&ctx, ctx.base[cur_relpos + i]);

                if (ctx.flag_count == 8)
                {
                    ctx.flag_count = 0;
                    ctx.flag = array_size(ctx.output);
                    array_push(ctx.output, 0);
                }
            }
            cur_relpos = record_bytes(&ctx, num_literals, cur_relpos);
        }
        else
        {
            // Emit reference.
            emit_reference(&ctx, match_offset, match_length);
            cur_relpos = record_bytes(&ctx, match_length, cur_relpos);
        }
    }

    return ctx.output;
}

uint8_t* lz_uncompress(const uint8_t* compressed_data, size_t size)
{
    uint8_t* data = NULL; // Array.

    uint8_t flags = compressed_data[0];
    uint8_t flag_count = 0;

    size_t i;
    for (i = 1; i < size;)
    {
        if ((flags >> flag_count) & 0x1)
        {
            uint16_t len = compressed_data[i++];
            uint16_t high = compressed_data[i++];
            uint16_t low = compressed_data[i++];
            uint16_t off = (high << 8) | low;

            uint8_t shift = WIN_BITS;

            uint16_t length = len | ((off >> shift) << 8);
            uint16_t offset = off & ((1 << shift) - 1);

            array_reserve(data, array_size(data) + length);
            const uint8_t* it = data + array_size(data) - offset; // Not -1 because offset 0 is current char.
            const uint8_t* end = it + length;
            while (it != end)
            {
                array_push(data, *it);
                ++it;
            }
        }
        else
        {
            uint8_t byte = compressed_data[i++];
            array_push(data, byte);
        }

        flag_count++;
        if (flag_count == 8)
        {
            flags = compressed_data[i++];
            flag_count = 0;
        }
    }

    return data;
}
