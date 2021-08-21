#include "huffman.h"

#include "bitarray.h"

#include <stdbool.h>

// This implementation of Huffman compression is designed to compress bytes. Thus a symbol of the
// alphabet ranges in [0x00, 0xff].
#define ALPHABET_SIZE 256

typedef struct frequencies_t
{
    uint32_t count[ALPHABET_SIZE]; // Frequency of every symbol in the alphabet.
    uint32_t sorted[ALPHABET_SIZE]; // Sorted version of the count array.
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
        freq->sorted[i] = i;
    }
}

// @Todo @Performance; we use the indirection quite a lot like lengths[sorted[i]], freqs[sorted[i]],
// etc. This cause quite a lot of jumps all over the place in memory. Alphabets are rather small,
// for my use case anyway so copying the frequencies in a sorted array should be rather cheap and
// faster to use afterwards. Then we only need use the indirection when building codelengths for
// symbols. Test and profile this.
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
            uint32_t f1 = freq->count[freq->sorted[j]];
            uint32_t f2 = freq->count[freq->sorted[j + 1]];
            // Sort by frequency and alphabetically. Important for canonical prefix codes.
            if (f1 > f2 || (f1 == f2 && freq->sorted[j] > freq->sorted[j + 1]))
            {
                size_t swap = freq->sorted[j + 1];
                freq->sorted[j + 1] = freq->sorted[j];
                freq->sorted[j] = swap;
            }
        }
    }

    for (size_t i = 0; i < ALPHABET_SIZE; ++i)
    {
        if (freq->count[freq->sorted[i]] != 0)
        {
            freq->num_used_symbols = ALPHABET_SIZE - i;
            break;
        }
    }
}

#define NULL_CHAIN_REF UINT16_MAX
#define FREQ_MAX UINT32_MAX
#define LIMIT_MAX 32 // Allows for 2^32 symbols which is propably more than anyone would ever need.

typedef struct chain_t
{
    uint32_t count;
    uint16_t tail;
} chain_t;

typedef struct freelist_t
{
    uint32_t capacity;
    uint32_t size;
    uint16_t next_free;

} freelist_t;

uint16_t alloc_chain(freelist_t* fl, const chain_t* chains)
{
    if (fl->next_free == NULL_CHAIN_REF)
    {
        assert(fl->size < fl->capacity && "No more nodes available.");
        return fl->size++;
    }
    uint16_t new = fl->next_free;
    fl->next_free = (uint16_t)chains[new].count;
    return new;
}

void release_chain(freelist_t* fl, chain_t* chains, uint32_t chain_index)
{
    assert(chain_index != NULL_CHAIN_REF);

    chains[chain_index].count = fl->next_free;
    chains[chain_index].tail = NULL_CHAIN_REF;
    fl->next_free = chain_index;
}

