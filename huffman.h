#ifndef HUFFMAN_H_
#define HUFFMAN_H_

#include <stdint.h>
#include <stddef.h>

// Create a Huffman tree from the given bytes i.e. with an alphabet of 256 values.
uint8_t* huffman_tree(const uint8_t* bytes, size_t size);

#endif


