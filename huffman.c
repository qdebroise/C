#include "huffman.h"

#include "bitarray.h"

// This implementation of Huffman compression is designed to compress bytes. Thus a symbol of the
// alphabet ranges in [0x00, 0xff].
#define ALPHABET_SIZE 256

// The maximum number of nodes that can compose the tree. A binary tree with N leaves can have at
// most N - 1 additional nodes. This gives the maximum number of nodes in the tree to be N + N - 1.
#define TREE_MAX_NODES (2 * ALPHABET_SIZE - 1)
// Because we use indices and not pointer this Value is used to tag a node as empty.
#define TREE_EMPTY_NODE ((uint32_t)(1ULL << 31))

// Size of the queues used to build the Huffman tree.
#define LEAVES_QUEUE_SIZE ALPHABET_SIZE
// @Note @Todo: I think this can be at most 128 long. The only way this queue can grow is if we take two nodes from queue1 resulting in a single new node in queue2. Thus, because we have 256 nodes in queue1 we can only have 256/2=128 nodes in this queue.
#define PARENTS_QUEUE_SIZE ALPHABET_SIZE

typedef struct node_t
{
    uint8_t byte;
    size_t freq;
    uint32_t left;
    uint32_t right;
    uint32_t parent;
} node_t;

typedef struct huffman_tree_o
{
    struct
    {
        size_t size;
        size_t head;
        uint32_t data[LEAVES_QUEUE_SIZE]; // Indirection into `nodes`.
    } leaves_queue;

    struct
    {
        size_t size;
        size_t head;
        uint32_t data[PARENTS_QUEUE_SIZE]; // Indirection into `nodes`.
    } parents_queue;

    uint32_t next_free_node;
    node_t nodes[511];
} huffman_tree_o;

typedef struct codeword_t
{
    uint8_t num_bits;
    uint32_t bits;
} codeword_t;

void init_huffman_tree(huffman_tree_o* tree)
{
    tree->next_free_node = 0;
    tree->leaves_queue.size = 0;
    tree->leaves_queue.head = 0;
    tree->parents_queue.size = 0;
    tree->parents_queue.head = 0;

    for (int i = 0; i < LEAVES_QUEUE_SIZE; ++i)
    {
        tree->leaves_queue.data[i] = UINT32_MAX;
    }

    for (int i = 0; i < PARENTS_QUEUE_SIZE; ++i)
    {
        tree->parents_queue.data[i] = UINT32_MAX;
    }
}

uint32_t leaves_queue_dequeue(huffman_tree_o* tree)
{
    uint32_t selected_node = tree->leaves_queue.data[tree->leaves_queue.head];
    tree->leaves_queue.head = (tree->leaves_queue.head + 1) % LEAVES_QUEUE_SIZE;
    tree->leaves_queue.size--;
    return selected_node;
}

uint32_t parents_queue_dequeue(huffman_tree_o* tree)
{
    uint32_t selected_node = tree->parents_queue.data[tree->parents_queue.head];
    tree->parents_queue.head = (tree->parents_queue.head + 1) % PARENTS_QUEUE_SIZE;
    tree->parents_queue.size--;
    return selected_node;
}

uint32_t select_node_with_smallest_frequency(huffman_tree_o* tree)
{
    assert(tree->leaves_queue.size + tree->parents_queue.size >= 1 && "Both queues are empty.");

    uint32_t selected_node;

    if (tree->parents_queue.size == 0)
    {
        // Only `leaves_queue` has nodes.
        selected_node = leaves_queue_dequeue(tree);
    }
    else if (tree->leaves_queue.size == 0)
    {
        // Only `parents_queue` has nodes.
        selected_node = parents_queue_dequeue(tree);
    }
    // Both queues have nodes. Select the one with smallest frequency.
    else if (tree->nodes[tree->leaves_queue.data[tree->leaves_queue.head]].freq <= tree->nodes[tree->parents_queue.data[tree->parents_queue.head]].freq)
    {
        // Select from `leaves_queue`.
        selected_node = leaves_queue_dequeue(tree);
    }
    else
    {
        // Select from `parents_queue`.
        selected_node = parents_queue_dequeue(tree);
    }

    return selected_node;
}

