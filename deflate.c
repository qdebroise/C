#include "deflate.h"

#include "bitarray.h"

// [0-255] maps to the regular ASCII character.
// 256 is the EOF symbol.
// [257-285] maps to code lengths.
#define LIT_LEN_ALPHABET_SIZE 285
// Distance alphabet (see Deflate rfc for what symbol corresponds to which real offset).
#define OFFSET_ALPHABET_SIZE 30

// @Todo: next steps are:
// - count symbols as they appear during lz compression.
// - build sequences
// - create & generate offset table (the one from the specs) 32K memory but O(1) lookups, can we do better ?


//
// LZ77
//

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

typedef struct sequence_t
{
    uint32_t start_index;
    uint32_t num_literals;
    uint32_t match_length;
    uint32_t match_offset;
} sequence_t;

struct lz_context_t
{
    const uint8_t* lookahead; // Pointer to the next bytes to compress.
    const uint8_t* input_end; // Pointer to the end of the input + 1.

    // Pointer from which bytes positions inside the sliding window are computed.
    const uint8_t* base;

    sequence_t* sequences; // New output.
    sequence_t  sequence;

    int16_t head[WIN_SIZE]; // Hashtable buckets.
    int16_t prev[WIN_SIZE]; // Hashtable linked list.

    // Specifies how far in the linked list we sould go when searching for a match.
    uint16_t match_search_depth;

    // Rolling hash to avoid recalculations. @Todo
    uint32_t rolling_hash;

    uint32_t lit_len_count[LIT_LEN_ALPHABET_SIZE];
    uint32_t dist_count[OFFSET_ALPHABET_SIZE];
};

static inline void lz_start_new_sequence(lz_context_t* ctx, const uint8_t* input)
{
    ctx->sequence.start_index = ctx->lookahead - input;
    ctx->sequence.num_literals = 0;
    ctx->sequence.match_length = 0;
    ctx->sequence.match_offset = 0;
}

static inline void lz_init_context(lz_context_t* ctx, const uint8_t* input, size_t size)
{
    *ctx = (lz_context_t){
        .lookahead = input,
        .input_end = input + size,
        .base = input,
        .sequences = NULL,
        .match_search_depth = 64, // @Todo: compression level selection.
        .rolling_hash = 0,
    };

    lz_start_new_sequence(ctx, input);

    for (int i = 0; i < WIN_SIZE; ++i)
    {
        ctx->head[i] = HASHTABLE_EMPTY_BUCKET;
        ctx->prev[i] = HASHTABLE_EMPTY_BUCKET;
    }
}

static inline uint32_t lz_hash(const uint8_t* lookahead)
{
    // @Todo: rolling hash and/or better hashing :p.
    return ((3483  * ((uint32_t)*(lookahead + 0))) +
            (23081 * ((uint32_t)*(lookahead + 1))) +
            (6954  * ((uint32_t)*(lookahead + 2))));
}

static void lz_reindex_hashtable(lz_context_t* ctx, uint32_t cur_relpos)
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

void lz_find_longest_match(const lz_context_t* ctx, uint32_t* out_match_offset, uint32_t* out_match_length)
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

    uint16_t slot = lz_hash(ctx->lookahead) & WIN_MASK;

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
uint32_t lz_record_bytes(lz_context_t* ctx, uint32_t num_bytes, uint32_t cur_relpos)
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

        uint32_t slot = lz_hash(ctx->lookahead) & WIN_MASK;
        ctx->prev[relpos] = ctx->head[slot];
        ctx->head[slot] = relpos;

        ctx->lookahead++;
        relpos++;

        if (relpos == MATCH_OFFSET_MAX)
        {
            lz_reindex_hashtable(ctx, relpos);
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
            lz_reindex_hashtable(ctx, relpos);
            ctx->base = ctx->base + relpos;
            relpos = 0;
        }
    }

    return relpos;
}

void lz_emit_literal(lz_context_t* ctx, uint8_t byte)
{
    ctx->sequence.num_literals++;
}

