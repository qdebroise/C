#include "array.h"
#include "huffman.h"
#include "lz.h"

#include <stdio.h>
#include <time.h> // very basic and oversimplified timing.

// https://go-compression.github.io/algorithms/lzss/
// https://gist.github.com/fogus/5401265
// https://github.com/cstdvd/lz77
int main(int argc, char* argv[])
{
    // static const char content[] = "abcabcabcabc";
    // static const char content[] = "abracadabra";
    // static const char content[] = "les chaussettes de l'archiduchesse sont elles seches archiseches.";
    // static const char content[] = "les chaussettes duchesse";
    // static const char content[] = "bbba";
    // static const char content[] = "aacaacabcabaaac";
    // static const size_t end = sizeof(content) / sizeof(content[0]);

    FILE* f = fopen(argv[1], "rb");
    // FILE* f = fopen("test/bible.txt", "rb");
    // FILE* f = fopen("tmp.txt", "rb");
    if (!f) return 1;
    fseek(f, 0, SEEK_END);
    size_t end = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* content = malloc(end);
    fread(content, 1, end, f);
    fclose(f);

    clock_t tic, toc;

    printf("Start LZ compression\n");
    tic = clock();
    uint8_t* compressed_data = lz_compress(content, end);
    toc = clock();
    float compression_time_s = (float)(toc - tic) / CLOCKS_PER_SEC;

    // @Note @Todo: put huffman compression outside for now. Later on lz + huffman will be combined to yield a single compression function.
    // For now, for easier debugging and algorithm building the two are seperated.
    printf("Start Huffman compression\n");
    tic = clock();
    uint8_t* huffman_output = huffman_compress(compressed_data, array_size(compressed_data));
    toc = clock();
    float huffman_time_s = (float)(toc - tic) / CLOCKS_PER_SEC;

    printf("Start Huffman decompression\n");
    tic = clock();
    uint8_t* uncompressed_huffman = huffman_uncompress(huffman_output, array_size(huffman_output));
    toc = clock();
    float huffman_uncompress_time_s = (float)(toc - tic) / CLOCKS_PER_SEC;

    printf("Start LZ decompression\n");
    tic = clock();
    uint8_t* uncompressed_data = lz_uncompress(compressed_data, array_size(compressed_data));
    // @Note: we can't directly fedd the output of huffman uncompression because the huffman decoder
    // decodes a bit more bytes that in the original data. This is due to a lack of knowledge on
    // when to stop decoding. This is no big deal for now as all this will be fixed when things will
    // be put together for Deflate.
    // uint8_t* uncompressed_data = lz_uncompress(uncompressed_huffman, array_size(compressed_data));
    toc = clock();
    float uncompression_time_s = (float)(toc - tic) / CLOCKS_PER_SEC;

    printf("LZ:\n\tInput size (Mb): %f\n\tOutput size (Mb): %f\n\tCompression rate (%%): %.3f\n\tCompression time (s): %f\n\tUncompression time (s): %f\n",
            (float)end / (1 << 20),
            (float)array_size(compressed_data) / (1 << 20),
            (1 - array_size(compressed_data) / (float)array_size(uncompressed_data)) * 100,
            compression_time_s, uncompression_time_s);
    printf("Huffman:\n\tInput size (Mb): %f\n\tOutput size (Mb): %f\n\tCompression rate (%%): %.3f\n\tHuffman time (s): %.3f\n\tUncompression time (s): %f\n",
            (float)array_size(compressed_data) / (1 << 20),
            (float)array_size(huffman_output) / (1 << 20),
            (1 - array_size(huffman_output) / (float)array_size(compressed_data)) * 100,
            huffman_time_s, huffman_uncompress_time_s);
    printf("Total (LZ + Huffman):\n\tInput size (Mb): %f\n\tOutput size (Mb): %f\n\tCompression rate (%%): %.3f\n\tCompression time (s): %.3f\n",
            (float)array_size(uncompressed_data) / (1 << 20),
            (float)array_size(huffman_output) / (1 << 20),
            (1 - array_size(huffman_output) / (float)array_size(uncompressed_data)) * 100,
            compression_time_s + huffman_time_s);

    /*
    printf("%s\n", uncompressed_data);
    printf("LZ output:\n");
    for (size_t i = 0; i < array_size(compressed_data); ++i)
    {
        printf("%x ", compressed_data[i]);
    }
    printf("\n");
    printf("Huffman uncompressed output:\n");
    for (size_t i = 0; i < array_size(uncompressed_huffman); ++i)
    {
        printf("%x ", uncompressed_huffman[i]);
    }
    printf("\n\n");
    */

    printf("LZ output size: %lu B\n", array_size(compressed_data));
    printf("Huffman output size: %lu B\n", array_size(huffman_output));
    printf("Uncompressed huffman size: %lu B\n", array_size(uncompressed_huffman));
    printf("Uncompressed LZ size: %lu B\n", array_size(uncompressed_data));
    printf("Original size: %lu B\n", end);
    /*
    printf("LZ compressed data stream:\n");
    for (size_t i = 0; i < array_size(compressed_data); ++i)
    {
        printf("0x%x ", compressed_data[i]);
    }
    printf("\nCompressed size: %lu bytes\n\n", array_size(compressed_data));
    */

    // printf("%.*s\n", array_size(uncompressed_data), uncompressed_data);
    printf("\nOriginal size: %lu bytes\n", end);
    printf("LZ uncompressed size: %lu bytes\n", array_size(uncompressed_data));
    printf("LZ+Huffman uncompressed size: ??\n");

    FILE* fout = fopen("decompressed_file", "wb");
    fwrite(uncompressed_data, 1, array_size(uncompressed_data), fout);
    fclose(fout);

    array_free(compressed_data);
    array_free(uncompressed_data);
    array_free(huffman_output);
    array_free(uncompressed_huffman);
    free(content);

    return 0;
}
