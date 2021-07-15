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
    // FILE* f = fopen("bitarray.h", "rb");
    if (!f) return 1;
    fseek(f, 0, SEEK_END);
    size_t end = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* content = malloc(end);
    fread(content, 1, end, f);
    fclose(f);

    clock_t tic, toc;

    printf("Start LZ77 compression\n");
    tic = clock();
    uint8_t* lz77_compressed_data = lz77_compress(content, end);
    toc = clock();
    float lz77_compression_time_s = (float)(toc - tic) / CLOCKS_PER_SEC;

    printf("Start LZ77 decompression\n");
    tic = clock();
    uint8_t* lz77_uncompressed_data = lz77_uncompress(lz77_compressed_data, array_size(lz77_compressed_data));
    toc = clock();
    float lz77_uncompression_time_s = (float)(toc - tic) / CLOCKS_PER_SEC;

    printf("Start LZSS compression\n");
    tic = clock();
    uint8_t* lzss_compressed_data = lzss_compress(content, end);
    toc = clock();
    float lzss_compression_time_s = (float)(toc - tic) / CLOCKS_PER_SEC;

    printf("Start LZSS decompression\n");
    tic = clock();
    uint8_t* lzss_uncompressed_data = lzss_uncompress(lzss_compressed_data, array_size(lzss_compressed_data));
    toc = clock();
    float lzss_uncompression_time_s = (float)(toc - tic) / CLOCKS_PER_SEC;

    printf("LZ77:\n\tCompression rate (%%): %.3f\n\tCompression time (s): %f\n\tUncompression time (s): %f\n",
            (1 - array_size(lz77_compressed_data) / (float)array_size(lz77_uncompressed_data)) * 100,
            lz77_compression_time_s, lz77_uncompression_time_s);
    printf("LZSS:\n\tCompression rate (%%): %.3f\n\tCompression time (s): %f\n\tUncompression time (s): %f\n",
            (1 - array_size(lzss_compressed_data) / (float)array_size(lzss_uncompressed_data)) * 100,
            lzss_compression_time_s, lzss_uncompression_time_s);

    /*
    printf("LZSS compressed data stream:\n");
    for (size_t i = 0; i < array_size(lzss_compressed_data); ++i)
    {
        printf("0x%x ", lzss_compressed_data[i]);
    }
    printf("\nCompressed size: %lu bytes\n\n", array_size(lzss_compressed_data));

    printf("Uncompressed data stream:\n");
    for (uint8_t* byte = lzss_uncompressed_data; byte < lzss_uncompressed_data + array_size(lzss_uncompressed_data); ++byte)
    {
        printf("%c", *byte);
    }
    printf("\nUncompressed size: %lu bytes\n", array_size(lzss_uncompressed_data));
    */

    array_free(lz77_compressed_data);
    array_free(lz77_uncompressed_data);
    array_free(lzss_compressed_data);
    array_free(lzss_uncompressed_data);
    free(content);

    return 0;
}
