#include "huffman.h"

#include "bitarray.h"

typedef struct node_t
{
    uint8_t byte;
    size_t freq;
    uint32_t left;
    uint32_t right;
    uint32_t parent;
} node_t;

typedef struct tree_builder_t
{
    node_t nodes[511];
    uint32_t next_free_node;
    uint32_t queue_1[256]; // Indirection into `nodes`.
    // @Note @Todo: I think this can be at most 128 long. The only way this queue can grow is if we take two nodes from queue1 resulting in a single new node in queue2. Thus, because we have 256 nodes in queue1 we can only have 256/2=128 nodes in this queue.
    uint32_t queue_2[256]; // Indirection into `nodes`.
    size_t queue_1_size;
    size_t queue_2_size;
    size_t queue_1_head;
    size_t queue_2_head;
} tree_builder_t;

typedef struct codeword_t
{
    uint8_t num_bits;
    uint32_t bits;
} codeword_t;

void init_tree_builder(tree_builder_t* tb)
{
    for (int i = 0; i < 256; ++i)
    {
        tb->queue_1[i] = UINT32_MAX;
        tb->queue_2[i] = UINT32_MAX;
    }

    tb->next_free_node = 0;
    tb->queue_1_size = 0;
    tb->queue_2_size = 0;
    tb->queue_1_head = 0;
    tb->queue_2_head = 0;
}

uint32_t select_node(tree_builder_t* tb)
{
    assert(tb->queue_1_size + tb->queue_2_size >= 1 && "Both queues are empty.");

    uint32_t selected_node;

    if (tb->queue_1_size != 0 && tb->queue_2_size == 0)
    {
        // Only queue1 has nodes.
        selected_node = tb->queue_1[tb->queue_1_head];
        tb->queue_1_head = (tb->queue_1_head + 1) % 256;
        tb->queue_1_size--;
    }
    else if (tb->queue_1_size == 0 && tb->queue_2_size != 0)
    {
        // Only queue2 has nodes.
        selected_node = tb->queue_2[tb->queue_2_head];
        tb->queue_2_head = (tb->queue_2_head + 1) % 256;
        tb->queue_2_size--;
    }
    // Both queues have nodes. Select the smallest one.
    else if (tb->nodes[tb->queue_1[tb->queue_1_head]].freq <= tb->nodes[tb->queue_2[tb->queue_2_head]].freq)
    {
        // Select from queue1.
        selected_node = tb->queue_1[tb->queue_1_head];
        tb->queue_1_head = (tb->queue_1_head + 1) % 256;
        tb->queue_1_size--;
    }
    else
    {
        // Select from queue2.
        selected_node = tb->queue_2[tb->queue_2_head];
        tb->queue_2_head = (tb->queue_2_head + 1) % 256;
        tb->queue_2_size--;
    }

    return selected_node;
}

#ifndef NDEBUG
#include <stdio.h>
// Write graph in a file using DOT graph language.
// Nodes names are "X-Y" where X is the node index in tree_builder->nodes.
// Y is the byte in hexadecimal.
void write_graph(const tree_builder_t* tb, uint32_t root)
{
    uint32_t stack[512];
    stack[0] = root;
    uint32_t stack_size = 1;

    FILE* f = fopen("huffman_tree.txt", "w");
    fprintf(f, "digraph G {\n");
    while (stack_size--)
    {
        uint32_t current = stack[stack_size];

        if (tb->nodes[current].left != UINT32_MAX)
        {
            fprintf(f, "\t\"%d-%x\" -> \"%d-%x\"\n", current, tb->nodes[current].byte, (int)tb->nodes[current].left, tb->nodes[tb->nodes[current].left].byte);
            // fprintf(f, "\t\%d -> %d\n", current, (int)tb->nodes[current].left);
            stack[stack_size++] = tb->nodes[current].left;
        }

        if (tb->nodes[current].right != UINT32_MAX)
        {
            fprintf(f, "\t\"%d-%x\" -> \"%d-%x\"\n", current, tb->nodes[current].byte, (int)tb->nodes[current].right, tb->nodes[tb->nodes[current].right].byte);
            // fprintf(f, "\t\%d -> %d\n", current, (int)tb->nodes[current].right);
            stack[stack_size++] = tb->nodes[current].right;
        }
    }
    fprintf(f, "}");
    fclose(f);
}
#endif

