#ifndef PACKAGE_MERGE_H_
#define PACKAGE_MERGE_H_

#include <stdint.h>

// Compute optimal length-limited prefix code lengths from an ordered set of frequencies using
// boudary package-merge algorithm. The frequencies *MUST* be sorted in ascending order.
// Codes lengths of every symbol are returned in the `code_lengths` parameter which *MUST* be large
// enough to receive the `n` codes lengths.
// `limit` is the maximum code length allowed for the symbols.
void package_merge(const uint32_t* freqs, uint32_t n, uint8_t limit, uint32_t* code_lengths);

#endif // PACKAGE_MERGE_H_


