#ifndef PACKAGE_MERGE_H_
#define PACKAGE_MERGE_H_

#include <stdint.h>

// Compute optimal length-limited prefix code lengths from an ordered set of frequencies using
// boudary package-merge algorithm. The frequencies *MUST* be sorted in ascending order and be free
// of symbols with a frequency of 0.
// Active leaves for each list is returned in the `active_leaves` parameter which *MUST* be large
// enough to receive `limit` number of values.
// `limit` is the maximum code length allowed for the symbols.
void package_merge(const uint32_t* freqs, uint32_t n, uint8_t limit, uint32_t* active_leaves);

// @Todo: add a function in this API to translate from active leaves to code lengths ?

#endif // PACKAGE_MERGE_H_


