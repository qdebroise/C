// LZ77 compression algorithm.

#ifndef LZ77_H_
#define LZ77_H_

#include <stddef.h>
#include <stdint.h>

// Performs LZ77 compression on `data`.
// The returned value is an array and *MUST* be freed when it is no longer needed (see `array.h:array_free()`).
uint8_t* lz77_compress(const uint8_t* data, size_t size);

// Performs LZ77 decompression of `compressed_data`
// The returned value is an array and *MUST* be freed when it is no longer needed (see `array.h:array_free()`).
uint8_t* lz77_uncompress(const uint8_t* compressed_data, size_t size);

#endif // L277_H_


