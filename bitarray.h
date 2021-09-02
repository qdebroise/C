// Bit array data structure.
//
// =====================
// === Documentation ===
// =====================
//
// The following schemas describe the array format:
//
// - Bit array representation from the user perspective.
//
//                  +--------------------------------
// Array            |0 1 1 1 0 1 0 1 0 1 0 0 0 1 ...
//                  +--------------------------------
// Bit positions,                        1 1 1 1
// and indices       0 1 2 3 4 5 6 7 8 9 0 1 2 3 ...
//
// - Real in memory layout of a bit array.
//
//                        Byte 0            Byte 1
//                  +----------------+-----------------+-----
// Memory layout    |1 0 1 0 1 1 1 0 | x x 1 0 0 0 1 0 | ...
//                  +----------------+-----------------+-----
// Bit positions,                      1 1 1 1 1 1
// and indices       7 6 5 4 3 2 1 0   5 4 3 2 1 0 9 8   ...
//
// =====================
// ===      API      ===
// =====================
//
// `bitarray_size(array) -> (size_t)`
// Returns the number of bits used in the array.
//
// `bitarray_capacity(array) -> (size_t)`
// Returns the number of bits available in the current allocation.
//
// `bitarray_setbit(array, pos)` -> (void)`
// Sets the bit at the given position. `pos` *MUST* be within bounds.
//
// `bitarray_clearbit(array, pos) -> (void)`
// Clears thebit at the given position. `pos` *MUST* be within bounds.
//
// `bitarray_bit(array, pos) -> (bit)`
// Returns the value of the bit (0 or 1) at the given position. `pos` *MUST* be within bounds.
//
// `bitarray_bits_lsb(array, pos, n) -> (uint64_t)`
// Reads `n` bits starting at `pos`. Bits are appended to the output from the least significant bit to the most significant bit.
// This way a number like a uint32_t pushed using `bitarray_push_bits_lsb()` can be retrieved with this function.
// Note: `n` *MUST* cannot be larger than 64.
//
// `bitarray_bits_msb(array, pos, n) -> (uint64_t)`
// Reads `n` bits starting at `pos`. Bits are appended to the output from the most significant bit to the least significant bit.
// This way a number like a uint32_t pushed using `bitarray_push_bits_msb()` can be retrieved with this function.
// Note: `n` *MUST* cannot be larger than 64.
//
// `bitarray_push(array, bit) -> (void)`
// Appends a new bit (0 or 1) at the end of the array.
// If `bit` is anything other than 0 or 1 then behevior is undefined.
// This function causes reallocation when there isn't enough space.
//
// `bitarray_push_bits_lsb(array, bits, n) -> (void)`
// Appends `n` bits sotred in `bits` to the array.
// Bits are added starting with the lowest significant bit from `bits` up to its most significant bit.
// This function causes reallocation when there isn't enough space.
//
// `bitarray_push_bits_msb(array, bits, n) -> (void)`
// Appends `n` bits sotred in `bits` to the array.
// Bits are added starting with the most significant bit from `bits` up to its least significant bit.
// This function causes reallocation when there isn't enough space.
//
// `bitarray_clear(array) -> (void)`
// Resets the array to a size of 0. The allocation remains unchanged.
//
// `bitarray_reserve(array, size) -> (void)`
// Makes the array to be able to contain at least `size` bits without causing a reallocation.
//
// `bitarray_resize(array, size) -> (void)`
// Change the size of the array to `size`. If the array grows then new bits are uninitialized.
//
// `bitarray_free(array) -> (void)`
// Frees any memory tied to the array and set the array variable to `NULL` so that it is ready to be reused.
//
// =====================
// ===     Notes     ===
// =====================
//
// - In debug mode asserts are used to prevent out of bounds errors. Define NDEBUG to avoid using assertion (in
// release mode for instance).
//
// =====================
// ===    Example    ===
// =====================
//
// ```c
// bitarray_t ba = {0};
//
// bitarray_resize(&ba, 6);  // Resize the array to 6 bits.
//
// bitarray_setbit(&ba, 0);  // 100000
// bitarray_setbit(&ba, 4);  // 100010
// bitarray_clearbit(&ba, 0);// 0000101
//
// bitarray_push(&ba, 1);    // 1000101, size is now 7 bits.
//
// // Push 16 bits of data to the array starting with the lsb of the data.
// size_t data_index = ba.size;
// uint16_t bit_data = 23781; // Some random data.
// bitarray_push_bits_lsb(&ba, bit_data, 16);
//
// // Partially retrieved the data. 11 bits out of the 16.
// uint16_t retrieved_data = bitarray_bits_lsb(&ba, data_index, 11);
//
// bitarray_free(&ba);
// ```

#ifndef BITARRAY_H_
#define BITARRAY_H_

#include "array.h"

#include <stdint.h>

// Public API.

typedef struct bitarray_t bitarray_t;

struct bitarray_t
{
    size_t size;    // Number of bits.
    uint8_t* data;  // Array (from array.h) where the bits are stored.
};

static inline void bitarray_reserve(bitarray_t*, size_t num_bits);
static inline void bitarray_resize(bitarray_t*, size_t num_bits);
static inline void bitarray_free(bitarray_t*);

static inline void bitarray_clear(bitarray_t*);
static inline void bitarray_push(bitarray_t*, uint8_t bit);
static inline void bitarray_push_bits_lsb(bitarray_t*, uint64_t bits, size_t n);
static inline void bitarray_push_bits_msb(bitarray_t*, uint64_t bits, size_t n);

