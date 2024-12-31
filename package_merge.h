#ifndef PACKAGE_MERGE_H_
#define PACKAGE_MERGE_H_

#include <stdint.h>

// Computes optimal length-limited prefix code lengths from an ordered set of frequencies using
// package-merge algorithm. The frequencies **MUST** not contain 0s and **MUST** be sorted in
// ascending order.
// Code length for each frequency is returned in the `code_lengths` parameter which *MUST* be
// allocated by the user and large enough to receive `n` number of values.
// `limit` is the maximum code length allowed for the symbols.
void package_merge(const uint32_t *freqs, uint32_t n, uint8_t limit, uint32_t *code_lengths);

// @Todo(qdebroise): Implement a second version without the requirements on the `freqs` array. The
// function will internally sort frequencies and then call the other package-merge function. The
// code lengths returned must be in the same order as the input frequencies though, not in the
// sorted order.
void package_merge_any(const uint32_t *freqs, uint32_t n, uint8_t limit, uint32_t *code_lengths);

#endif // PACKAGE_MERGE_H_