uint8_t* huffman_tree(const uint8_t* input, size_t size)
{
    bitarray_t output = {0};

    // Count bytes frequencies.
    size_t frequencies[256] = {0};

    for (size_t i = 0; i < size; ++i)
    {
        frequencies[input[i]]++;
    }

    // Sort frequencies in increasing order.
    // `sorted_frequencies` is an indirection layer to the frequencies.
    size_t sorted_frequencies[256];
    for (size_t i = 0; i < 256; ++i)
    {
        sorted_frequencies[i] = i;
    }

    for (size_t i = 0; i < 256; ++i)
    {
        for (size_t j = 0; j < 256 - i - 1; ++j)
        {
            if (frequencies[sorted_frequencies[j]] > frequencies[sorted_frequencies[j + 1]])
            {
                size_t swap = sorted_frequencies[j + 1];
                sorted_frequencies[j + 1] = sorted_frequencies[j];
                sorted_frequencies[j] = swap;
            }
        }
    }

    tree_builder_t tb;
    init_tree_builder(&tb);

    // Initialize the nodes.
    uint32_t num_valid_frequencies = 0;
    for (size_t i = 0; i < 256; ++i)
    {
        // If the frequency is 0 then there is no need for the byte to appear in the tree.
        if (frequencies[sorted_frequencies[i]] == 0) continue;

        tb.nodes[tb.next_free_node] = (node_t){
            .byte = (uint8_t)sorted_frequencies[i],
            .freq = frequencies[sorted_frequencies[i]],
            .left = UINT32_MAX,
            .right = UINT32_MAX,
            .parent = UINT32_MAX,
        };
        tb.queue_1[tb.queue_1_size] = tb.next_free_node++;
        tb.queue_1_size++;
        num_valid_frequencies++;
    }

    // @Note @Todo: N nodes in the tree will result in a maximum new nodes of N - 1. Thus we can have a maximum of 256 + 256 - 1 nodes = 511.

    // Build huffman tree from the sorted frequencies in O(n) look here for the algorithm
    // https://en.wikipedia.org/wiki/Huffman_coding#Compression
    while (tb.queue_1_size + tb.queue_2_size >= 2)
    {
        uint32_t node1 = select_node(&tb);
        uint32_t node2 = select_node(&tb);

        // Internal node.
        uint32_t new_node = tb.next_free_node++;
        tb.nodes[new_node] = (node_t){
            .freq = tb.nodes[node1].freq + tb.nodes[node2].freq,
            .left = node1,
            .right = node2,
            .parent = UINT32_MAX,
        };
        tb.nodes[node1].parent = new_node;
        tb.nodes[node2].parent = new_node;

        tb.queue_2[(tb.queue_2_head + tb.queue_2_size) % 256] = new_node;
        tb.queue_2_size++;
    }

#ifndef NDEBUG
    // The resulting node is always in the second queue. @Todo: unless there is only one node in queue1 at first.
    uint32_t root = tb.queue_2[tb.queue_2_head];
    write_graph(&tb, root); // @Cleanup: remove, or keep for debug only.
#endif

    // @Note: this is the number of nodes added in queue_1 during initialization.
    // We know for certain that these N first nodes are the leaves of the tree.
    codeword_t codewords[256] = {0}; // The index is the byte.
    for (int i = 0; i < num_valid_frequencies; ++i)
    {
        uint32_t current = i;
        uint8_t byte = tb.nodes[current].byte;
        while (tb.nodes[current].parent != UINT32_MAX)
        {
            uint32_t parent = tb.nodes[current].parent;
            if (tb.nodes[parent].left == current)
            {
                // Write 0 (already initialized to 0).
                codewords[byte].num_bits++;
            }
            else
            {
                // Write 1.
                codewords[byte].bits |= (1 << codewords[i].num_bits);
                codewords[byte].num_bits++;
            }

            current = parent;
        }
    }

    // @Cleanup: for debug purposes only. Remove when done.
    // for (int i = 0; i < num_valid_frequencies; ++i)
    // {
        // printf("0x%x %d\n", i, codewords[i].num_bits);
    // }

    // @Note @Todo: pass over the input and compress using the codewords. As simple as that :)
    const uint8_t* it = input;
    const uint8_t* end = input + size;
    while (it != end)
    {
        // @Todo @Performance: see if it is better to have a buffer of bits on a uint64_t and
        // push only when it is full.
        bitarray_push_bits_msb(&output, codewords[*it].bits, codewords[*it].num_bits);
        it++;
    }

    bitarray_pad_last_byte(&output);
    return output.data;
}
