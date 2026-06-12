#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <epdiy.h>

#include "app_config.h"
#include "png_writer.h"

static int failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static void test_png_writer_roundtrip(void) {
    const char *path = "harness_test_out.png";
    uint8_t pixels[4 * 2] = { 0, 85, 170, 255, 255, 170, 85, 0 };
    CHECK(png_write_gray8(path, pixels, 4, 2) == 0);

    FILE *f = fopen(path, "rb");
    CHECK(f != NULL);
    if (f) {
        uint8_t header[24] = { 0 };
        CHECK(fread(header, 1, 24, f) == 24);
        const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
        CHECK(memcmp(header, sig, 8) == 0);
        uint32_t w = ((uint32_t)header[16] << 24) | ((uint32_t)header[17] << 16)
            | ((uint32_t)header[18] << 8) | (uint32_t)header[19];
        uint32_t h = ((uint32_t)header[20] << 24) | ((uint32_t)header[21] << 16)
            | ((uint32_t)header[22] << 8) | (uint32_t)header[23];
        CHECK(w == 4);
        CHECK(h == 2);
        fclose(f);
        remove(path);
    }
}

int main(void) {
    CHECK(DISPLAY_VCOM_MV == 1560);

    test_png_writer_roundtrip();

    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("all checks passed\n");
    return 0;
}
