// Boundary package-merge algorithm.
//
// An algorithm to generate optimal length-limited prefix codes.
//
// Ref:
// "A Fast and Space-Economical Algorithm for Length-Limited Coding", 1995, Moffat et. al.
// https://doi.org/10.1007/BFb0015404

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#define NULL_NODE_REF UINT16_MAX
#define FREQ_MAX UINT32_MAX
#define LIMIT_MAX 32 // Allows for 2^32 symbols which propably more than anyone would need.

typedef struct chain_t
{
    uint32_t weight;
    uint32_t count;
    uint16_t tail;
} chain_t;

typedef struct freelist_t
{
    uint32_t capacity;
    uint32_t size;
    uint16_t next_free;

} freelist_t;

uint16_t alloc_node(freelist_t* fl, chain_t* nodes)
{
    if (fl->next_free == NULL_NODE_REF) return fl->size++;
    uint16_t new = fl->next_free;
    fl->next_free = nodes[new].weight;
    return new;
}

void release_node(freelist_t* fl, chain_t* nodes, uint32_t node_index)
{
    assert(node_index != NULL_NODE_REF);

    nodes[node_index].weight = fl->next_free;
    nodes[node_index].count = -1;
    nodes[node_index].tail = NULL_NODE_REF;
    fl->next_free = node_index;
}

