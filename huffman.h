#ifndef HUFFMAN_H_
#define HUFFMAN_H_

#include <stdint.h>
#include <stddef.h>

// Compress the inputs bytes using a Huffman code.
uint8_t* huffman_compress(const uint8_t* bytes, size_t size);

// Uncompress data previously encoded with a Huffman code.
uint8_t* huffman_uncompress(const uint8_t* compressed_data, size_t size);

#endif


