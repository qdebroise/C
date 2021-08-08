#include "huffman.h"

#include "bitarray.h"

typedef struct node_t
{
    uint8_t byte;
    size_t freq;
    uint32_t left;
    uint32_t right;
} node_t;

typedef struct tree_builder_t
{
    // Build huffman tree from the sorted frequencies.
    // @Todo: see wiki page of huffman tree for O(n) algorithm to build the Huffman tree from
    // a sorted list of frequencies. https://en.wikipedia.org/wiki/Huffman_coding#Compression
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

uint8_t* huffman_tree(const uint8_t* bytes, size_t size)
{
    bitarray_t output = {0};

    // Count bytes frequencies.
    size_t frequencies[256] = {0};

    for (size_t i = 0; i < size; ++i)
    {
        frequencies[bytes[i]]++;
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
            if (frequencies[sorted_frequencies[j]] < frequencies[sorted_frequencies[j + 1]])
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
    for (size_t i = 0; i < 256; ++i)
    {
        // If the frequency is 0 then there is no need for the byte to appear in the tree.
        if (frequencies[sorted_frequencies[i]] == 0) continue;

        tb.nodes[tb.next_free_node] = (node_t){
            // .byte = (uint8_t)i,
            .byte = (uint8_t)sorted_frequencies[i],
            .freq = frequencies[sorted_frequencies[i]],
            .left = UINT32_MAX,
            .right = UINT32_MAX,
        };
        tb.queue_1[i] = tb.next_free_node++;
        tb.queue_1_size++;
    }

    // @Note @Todo: N nodes in the tree will result in a maximum new nodes of N - 1. Thus we can have a maximum of 256 + 256 - 1 nodes = 511.

    while (tb.queue_1_size + tb.queue_2_size >= 2)
    {
        // @Todo
        // node1 = if freq head q1 < freq head q2 pop q1 otherwise pop q2.
        // node2 = if freq head q1 < freq head q2 pop q1 otherwise pop q2.
        //
        // new_node.left = node1;
        // new_node.right = node2;
        // new_node.freq = node1.freq + node2.freq;
        //
        // q2 push new_node.

        uint32_t node1 = select_node(&tb);
        uint32_t node2 = select_node(&tb);

        // Internal node.
        uint32_t new_node = tb.next_free_node++;
        tb.nodes[new_node] = (node_t){
            .freq = tb.nodes[node1].freq + tb.nodes[node2].freq,
            .left = node1,
            .right = node2,
        };

        tb.queue_2[(tb.queue_2_head + tb.queue_2_size) % 256] = new_node;
        tb.queue_2_size++;
    }

    // The resulting node is always in the second queue. @Todo: unless there is only one node in queue1 at first.
    uint32_t root = tb.queue_2[tb.queue_2_head];
    write_graph(&tb, root);

    // @Todo: write codewords. Iterate the tree for each symol and write its codeword. Skip if frequence is empty.

    codeword_t codewords[256]; // The index is the byte.

    bitarray_pad_last_byte(&output);
    return output.data;
}
