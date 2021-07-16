// C array data structure.
//
// == References ==
//
// Inspired from Sean Barett's stretchy buffers.
// https://github.com/nothings/stb/blob/master/stretchy_buffer.h
//
// == Documentation ==
//
// An array can easily be declared. A pointer on the type of the elements that need to be stored
// must be declared and set to NULL. Then the array is ready to be used through the functions
// provided by the API. These functions are documented below.
//
// `array_size(array) -> (size_t)`
// Returns the number of elements in the array.
//
// `array_empty(array) -> (void)`
// Returns `true` if the array is empty (i.e. its size is 0). Otherwise, `false` is returned.
//
// `array_capacity(array) -> (size_t)`
// Returns the number of elements the array can hold before being resized.
//
// `array_push(array, element) -> (void)`
// Append a new `element` at the end of the array. If the array doesn't have enough storage
// then the array is reallocated and any pointer is invalidated.
//
// `array_pop(array) -> (void)`
// Removes the last element from the array.
//
// `array_free(array) -> (void)`
// Release all the memory allocated thus far for the array. It also resets the array pointer
// to `NULL` so that it can be reused. This *MUST* be called in order to avoid memory leaks.
//
// `array_reserve(array, size) -> (void)`
// Resize the array storage so that it can hold at least `size` elements without the need
// of being resized. Pointers to the array are invalidated.
//
// `array_resize(array, size) -> (void)`
// Resize the array storage and set the array size to the specified value. Elements are uninitialized.
// Pointers to the array are invalidated.
//
// `array_clear(array) -> (void)`
// Removes all elements from the array leaving it with a size of 0. This does not skrink
// the allocated storage.
//
// `array_at(array, index) -> (element)`
// Retrieve the `element` at `index` from the array. The index *MUST* be valid (i.e. it must be
// positive and less than the size of the array). An invalid index will cause undefined behavior.
//
// `array_remove_fast(array, index) -> (void)`
// Removes the element at `index` from the array. The index *MUST* be valid (i.e. it must be
// positive and less than the size of the array). An invalid index will cause undefined behavior.
// The operation is a fast removal, it *DOES NOT* maintain order of the elements. The last element
// of the array is put at the index of the element to remove.
//
// `array_end(array) -> (element*)`
// Returns pointer the the first element past the end of the array. It can be used
// to easily iterate over the array element in a for..each manner.
//
// == Usage example ==
//
// ```c
// int* b = NULL; // Declares an array of int.
// array_reserve(b, 2); // Reserve enough space in advance for 2 elements (array_push can directly be used here).
// array_push(b, 12); // Append new elements to the array.
// array_push(b, 15);
// // Iterate over the array.
// for (int* element = b; element < array_end(b); element++)
// {
//     // Do stuff...
// }
// array_free(b); // Free allocated memory after the array is no longer needed.
// ```
//
// @Todo:
//  - add a remove operation that maintain order.

#ifndef ARRAY_H_
#define ARRAY_H_

#include <assert.h>
#include <stddef.h>
#include <stdlib.h>

// Public API.

#define array_size(B) ((B) ? _array_header(B)->_size : 0)
#define array_empty(B) (array_size(B) == 0)
#define array_capacity(B) ((B) ? _array_header(B)->_capacity : 0)
#define array_push(B, X) (_array_fit(B), (B)[_array_header(B)->_size++] = (X))
#define array_pop(B) (array_size(B) > 0 ? _array_header(B)->_size-- : 0)
#define array_free(B) ((B) ? free(_array_header(B)), (B) = NULL : 0)
#define array_reserve(B, N) ((B) = _array_reserve(B, N, sizeof(*(B))))
#define array_resize(B, N) ((B) = _array_reserve(B, N, sizeof(*(B))), _array_header(B)->_size = (N))
#define array_clear(B) ((B) ? _array_header(B)->_size = 0 : 0)
#define array_end(B) ((B) ? (B) + _array_header(B)->_size : 0)

#ifndef NDEBUG
    #define array_at(B, I)                                                  \
        assert((I) >= 0 && (I) < array_size(B) && "Array out of bounds.");  \
        (B)[(I)]
    #define array_remove_fast(B, I)                                         \
        assert((I) >= 0 && (I) < array_size(B) && "Array out of bounds.");  \
          (B)[I] = (B)[_array_header(B)->_size - 1])
#else
    #define array_at(B, I) ((B)[(I)])
    #define array_remove_fast(B, I) ((B) ? (B)[(I)] = (B)[_array_header(B)->_size - 1] : 0)
#endif

// -----------------------------------------------------------------------------
// Implementation details.
// -----------------------------------------------------------------------------

struct _array_header_t
{
    size_t _size;
    size_t _capacity;
    char _buf[];
};

#define _array_header(B) ((struct _array_header_t*)((char*)(B) - offsetof(struct _array_header_t, _buf)))
#define _array_is_full(B) (array_size(B) == array_capacity(B))
// The array is set to double its storage whenever a reallocation must be performed.
// @Todo: this is the growth sequence used by some implementations of std::vector https://oeis.org/A061418
// https://stackoverflow.com/questions/5232198/about-vectors-growth
#define _array_fit(B) (_array_is_full(B) ? ((B) = _array_reserve(B, 2 * array_capacity(B), sizeof(*(B)))) : 0)

static inline void* _array_reserve(void* b, size_t capacity, size_t size)
{
    struct _array_header_t* header = b ? _array_header(b) : NULL;

    if (header && capacity <= header->_capacity)
    {
        // There is already enough place for the size asked.
        return header->_buf;
    }

    // Ensure at least 1 element is allocated. We don't want any problems when 0 elements are asked.
    const size_t new_capacity = capacity == 0 ? 1 : capacity;
    const size_t alloc_size = new_capacity * size + sizeof(struct _array_header_t);
    header = realloc(header, alloc_size);

    if (header)
    {
        if (!b)
        {
            header->_size = 0;
        }

        header->_capacity = new_capacity;
        return header->_buf;
    }
    else
    {
        assert(!"array.h:_reserve out of memory.");
        return NULL;
    }
}

#endif // ARRAY_H_
