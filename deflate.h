#ifndef DEFLATE_H_
#define DEFLATE_H_

#include <stddef.h>
#include <stdint.h>

void deflate_compress(const uint8_t* input, size_t size);

void deflate_uncompress(const uint8_t* input, size_t size);

#endif // DEFLATE_H_


