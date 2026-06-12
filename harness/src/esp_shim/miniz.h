#pragma once

// Minimal tinfl-compatible shim backed by system zlib (see miniz_shim.c).
// Supports exactly the one-shot, whole-buffer call pattern used by epdiy's
// font.c (TINFL_FLAG_PARSE_ZLIB_HEADER on a complete zlib stream).
// Deliberately does not include <zlib.h>: font.c defines its own static
// `uncompress`, which would clash with zlib's declaration.

#include <stddef.h>

typedef unsigned char mz_uint8;
typedef unsigned int mz_uint32;

typedef struct {
    int unused;
} tinfl_decompressor;

typedef enum {
    TINFL_STATUS_FAILED = -1,
    TINFL_STATUS_DONE = 0,
} tinfl_status;

#define TINFL_FLAG_PARSE_ZLIB_HEADER 1
#define TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF 4

#define tinfl_init(r) \
    do {              \
        (void)(r);    \
    } while (0)

tinfl_status tinfl_decompress(
    tinfl_decompressor *r,
    const mz_uint8 *in_buf,
    size_t *in_buf_size,
    mz_uint8 *out_buf_start,
    mz_uint8 *out_buf_next,
    size_t *out_buf_size,
    mz_uint32 flags
);
