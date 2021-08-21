// Boundary package-merge algorithm.
//
// An algorithm to generate optimal length-limited prefix codes.
//
// Ref:
// "A Fast and Space-Economical Algorithm for Length-Limited Coding", 1995, Moffat et. al.
// https://doi.org/10.1007/BFb0015404
#include "package_merge.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>

#define NULL_CHAIN_REF UINT16_MAX
#define FREQ_MAX UINT32_MAX
#define LIMIT_MAX 32 // Allows for 2^32 symbols which is propably more than anyone would ever need.

typedef struct chain_t
{
    uint32_t count;
    uint16_t tail;
} chain_t;

typedef struct freelist_t
{
    uint32_t capacity;
    uint32_t size;
    uint16_t next_free;

} freelist_t;

uint16_t alloc_chain(freelist_t* fl, const chain_t* chains)
{
    if (fl->next_free == NULL_CHAIN_REF)
    {
        assert(fl->size < fl->capacity && "No more nodes available.");
        return fl->size++;
    }
    uint16_t new = fl->next_free;
    fl->next_free = (uint16_t)chains[new].count;
    return new;
}

void release_chain(freelist_t* fl, chain_t* chains, uint32_t chain_index)
{
    assert(chain_index != NULL_CHAIN_REF);

    chains[chain_index].count = fl->next_free;
    chains[chain_index].tail = NULL_CHAIN_REF;
    fl->next_free = chain_index;
}

