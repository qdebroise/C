#include "array.h"
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
    // static const char content[] = "aacaacabcabaaac";
    // static const size_t end = sizeof(content) / sizeof(content[0]);

    FILE* f = fopen("test/bible.txt", "rb");
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

    printf("Start LZ decompression\n");
    tic = clock();
    uint8_t* uncompressed_data = lz_uncompress(compressed_data, array_size(compressed_data));
    toc = clock();
    float uncompression_time_s = (float)(toc - tic) / CLOCKS_PER_SEC;

    printf("LZ:\n\tInput size: %f Mb\n\tOutput size: %f Mb\n\tCompression rate (%%): %.3f\n\tCompression time (s): %f\n\tUncompression time (s): %f\n",
            (float)end / (1 << 20),
            (float)array_size(compressed_data) / (1 << 20),
            (1 - array_size(compressed_data) / (float)array_size(uncompressed_data)) * 100,
            compression_time_s, uncompression_time_s);

    /*
    printf("LZ compressed data stream:\n");
    for (size_t i = 0; i < array_size(compressed_data); ++i)
    {
        printf("0x%x ", compressed_data[i]);
    }
    printf("\nCompressed size: %lu bytes\n\n", array_size(compressed_data));
    */

    /*
    printf("Uncompressed data stream:\n");
    for (int i = 0; i < array_size(uncompressed_data); ++i)
    {
        printf("%c", uncompressed_data[i]);
    }
    */
    printf("\nUncompressed size: %lu bytes\n", array_size(uncompressed_data));
    printf("Original size: %lu bytes\n", end);

    array_free(compressed_data);
    array_free(uncompressed_data);
    free(content);

    return 0;
}