#ifndef NDEBUG
#include <stdio.h>
// Write graph in a file using DOT graph language.
// Nodes names are "X-Y" where X is the node index in huffman tree nodes.
// Y is the byte in hexadecimal.
void write_graph(const huffman_tree_o* tree, uint32_t root)
{
    uint32_t stack[TREE_MAX_NODES];
    stack[0] = root;
    uint32_t stack_size = 1;

    FILE* f = fopen("huffman_tree.txt", "w");
    fprintf(f, "digraph G {\n");
    while (stack_size--)
    {
        uint32_t current = stack[stack_size];

        if (tree->nodes[current].left != UINT32_MAX)
        {
            fprintf(f, "\t\"%d-%x\" -> \"%d-%x\"\n", current, tree->nodes[current].byte, (int)tree->nodes[current].left, tree->nodes[tree->nodes[current].left].byte);
            stack[stack_size++] = tree->nodes[current].left;
        }

        if (tree->nodes[current].right != UINT32_MAX)
        {
            fprintf(f, "\t\"%d-%x\" -> \"%d-%x\"\n", current, tree->nodes[current].byte, (int)tree->nodes[current].right, tree->nodes[tree->nodes[current].right].byte);
            stack[stack_size++] = tree->nodes[current].right;
        }
    }
    fprintf(f, "}");
    fclose(f);
}
#endif

uint8_t* huffman_compress(const uint8_t* input, size_t size)
{
    bitarray_t output = {0};

    // Count bytes frequencies.
    size_t frequencies[ALPHABET_SIZE] = {0};

    for (size_t i = 0; i < size; ++i)
    {
        frequencies[input[i]]++;
    }

    // Sort frequencies in increasing order.
    // `sorted_frequencies` is an indirection layer to the frequencies.
    size_t sorted_frequencies[ALPHABET_SIZE];
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

    huffman_tree_o tree;
    init_huffman_tree(&tree);

    // Initialize the nodes.
    uint32_t num_valid_frequencies = 0;
    for (size_t i = 0; i < 256; ++i)
    {
        // If the frequency is 0 then there is no need for the byte to appear in the tree.
        if (frequencies[sorted_frequencies[i]] == 0) continue;

        tree.nodes[tree.next_free_node] = (node_t){
            .byte = (uint8_t)sorted_frequencies[i],
            .freq = frequencies[sorted_frequencies[i]],
            .left = UINT32_MAX,
            .right = UINT32_MAX,
            .parent = UINT32_MAX,
        };
        tree.leaves_queue.data[tree.leaves_queue.size] = tree.next_free_node++;
        tree.leaves_queue.size++;
        num_valid_frequencies++;
    }

    // @Note @Todo: N nodes in the tree will result in a maximum new nodes of N - 1. Thus we can have a maximum of 256 + 256 - 1 nodes = 511.

    // Build huffman tree from the sorted frequencies in O(n) look here for the algorithm
    // https://en.wikipedia.org/wiki/Huffman_coding#Compression
    while (tree.leaves_queue.size + tree.parents_queue.size >= 2)
    {
        uint32_t node1 = select_node_with_smallest_frequency(&tree);
        uint32_t node2 = select_node_with_smallest_frequency(&tree);

        // Internal node.
        uint32_t new_node = tree.next_free_node++;
        tree.nodes[new_node] = (node_t){
            .freq = tree.nodes[node1].freq + tree.nodes[node2].freq,
            .left = node1,
            .right = node2,
            .parent = UINT32_MAX,
        };
        tree.nodes[node1].parent = new_node;
        tree.nodes[node2].parent = new_node;

        tree.parents_queue.data[(tree.parents_queue.head + tree.parents_queue.size) % 256] = new_node;
        tree.parents_queue.size++;
    }

#ifndef NDEBUG
    // The resulting node is always in the second queue. @Todo: unless there is only one node in queue1 at first.
    uint32_t root = tree.parents_queue.data[tree.parents_queue.head];
    write_graph(&tree, root); // @Cleanup: remove, or keep for debug only.
#endif

    // @Note: this is the number of nodes added in leaves_queue.data during initialization.
    // We know for certain that these N first nodes are the leaves of the tree.
    codeword_t codewords[256] = {0}; // The index is the byte.
    for (int i = 0; i < num_valid_frequencies; ++i)
    {
        uint32_t current = i;
        uint8_t byte = tree.nodes[current].byte;
        while (tree.nodes[current].parent != UINT32_MAX)
        {
            uint32_t parent = tree.nodes[current].parent;
            if (tree.nodes[parent].left == current)
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

uint8_t* huffman_uncompress(const uint8_t* compressed_data, size_t size)
{
    // @Todo: impl.
    return NULL;
}
