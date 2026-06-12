#pragma once

#include <stdint.h>

// Writes an 8-bit grayscale PNG. Returns 0 on success, -1 on failure.
int png_write_gray8(const char *path, const uint8_t *pixels, int width, int height);
