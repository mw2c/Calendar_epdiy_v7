#include <miniz.h>

#include <zlib.h>

tinfl_status tinfl_decompress(
    tinfl_decompressor *r,
    const mz_uint8 *in_buf,
    size_t *in_buf_size,
    mz_uint8 *out_buf_start,
    mz_uint8 *out_buf_next,
    size_t *out_buf_size,
    mz_uint32 flags
) {
    (void)r;
    (void)out_buf_start;
    (void)flags;
    uLongf out_len = (uLongf)*out_buf_size;
    int rc = uncompress(out_buf_next, &out_len, in_buf, (uLong)*in_buf_size);
    *out_buf_size = (size_t)out_len;
    return rc == Z_OK ? TINFL_STATUS_DONE : TINFL_STATUS_FAILED;
}
