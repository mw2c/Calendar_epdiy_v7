#pragma once

#include <stddef.h>

char text_printable_char(char c);
void text_copy_sanitized(char *dest, size_t dest_size, const char *src);
