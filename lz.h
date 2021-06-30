// LZ* family compression algorithms:
// - LZ77
// - LZSS

#ifndef LZ_H_
#define LZ_H_

#include <stddef.h>
#include <stdint.h>

// Performs LZ77 compression on `data`.
// The returned value is an array and *MUST* be freed when it is no longer needed (see `array.h:array_free()`).
uint8_t* lz77_compress(const uint8_t* data, size_t size);

// Performs LZ77 decompression of `compressed_data`
// The returned value is an array and *MUST* be freed when it is no longer needed (see `array.h:array_free()`).
uint8_t* lz77_uncompress(const uint8_t* compressed_data, size_t size);

// Performs LZSS compression on `data`.
// The returned value is an array and *MUST* be freed when it is no longer needed (see `array.h:array_free()`).
uint8_t* lzss_compress(const uint8_t* data, size_t size);

// Performs LZSS decompression of `compressed_data`
// The returned value is an array and *MUST* be freed when it is no longer needed (see `array.h:array_free()`).
uint8_t* lzss_uncompress(const uint8_t* compressed_data, size_t size);

#endif // LZ_H_


