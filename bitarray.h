// Bit array data structure.
// The implementation uses the same ideas as stretchy buffers (see 'array.h').
//
// =====================
// === Documentation ===
// =====================
//
// Bit arrays use the same principles as the arrays in 'array.h'.
// Thus, to use an array simply declare a `bitarray_t` pointer, set it to `NULL` and you're ready to go.
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
// ===   Warnings    ===
// =====================
//
// - The API is macro based, so be aware of side effects.
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
// bitarray_t* ba = NULL;
//
// bitarray_resize(ba, 6);  // Resize the array to 6 bits.
//
// bitarray_setbit(ba, 0);  // 100000
// bitarray_setbit(ba, 4);  // 100010
// bitarray_clearbit(ba, 0);// 0000101
//
// bitarray_push(ba, 1);    // 1000101, size is now 7 bits.
//
// // Push 16 bits of data to the array starting with the lsb of the data.
// size_t data_index = bitarray_size(ba);
// uint16_t bit_data = 23781; // Some random data.
// bitarray_push_bits_lsb(ba, bit_data, 16);
//
// // Partially retrieved the data. 11 bits out of the 16.
// uint16_t retrieved_data = bitarray_bits_lsb(ba, data_index, 11);
//
// bitarray_free(ba);
// ```

#ifndef BITARRAY_H_
#define BITARRAY_H_

#include <assert.h>
#include <stddef.h> // offsetof
#include <stdint.h>
#include <stdlib.h> // realloc, free

// Public API.

typedef uint8_t bitarray_t;

#define bitarray_size(B) ((B) ? _bitarray_header(B)->_size : 0)
#define bitarray_capacity(B) ((B) ? _bitarray_header(B)->_bytes_allocated * 8 : 0)

#define bitarray_reserve(B, N) ((B) = _bitarray_reserve((B), (N))->_buf)
#define bitarray_resize(B, N) (bitarray_reserve(B, N), _bitarray_header(B)->_size = (N))
#define bitarray_free(B) ((B) ? free(_bitarray_header(B)) : 0, (B) = NULL)

#define bitarray_clear(B) ((B) ? _bitarray_header(B)->_size = 0 : 0)
#define bitarray_push(B, X) ((B) = _bitarray_push(B, X)->_buf)
#define bitarray_push_bits_lsb(B, X, N) ((B) = _bitarray_push_bits_lsb(B, X, N)->_buf)
#define bitarray_push_bits_msb(B, X, N) ((B) = _bitarray_push_bits_msb(B, X, N)->_buf)

#define bitarray_setbit(B, I) _bitarray_setbit(B, I)
#define bitarray_clearbit(B, I) _bitarray_clearbit(B, I)

#define bitarray_bit(B, I) _bitarray_bit(B, I)
#define bitarray_bits_lsb(B, I, N) _bitarray_bits_lsb(B, I, N)
#define bitarray_bits_msb(B, I, N) _bitarray_bits_msb(B, I, N)

// -----------------------------------------------------------------------------
// Implementation details.
// -----------------------------------------------------------------------------

struct _bitarray_header_t
{
    size_t _size;               // Number of bits.
    size_t _bytes_allocated;    // Number of bytes in the allocation.
    uint8_t _buf[];             // Actual array of uint8_t where bits are stored.
};

#define _bitarray_header(B) ((struct _bitarray_header_t*)((char*)(B) - offsetof(struct _bitarray_header_t, _buf)))

static inline struct _bitarray_header_t* _bitarray_reserve(void* b, size_t num_bits)
{
    struct _bitarray_header_t* header = b ? _bitarray_header(b) : NULL;

    const size_t bytes_required = (num_bits + 7) / 8; // Ceiling operation.

    if (header && bytes_required < header->_bytes_allocated)
    {
        // There is already enough place for the number of bits asked.
        return header;
    }

    // Ensure at least 1 byte is allocated. We don't want any problems when 0 bits are asked.
    const size_t real_bytes_required = bytes_required == 0 ? 1 : bytes_required;
    const size_t alloc_size = real_bytes_required + sizeof(struct _bitarray_header_t);
    header = realloc(header, alloc_size);

    if (header)
    {
        if (!b)
        {
            // This is the first allocation.
            header->_size = 0;
        }

        header->_bytes_allocated = real_bytes_required;
        return header;
    }
    else
    {
        // Out of memory.
        return NULL;
    }
}