void lz_emit_reference(lz_context_t* ctx, uint16_t offset, uint16_t length)
{
    // @Todo: since we have a min length of 3 before emitting a reference we can have lengths in the
    // range 0 + MIN_LEN to THEORETICAL_MAX_LEN + MIN_LEN. We simply shift everything by MIN_LEN.
    // Thus an encoded length of 0 is actualy MIN_LEN and more generally an encoded length of L
    // really is L + MIN_LEN.
    ctx->sequence.match_length = length;
    ctx->sequence.match_offset = offset;
    array_push(ctx->sequences, ctx->sequence);
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
// uint8_t* lz_compress(const uint8_t* input, size_t size)
sequence_t* lz_compress(const uint8_t* input, size_t size)
{
    lz_context_t ctx;
    lz_init_context(&ctx, input, size);

    uint16_t cur_relpos = 0;
    uint32_t match_length;
    uint32_t match_offset;

    while (ctx.lookahead != ctx.input_end)
    {
        /*
        if (ctx.flag_count == 8)
        {
            ctx.flag_count = 0;
            ctx.flag = array_size(ctx.output);
            array_push(ctx.output, 0);
        }
        */

        lz_find_longest_match(&ctx, &match_offset, &match_length);

        if (match_length < MIN_MATCH_LEN)
        {
            // Emit literals.
            uint8_t num_literals = match_length == 0 ? 1 : match_length;
            for (uint8_t i = 0; i < num_literals; ++i)
            {
                lz_emit_literal(&ctx, ctx.base[cur_relpos + i]);

                /*
                if (ctx.flag_count == 8)
                {
                    ctx.flag_count = 0;
                    ctx.flag = array_size(ctx.output);
                    array_push(ctx.output, 0);
                }
                */
            }
            cur_relpos = lz_record_bytes(&ctx, num_literals, cur_relpos);
        }
        else
        {
            // Emit reference.
            lz_emit_reference(&ctx, match_offset, match_length);
            lz_start_new_sequence(&ctx, input);
            cur_relpos = lz_record_bytes(&ctx, match_length, cur_relpos);
        }
    }

    // return ctx.output;
    return ctx.sequences;
}

//
// Deflate
//

// Deflate decoding tables from the specs section 3.2.5.
// https://datatracker.ietf.org/doc/html/rfc1951
static const uint32_t CODE_LENGTHS_CODEX[] =
    {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131,
    163, 195, 227, 258};

static const uint32_t CODE_LENGTHS_EXTRA_BITS[] =
    {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};

static const uint32_t OFFSETS_CODEX[] =
    {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537,
    2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};

static const uint32_t OFFSETS_EXTRA_BITS[] =
    {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13,
    13};

enum
{
    // BFINAL
    DEFLATE_NO_FINAL_BLOCK = 0,
    DEFLATE_FINAL_BLOCK = 1,

    // BTYPE
    DEFLATE_NO_COMPRESSION = 0,
    DEFLATE_FIXED_CODES = 1,
    DEFLATE_DYNAMIC_CODES = 2,
    DEFLATE_ERROR = 3,
};

void deflate_compress(const uint8_t* input, size_t size)
{
    bitarray_t output = {0};

    // Each Deflate block starts with a 3 bits header.
    // First bit BFINAL is set when it is the last block of the data stream.
    // The next 2 bits BTYPE specifies how the data is compressed.
    //  - 00 -> no compression
    //  - 01 -> compressed with fixed Huffman codes
    //  - 10 -> compressed with dynamic Huffman codes.
    //  - 11 -> reserved (error).

    // Compress a single block.
    uint64_t header = (DEFLATE_FINAL_BLOCK << 2) | (DEFLATE_DYNAMIC_CODES);
    bitarray_push_bits_msb(&output, header, 3);

    // @Note: extra bits are written MSB first.

    // Block format: see Deflate specs section 3.2.7.

    // @Note: LZ77 intermediate representation format with sequences, # literals + match -> feed to the huffman is list of sequences.
    // lz_compress(&deflate_ctx.lz_context, input, size);
}

void deflate_uncompress(const uint8_t* input, size_t size)
{
    /*
    do
    {
        uint32_t bfinal = bitarray_bit_msb(input);
        uint32_t btype = bitarray_bits_msb(input, 2);
    } while (bfinal != DEFLATE_FINAL_BLOCK);
    */
}