// Compute optimal length-limited prefix code lengths from an ordered set of frequencies using
// boudary package-merge algorithm. The frequencies *MUST* be sorted in ascending order and be free
// of symbols with a frequency of 0.
// Active leaves for each list is returned in the `active_leaves` parameter which *MUST* be large
// enough to receive `limit` number of values.
// `limit` is the maximum code length allowed for the symbols.
void package_merge(const uint32_t* freqs, uint32_t n, uint8_t limit, uint32_t* active_leaves)
{
    assert(n > 0 && "The list of frequencies is empty.");
    assert(limit <= LIMIT_MAX && "The code length limit is too big.");
    // @Note: `limit` represents the depth limit of the Huffman tree. A binary tree of depth `limit`
    // can have at most '2^`limit`' leaves. If `n`, the number of symbols, is more than the number
    // of leaves then constructing the tree is impossible as we don't have enough leaf nodes for all
    // the symbols.
    assert((1ull << limit) > n && "The code length limit is too small.");

    // @Todo: handle case where n == 1. We could also handle simple case like n == 2.

    assert(n > 1);

    // The maximal number of chains that can be used is 'M = L(L + 1)/2 + 1'.
    //
    // Proof:
    // For each of the L lists (where L is `limit`, l below denotes the l-th list with 0 < l <= L)
    // we only store the rightmost chain. Because a chain in list l always link to an element in
    // list l-1 we know that a chain in list l can link up to l elements (other chains including
    // itself). So, list l can have at most l chains, list l-1 has at most l-1 and so on. Therefore,
    // the total number of chains is '1 + 2 + .. + L = L(L + 1)/2'.
    // In addition, we have to account for the new chain being created. Indeed, the chain creation
    // happens before any other chain currently stored is released, hence we need to add an extra
    // chain giving us the total number of chains to be 'M = L(L + 1)/2 + 1'.
    uint32_t min_num_chains = limit * (limit + 1) / 2 + 1;
    chain_t* chains = malloc(min_num_chains * sizeof(chain_t));
    freelist_t fl = (freelist_t){
        .capacity = min_num_chains,
        .size = 0,
        .next_free = NULL_CHAIN_REF,
    };

    // A stack is used to simulate recursion. We know for certain that the stack can never grow
    // beyond L elements.
    //
    // Proof:
    // To simulate the two recursive calls we need to add 2 elements on the stack. Also, we know
    // that operations in the first list will never cause recursive calls. Thus we already have an
    // upper bound to the stack size that is '2*(L - 1)'.
    // This can be shrinked further with the following observation. Amongst the two elements added
    // for the recursive calls, one will be popped immediately beggining a new loop cycle. So if the
    // next list causes a recursion it adds 2 elements in turn and pops one right aways.
    // Let S(l) be the size of the stack when operating in list l. When 'l = L' then we know the
    // stack is empty so 'S(L) = 0'. Then we have 'S(l-1) = S(l) + 1', not '+2' since S(l) is the
    // stack size when operating in list l and in order to operate from list l + 1 to l we have to
    // pop an element of the stack.
    // Therefore we have:
    // - S(L) = 0
    // - S(L - l) = S(L - l) + 1, where 1 < l <= L <=> S(L - l) = l.
    //
    // @Note: Since `limit` is capped to a maximum of 32 we don't bother and can handle the full
    // 64 bytes required for the stack no matter the limit. If memory is an issue then the stack
    // size can be set to `limit` as proven.
    uint16_t stack[LIMIT_MAX];
    uint8_t stack_size = 0;

    // Stores the rightmost chain of each list.
    uint16_t lists[limit];
    // Weights don't need to be stored in the chains. We only need to be able to lookup the sum of
    // the weights of the two lookahead chains of a list. Thus, we can store these summed weights
    // in a separate list. When adding a new node we simply add its weight to the existing sum and
    // when provoking a recursion we reset the weight of list l-1 to 0.
    uint32_t weights[limit];

    // The algorithm starts with the two lookahead chains in each list, that is: weights `freqs[0]`
    // and `freqs[1]`.
    assert(freqs[0] > 0 && freqs[1] > 0 && "Frequencies of 0 are not allowed.");
    for (uint8_t i = 0; i < limit; ++i)
    {
        weights[i] = freqs[0] + freqs[1];

        uint16_t new = alloc_chain(&fl, &chains[0]);
        chains[new] = (chain_t){
            .count = 2,
            .tail = NULL_CHAIN_REF,
        };
        lists[i] = new;
    }

    // Run BoundaryPM.
    uint8_t current = limit - 1;
    for (uint32_t i = 2; i < 2 * n - 2;)
    {
        uint16_t new = alloc_chain(&fl, &chains[0]);
        // At every iteration we add a chain in `current` list which becomes the rightmost chain.
        // We aggressively try to collect the previous rightmost chain every time.
        uint16_t to_free = lists[current];

        chain_t current_chain = chains[lists[current]];
        uint32_t freq = current_chain.count >= n ? FREQ_MAX : freqs[current_chain.count];
        uint32_t s = current == 0 ? 0 : weights[current - 1];

        assert(freq > 0 && "Frequencies of 0 are not allowed.");

        if (current == 0 || s > freq)
        {
            // Step 1 & 3a (c.f. paper).
            chains[new] = (chain_t){
                .count = current_chain.count + 1,
                .tail = current == 0 ? NULL_CHAIN_REF : chains[lists[current]].tail,
            };
            weights[current] += freq;
        }
        else
        {
            // Step 3b (c.f. paper).
            chains[new] = (chain_t){
                .count = current_chain.count,
                .tail = lists[current - 1],
            };
            weights[current - 1] = 0;
            weights[current] += s;

            stack[stack_size++] = current - 1;
            stack[stack_size++] = current - 1;
        }

        lists[current] = new;

        // A new chain was added into the last list which brings us closer to our goal: "The need for
        // 2n-2 chains in list L again drives the process." (quoted from the original paper).
        if (current == limit - 1)
        {
            i++;
        }

        // Perform aggressive reclaiming of useless chains to minimize memory usage.
        if (current == limit - 1)
        {
            // We can always free safely from the last list.
            uint16_t next = chains[to_free].tail;
            release_chain(&fl, &chains[0], to_free);
            to_free = next;
            current--;
        }
        // Try to remove other chains in the other lists. For every chain we wish to delete we first
        // need to check that it isn't referenced by any other chain from the lists rightmost
        // chains.
        // This impacts performance a bit but ensures minimal memory usage.
        // @Todo: maybe there is a clever way to avoid all this checking process...
        while (to_free != NULL_CHAIN_REF)
        {
            bool is_chain_used = false;
            for (uint8_t l = current + 1; l < limit; ++l)
            {
                uint16_t chain_index = lists[l];
                while (chain_index != to_free && chain_index != NULL_CHAIN_REF)
                {
                    chain_index = chains[chain_index].tail;
                }

                if (chain_index == to_free)
                {
                    is_chain_used = true;
                    break;
                }
            }

            // The chain is still tied to some other chain at the start of a list. So at this point,
            // we have freed everything we could.
            if (is_chain_used) break;

            uint16_t next = chains[to_free].tail;
            release_chain(&fl, &chains[0], to_free);
            to_free = next;
            current--;
        }

        if (stack_size != 0)
        {
            current = stack[--stack_size];
        }
        else
        {
            current = limit - 1;
        }
    }

    // Fill the array counting the number of active leaves in each list. The following example shows
    // how to retrieve the code lengths from such a list.
    // Example:
    // Given the array of active leaves [4, 6, 6] (with n=6, limit=3), one can retrieve the codes
    // lengths as:
    // - 4 symbols (4 - 0) of length 3
    // - 2 symbols (6 - 4) of length 2
    // - 0 symbols (6 - 6) of length 1
    // Giving the code length array [3, 3, 3, 3, 2, 2].
    uint16_t chain_index = lists[limit - 1];
    uint8_t l = limit - 1;
    while (chain_index != NULL_CHAIN_REF)
    {
        assert(l < limit && "It tried to add more active leaves than there are lists.");
        active_leaves[l--] = chains[chain_index].count;
        chain_index = chains[chain_index].tail;
    }

    free(chains);
}