// Compute optimal length-limited prefix code lengths from an ordered set of frequencies using
// boudary package-merge algorithm. The frequencies *MUST* be sorted in ascending order.
// Codes lengths of every symbol are returned in the `code_lengths` parameter which *MUST* be large
// enough to receive the `n` codes lengths.
// `limit` is the maximum code length allowed for the symbols.
void package_merge(const uint32_t* freqs, const uint32_t* sorted, uint32_t n, uint8_t limit, uint32_t* active_leaves)
{
    assert(n > 0 && "The list of frequencies is empty.");
    assert(limit <= LIMIT_MAX && "The code length limit is too big.");
    assert((1ull << limit) > n && "The code length limit is too small.");

    // @Todo: handle case where n == 1. We could also handle simple case like n == 2.

    assert(n > 1);

    uint32_t min_num_chains = limit * (limit + 1) / 2 + 1;
    chain_t* chains = malloc(min_num_chains * sizeof(chain_t));
    freelist_t fl = (freelist_t){
        .capacity = min_num_chains,
        .size = 0,
        .next_free = NULL_CHAIN_REF,
    };

    uint16_t stack[LIMIT_MAX]; // @Todo: Can be set exactly when in use in Deflate.
    uint8_t stack_size = 0;

    uint16_t lists[limit];
    uint32_t weights[limit];

    // Init state.
    assert(freqs[sorted[0]] > 0 && freqs[sorted[1]] > 0 && "Frequencies of 0 are not allowed.");
    for (uint8_t i = 0; i < limit; ++i)
    {
        weights[i] = freqs[sorted[0]] + freqs[sorted[1]];

        uint16_t new = alloc_chain(&fl, &chains[0]);
        chains[new] = (chain_t){
            .count = 2,
            .tail = NULL_CHAIN_REF,
        };
        lists[i] = new;
    }

    // Run BoundaryPM.
    uint8_t current = limit - 1;
    for (uint32_t i = 2; i < 2 * n - 2;)
    {
        uint16_t new = alloc_chain(&fl, &chains[0]);
        uint16_t to_free = lists[current];

        chain_t current_chain = chains[lists[current]];
        uint32_t freq = current_chain.count >= n ? FREQ_MAX : freqs[sorted[current_chain.count]];
        uint32_t s = current == 0 ? 0 : weights[current - 1];

        assert(freq > 0 && "Frequencies of 0 are not allowed.");

        if (current == 0 || s > freq)
        {
            chains[new] = (chain_t){
                .count = current_chain.count + 1,
                .tail = current == 0 ? NULL_CHAIN_REF : chains[lists[current]].tail,
            };
            weights[current] += freq;
        }
        else
        {
            chains[new] = (chain_t){
                .count = current_chain.count,
                .tail = lists[current - 1],
            };
            weights[current - 1] = 0;
            weights[current] += s;

            stack[stack_size++] = current - 1;
            stack[stack_size++] = current - 1;
        }

        lists[current] = new;

        if (current == limit - 1)
        {
            i++;
        }

        if (current == limit - 1)
        {
            uint16_t next = chains[to_free].tail;
            release_chain(&fl, &chains[0], to_free);
            to_free = next;
            current--;
        }
        while (to_free != NULL_CHAIN_REF)
        {
            bool is_chain_used = false;
            for (uint8_t l = current + 1; l < limit; ++l)
            {
                uint16_t chain_index = lists[l];
                while (chain_index != to_free && chain_index != NULL_CHAIN_REF)
                {
                    chain_index = chains[chain_index].tail;
                }

                if (chain_index == to_free)
                {
                    is_chain_used = true;
                    break;
                }
            }

            // The chain is still tied to some other chain at the start of a list. So at this point,
            // we have freed everything we could.
            if (is_chain_used) break;

            uint16_t next = chains[to_free].tail;
            release_chain(&fl, &chains[0], to_free);
            to_free = next;
            current--;
        }

        if (stack_size != 0)
        {
            current = stack[--stack_size];
        }
        else
        {
            current = limit - 1;
        }
    }

    uint16_t chain_index = lists[limit - 1];
    uint8_t l = limit - 1;
    while (chain_index != NULL_CHAIN_REF)
    {
        assert(l < limit && "It tried to add more active leaves than there are lists.");
        active_leaves[l--] = chains[chain_index].count;
        chain_index = chains[chain_index].tail;
    }

    free(chains);
}

// Constructs a canonical Huffman tree.
//
// A canonical Huffman tree is one Huffman tree amongst the several tree possibilities of a given
// alphabet. This tree fits additional rules making it very easy to describe in a compact format.
// https://en.wikipedia.org/wiki/Canonical_Huffman_code
void build_canonical_prefix_code(const uint32_t* lengths, const uint32_t* sorted, uint32_t num_used_symbols, codeword_t* codewords)
{
    uint32_t code = 0;
    for (int i = ALPHABET_SIZE - 1; i >= 0; --i)
    {
        uint32_t length = lengths[sorted[i]];
        uint32_t next_length = i == 0 ? length : lengths[i - 1];

        codewords[sorted[i]].num_bits = length;
        codewords[sorted[i]].bits = code;
        code = (code + 1) << (next_length - length);
    }
}

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
    uint32_t active_leaves[32] = {0};
    package_merge(&freq.count[0], &freq.sorted[ALPHABET_SIZE - freq.num_used_symbols], freq.num_used_symbols, 32, &active_leaves[0]);
    uint32_t lengths[ALPHABET_SIZE] = {0};
    package_merge_generate_lengths(&active_leaves[0], &freq.sorted[0], 32, freq.num_used_symbols, ALPHABET_SIZE, &lengths[0]);

    codeword_t codewords[ALPHABET_SIZE];
    build_canonical_prefix_code(&lengths[0], &freq.sorted[0], freq.num_used_symbols, &codewords[0]);

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

