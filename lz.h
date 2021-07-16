// LZ* family compression algorithms:
// - LZ77
// - LZSS

#ifndef LZ_H_
#define LZ_H_

#include <stddef.h>
#include <stdint.h>

// Performs LZ77 compression on `data`.
// The returned value is an array and *MUST* be freed when it is no longer needed (see `array.h:array_free()`).
//
// The compressed data layout is a sequence of triples on 3 bytes. The first 2 bytes encode a <distance, length> reference.
// The 12 msb are the distance and the 4 remaining lsb encode the length. The third byte is the next byte in the raw input stream.
//
uint8_t* lz77_compress(const uint8_t* data, size_t size);

// Performs LZ77 decompression of `compressed_data`
// The returned value is an array and *MUST* be freed when it is no longer needed (see `array.h:array_free()`).
uint8_t* lz77_uncompress(const uint8_t* compressed_data, size_t size);

// Performs LZSS compression on `data`.
// The returned value is an array and *MUST* be freed when it is no longer needed (see `array.h:array_free()`).
//
// The compressed data layout is a sequence of flags and elements where:
// - An element can be of two types: it can either be a raw byte from the original data or a <distance, length> reference on two bytes.
// A reference is encoded on 2 bytes where the 12 msb are the distance and the 4 remaining lsb encode the length.
// - One bit flags indicating what type an element is are grouped in a byte preceding a sequence of 8 elements.
//
// | 1 byte| N bytes     | 1 byte| N bytes     | ... |  Where N is no more than 16 bytes.
// +-------+-------------+-------+-------------+-----+
// | flags | elements x8 | flags | elements x8 | ... |
// +-------+-------------+-------+-------------+-----+
//
uint8_t* lzss_compress(const uint8_t* data, size_t size);

// Performs LZSS decompression of `compressed_data`
// The returned value is an array and *MUST* be freed when it is no longer needed (see `array.h:array_free()`).
uint8_t* lzss_uncompress(const uint8_t* compressed_data, size_t size);

#endif // LZ_H_