// Compute optimal length-limited prefix codes from an ordered set of frequencies. The frequencies
// *MUST* be sorted in increasing order.
// Codes lengths of every symbol are returned in the `code_lengths` parameter which *MUST* be large
// enough to receive the `n` codes lengths.
// `limit` is the maximum code length allowed for the symbols.
void package_merge(const uint32_t* freqs, uint32_t n, uint8_t limit, uint32_t* code_lengths)
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
    // we only store the rightmost chain. We know that a chain in list l can contain up to l
    // elements (other chains including itself) because a chain in list l always link to an element
    // in list l-1. Thus, list l can have at most l chains, list l-1 has at most l-1 and so on.
    // Therefore, the total number of nodes is '1 + 2 + .. + L = L(L + 1)/2'.
    // In addition, we have to account for the new chain being created. Indeed, the chain creation
    // happens before any other chain currently stored is released, hence we need to add an extra
    // chain giving us the total number of nodes to be 'M = L(L + 1)/2 + 1'.
    //
    // @Todo: allocate on the heap.
    chain_t nodes[limit*(limit + 1) / 2 + 1];
    freelist_t fl = (freelist_t){
        .capacity = limit*(limit + 1) / 2 + 1,
        .size = 0,
        .next_free = NULL_NODE_REF,
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
    // 64 bytes required for the stack when `limit`=32. If memory is an issue then stack size can be
    // set to `limit` has proven.
    uint16_t stack[LIMIT_MAX];
    uint8_t stack_size = 0;

    // Stores the rightmost chain of each list.
    uint16_t lists[limit];
    // Because in a list only the rightmost chain is stored, we need to keep track of the weight of
    // the chain before the last to manage lookahead chains comparisons.
    uint32_t weights[limit];

    // The algorithm starts with the two lookahead chains for each list, that is:
    // weights freqs[0] and freqs[1].
    for (uint8_t i = 0; i < limit; ++i)
    {
        weights[i] = freqs[0];

        uint16_t new = alloc_node(&fl, &nodes[0]);
        nodes[new] = (chain_t){
            .weight = freqs[1],
            .count = 2,
            .tail = NULL_NODE_REF,
        };
        lists[i] = new;
    }

    // Run BoundaryPM.
    uint8_t current = limit - 1;
    for (uint32_t i = 2; i < 2 * n - 2;)
    {
        uint16_t new = alloc_node(&fl, &nodes[0]);
        // At every iteration we add a chain in `current` list which becomes the rightmost chain.
        // We aggressively try to collect the previous rightmost chain every time.
        uint16_t to_free = lists[current];

        chain_t current_node = nodes[lists[current]];
        uint32_t freq = current_node.count >= n ? FREQ_MAX : freqs[current_node.count];
        uint32_t s = current == 0 ? 0 : weights[current - 1] + nodes[lists[current - 1]].weight;

        weights[current] = nodes[lists[current]].weight;

        if (current == 0 || s > freq)
        {
            // Step 1 & 3a (c.f. paper).
            nodes[new] = (chain_t){
                .weight = freq,
                .count = current_node.count + 1,
                .tail = current == 0 ? NULL_NODE_REF : nodes[lists[current]].tail,
            };
        }
        else
        {
            // Step 3b (c.f. paper).
            nodes[new] = (chain_t){
                .weight = s,
                .count = current_node.count,
                .tail = lists[current - 1],
            };

            stack[stack_size++] = current - 1;
            stack[stack_size++] = current - 1;
        }

        lists[current] = new;

        // A new node was added into the last list which brings us closer to our goal: "The need for
        // 2n-2 chains in list L again drives the process." (quoted from the original paper).
        if (current == limit - 1)
        {
            i++;
        }

        // Perform aggressive reclaiming of useless chains to minimize memory usage.
        if (current == limit - 1)
        {
            // We can always free safely from the last list.
            uint16_t next = nodes[to_free].tail;
            release_node(&fl, &nodes[0], to_free);
            to_free = next;
            current--;
        }
        // Try to remove other chains in the other lists. For every chain we wish to delete we first
        // need to check that it isn't referenced by any other chain from the lists rightmost
        // chains.
        // This impacts performance a bit but ensures minimal memory usage.
        // @Todo: maybe there is a clever way to avoid all this checking process...
        while (to_free != NULL_NODE_REF)
        {
            bool is_node_used = false;
            for (uint8_t i = current; i < limit; ++i)
            {
                uint16_t node_index = lists[i];
                while (node_index != to_free && node_index != NULL_NODE_REF)
                {
                    node_index = nodes[node_index].tail;
                }

                if (node_index == to_free)
                {
                    is_node_used = true;
                    break;
                }
            }

            // The node is still tied to some node at the start of a list. So at this point, we have
            // freed everything we could.
            if (is_node_used) break;

            uint16_t next = nodes[to_free].tail;
            release_node(&fl, &nodes[0], to_free);
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

    // Avoid creating the 'a' list of active nodes shown in the paper. Instead, we directly use the
    // chain's value to populate the codes lengths array.
    uint8_t code_len = 1;
    uint16_t node_index = lists[limit - 1];
    uint32_t symbol_idx = n;
    while (node_index != NULL_NODE_REF)
    {
        uint16_t next = nodes[node_index].tail;
        uint16_t num_symbols_with_len = next == NULL_NODE_REF
            ? nodes[node_index].count
            : nodes[node_index].count - nodes[next].count;
        for (uint16_t j = 0; j < num_symbols_with_len; ++j)
        {
            code_lengths[--symbol_idx] = code_len;
        }

        assert(symbol_idx <= n && "It tried to add more code lengths than there are symbols.");

        node_index = next;
        code_len++;
    }
}

#include <stdio.h>

void check_results(const uint32_t* results, const uint32_t* expected, size_t n)
{
    // printf("Code lengths (L=%d): [", limit);
    printf("[");
    for (size_t i = 0; i < n; ++i)
    {
        printf("%d, ", expected[i]);
    }
    printf("]\n");

    for (size_t i = 0; i < n; ++i)
    {
        assert(results[i] == expected[i]);
    }
}

int main(int argc, char* argv[])
{
    uint32_t frequencies[] = {1, 1, 5, 7, 10, 14};
    uint32_t n = sizeof(frequencies) / sizeof(frequencies[0]);
    uint32_t code_length[n];

    uint32_t expected_length_3[] = {3, 3, 3, 3, 2, 2};
    uint32_t expected_length_4[] = {4, 4, 3, 2, 2, 2};
    uint32_t expected_length_5[] = {5, 5, 4, 3, 2, 1};
    uint32_t expected_length_7[] = {5, 5, 4, 3, 2, 1};
    uint32_t expected_length_15[] = {5, 5, 4, 3, 2, 1};

    package_merge(&frequencies[0], n, 3, &code_length[0]); check_results(code_length, expected_length_3, n);
    package_merge(&frequencies[0], n, 4, &code_length[0]); check_results(code_length, expected_length_4, n);
    package_merge(&frequencies[0], n, 5, &code_length[0]); check_results(code_length, expected_length_5, n);
    package_merge(&frequencies[0], n, 7, &code_length[0]); check_results(code_length, expected_length_7, n);
    package_merge(&frequencies[0], n, 15, &code_length[0]); check_results(code_length, expected_length_15, n);
    package_merge(&frequencies[0], n, 32, &code_length[0]);

    uint32_t frequencies2[] = {1, 2, 4, 8, 16, 32, 124, 126, 1000, 1432, 1563, 2048};
    uint32_t n2 = sizeof(frequencies2) / sizeof(frequencies2[0]);
    uint32_t code_length2[n2];
    package_merge(&frequencies2[0], n2, 7, &code_length2[0]);
}