// Push a bit without any verifications.
static inline void _bitarray_push_raw(struct _bitarray_header_t* header, uint8_t bit)
{
    size_t byte_index = header->_size / 8;
    size_t rel_bit_index = header->_size & 7;
    header->_buf[byte_index] &= ~(1 << rel_bit_index);
    header->_buf[byte_index] |= ((bit & 0x1) << rel_bit_index);
    header->_size++;
}

static inline struct _bitarray_header_t* _bitarray_push(void* b, uint8_t bit)
{
    struct _bitarray_header_t* header = b ? _bitarray_header(b) : NULL;
    size_t size = bitarray_size(b);

    if (!header || size == header->_bytes_allocated * 8)
    {
        header = _bitarray_reserve(b, 2 * size);
    }

    _bitarray_push_raw(header, bit);

    return header;
}

static inline struct _bitarray_header_t* _bitarray_push_bits_lsb(void* b, uint64_t bits, size_t n)
{
    struct _bitarray_header_t* header = b ? _bitarray_header(b) : NULL;
    size_t size = bitarray_size(b);

    if (!header || size + n >= header->_bytes_allocated * 8)
    {
        header = _bitarray_reserve(b, size + n);
    }

    for (size_t i = 0; i < n; ++i)
    {
        _bitarray_push_raw(header, bits >> i);
    }

    return header;
}

static inline struct _bitarray_header_t* _bitarray_push_bits_msb(void* b, uint64_t bits, size_t n)
{
    struct _bitarray_header_t* header = b ? _bitarray_header(b) : NULL;
    size_t size = bitarray_size(b);

    if (!header || size + n >= header->_bytes_allocated * 8)
    {
        header = _bitarray_reserve(b, size + n);
    }

    for (size_t i = 0; i < n; ++i)
    {
        _bitarray_push_raw(header, bits >> (n - i - 1));
    }

    return header;
}

static inline void _bitarray_setbit(void* b, size_t index)
{
    assert(index < bitarray_size(b) && "bitarray:setbit index out of bounds.");
    _bitarray_header(b)->_buf[index / 8] |= (1 << (index & 7));
}

static inline void _bitarray_clearbit(void* b, size_t index)
{
    assert(index < bitarray_size(b) && "bitarray:clearbit index out of bounds.");
    _bitarray_header(b)->_buf[index / 8] &= ~(1 << (index & 7));
}

static inline uint8_t _bitarray_bit(void* b, size_t index)
{
    assert(index < bitarray_size(b) && "bitarray:bit index out of bounds.");
    return (_bitarray_header(b)->_buf[index / 8] >> (index & 7)) & 0x1;
}

static inline uint64_t _bitarray_bits_lsb(void* b, size_t index, size_t n)
{
    assert(index + n <= bitarray_size(b) && "bitarray:bits_lsb range out of bounds.");
    assert(n < 64 && "bitarray:bits_lsb can read at most 64 bits.");

    uint64_t bits = 0;
    for (size_t i = 0; i < n; ++i)
    {
        bits |= (bitarray_bit(b, index + i) << i);
    }

    return bits;
}

static inline uint64_t _bitarray_bits_msb(void* b, size_t index, size_t n)
{
    assert(index + n <= bitarray_size(b) && "bitarray:bits_msb range out of bounds.");
    assert(n < 64 && "bitarray:bits_msb can read at most 64 bits.");

    uint64_t bits = 0;
    for (size_t i = 0; i < n; ++i)
    {
        bits |= (bitarray_bit(b, index + i) << (n - i - 1));
    }

    return bits;
}

#endif // BITARRAY_H_
