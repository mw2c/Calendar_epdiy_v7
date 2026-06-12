#include "png_writer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zlib.h>

static void put_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static int write_chunk(FILE *f, const char type[4], const uint8_t *data, uint32_t len) {
    uint8_t hdr[8];
    put_be32(hdr, len);
    memcpy(hdr + 4, type, 4);
    if (fwrite(hdr, 1, 8, f) != 8) {
        return -1;
    }
    if (len > 0 && fwrite(data, 1, len, f) != len) {
        return -1;
    }
    uint32_t crc = (uint32_t)crc32(0, (const uint8_t *)type, 4);
    if (len > 0) {
        crc = (uint32_t)crc32(crc, data, len);
    }
    uint8_t crc_be[4];
    put_be32(crc_be, crc);
    if (fwrite(crc_be, 1, 4, f) != 4) {
        return -1;
    }
    return 0;
}

int png_write_gray8(const char *path, const uint8_t *pixels, int width, int height) {
    if (path == NULL || pixels == NULL || width <= 0 || height <= 0) {
        return -1;
    }

    // Raw image stream: one filter byte (0 = None) before each scanline.
    size_t stride = (size_t)width + 1;
    size_t raw_size = stride * (size_t)height;
    uint8_t *raw = malloc(raw_size);
    if (raw == NULL) {
        return -1;
    }
    for (int y = 0; y < height; y++) {
        raw[(size_t)y * stride] = 0;
        memcpy(raw + (size_t)y * stride + 1, pixels + (size_t)y * width, (size_t)width);
    }

    uLongf comp_size = compressBound((uLong)raw_size);
    uint8_t *comp = malloc(comp_size);
    if (comp == NULL) {
        free(raw);
        return -1;
    }
    int zrc = compress2(comp, &comp_size, raw, (uLong)raw_size, Z_BEST_SPEED);
    free(raw);
    if (zrc != Z_OK) {
        free(comp);
        return -1;
    }

    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        free(comp);
        return -1;
    }

    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    uint8_t ihdr[13];
    put_be32(ihdr, (uint32_t)width);
    put_be32(ihdr + 4, (uint32_t)height);
    ihdr[8] = 8;   // bit depth
    ihdr[9] = 0;   // color type: grayscale
    ihdr[10] = 0;  // compression method
    ihdr[11] = 0;  // filter method
    ihdr[12] = 0;  // interlace: none

    int ok = fwrite(sig, 1, 8, f) == 8 && write_chunk(f, "IHDR", ihdr, 13) == 0
        && write_chunk(f, "IDAT", comp, (uint32_t)comp_size) == 0
        && write_chunk(f, "IEND", NULL, 0) == 0;
    free(comp);
    if (fclose(f) != 0) {
        ok = 0;
    }
    return ok ? 0 : -1;
}