static inline void bitarray_setbit(bitarray_t*, size_t index);
static inline void bitarray_clearbit(bitarray_t*, size_t index);

static inline uint8_t bitarray_bit(const bitarray_t*, size_t index);
static inline uint64_t bitarray_bits_lsb(const bitarray_t*, size_t index, size_t n);
static inline uint64_t bitarray_bits_msb(const bitarray_t*, size_t index, size_t n);

static inline void bitarray_pad_last_byte(bitarray_t*);

// Implementation details.

// Push a bit without any verifications.
static inline void _bitarray_push_raw(bitarray_t* ba, uint8_t bit)
{
    size_t byte_index = ba->size / 8;
    size_t rel_bit_index = ba->size & 7;
    ba->data[byte_index] &= ~(1 << rel_bit_index); // We need to clear the bit first.
    ba->data[byte_index] |= ((bit & 0x1) << rel_bit_index);
    ba->size++;
}

static inline void bitarray_reserve(bitarray_t* ba, size_t num_bits)
{
    assert(ba);

    const size_t bytes_required = (num_bits + 7) / 8; // Ceiling operation.
    array_reserve(ba->data, bytes_required);
}

static inline void bitarray_resize(bitarray_t* ba, size_t num_bits)
{
    assert(ba);

    const size_t bytes_required = (num_bits + 7) / 8; // Ceiling operation.
    array_resize(ba->data, bytes_required);
    ba->size = num_bits;
}

static inline void bitarray_free(bitarray_t* ba)
{
    assert(ba);

    array_free(ba->data);
    ba->size = 0;
    ba->data = NULL;
}

static inline void bitarray_clear(bitarray_t* ba)
{
    assert(ba);

    array_clear(ba->data);
    ba->size = 0;
}

static inline void bitarray_push(bitarray_t* ba, uint8_t bit)
{
    assert(ba);

    size_t bytes_allocated = array_capacity(ba->data);

    if (ba->size == bytes_allocated * 8)
    {
        array_reserve(ba->data, 2 * bytes_allocated);
    }

    // Update the array size.
    if (ba->size / 8 >= array_size(ba->data)) array_resize(ba->data, ba->size / 8 + 1);

    _bitarray_push_raw(ba, bit);
}

static inline void bitarray_push_bits_lsb(bitarray_t* ba, uint64_t bits, size_t n)
{
    assert(ba);

    size_t bytes_allocated = array_capacity(ba->data);

    if (ba->size + n >= bytes_allocated * 8)
    {
        array_reserve(ba->data, ba->size + n);
    }

    // Update the array size.
    if ((ba->size + n) / 8 >= array_size(ba->data)) array_resize(ba->data, (ba->size + n) / 8 + 1);

    for (size_t i = 0; i < n; ++i)
    {
        _bitarray_push_raw(ba, bits >> i);
    }
}

static inline void bitarray_push_bits_msb(bitarray_t* ba, uint64_t bits, size_t n)
{
    assert(ba);

    size_t bytes_allocated = array_capacity(ba->data);

    if (ba->size + n >= bytes_allocated * 8)
    {
        array_reserve(ba->data, ba->size + n);
    }

    // Update the array size.
    if ((ba->size + n) / 8 >= array_size(ba->data)) array_resize(ba->data, (ba->size + n) / 8 + 1);

    for (size_t i = 0; i < n; ++i)
    {
        _bitarray_push_raw(ba, bits >> (n - i - 1));
    }
}

static inline void bitarray_setbit(bitarray_t* ba, size_t index)
{
    assert(ba);
    assert(index < ba->size && "bitarray:setbit index out of bounds.");
    ba->data[index / 8] |= (1 << (index & 7));
}

static inline void bitarray_clearbit(bitarray_t* ba, size_t index)
{
    assert(ba);
    assert(index < ba->size && "bitarray:clearbit index out of bounds.");
    ba->data[index / 8] &= ~(1 << (index & 7));
}

static inline uint8_t bitarray_bit(const bitarray_t* ba, size_t index)
{
    assert(ba);
    assert(index < ba->size && "bitarray:bit index out of bounds.");
    return (ba->data[index / 8] >> (index & 7)) & 0x1;
}

static inline uint64_t bitarray_bits_lsb(const bitarray_t* ba, size_t index, size_t n)
{
    assert(ba);
    assert(index + n <= ba->size && "bitarray:bits_lsb range out of bounds.");
    assert(n < 64 && "bitarray:bits_lsb can read at most 64 bits.");

    uint64_t bits = 0;
    for (size_t i = 0; i < n; ++i)
    {
        bits |= (bitarray_bit(ba, index + i) << i);
    }

    return bits;
}

static inline uint64_t bitarray_bits_msb(const bitarray_t* ba, size_t index, size_t n)
{
    assert(ba);
    assert(index + n <= ba->size && "bitarray:bits_msb range out of bounds.");
    assert(n < 64 && "bitarray:bits_msb can read at most 64 bits.");

    uint64_t bits = 0;
    for (size_t i = 0; i < n; ++i)
    {
        bits |= (bitarray_bit(ba, index + i) << (n - i - 1));
    }

    return bits;
}

static inline void bitarray_pad_last_byte(bitarray_t* ba)
{
    assert(ba);

    size_t rel_bit_index = ba->size & 7;

    if (rel_bit_index == 0) return;

    bitarray_push_bits_lsb(ba, 0, 8 - rel_bit_index);
}

#endif // BITARRAY_H_
