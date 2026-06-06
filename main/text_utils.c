#include "text_utils.h"

char text_printable_char(char c) {
    unsigned char uc = (unsigned char)c;

    if (c == '\n' || c == '\r' || c == '\t') {
        return c;
    }
    if (uc >= 0x20 && uc <= 0x7e) {
        return c;
    }

    return '?';
}

void text_copy_sanitized(char *dest, size_t dest_size, const char *src) {
    if (dest_size == 0) {
        return;
    }

    size_t i = 0;
    while (i + 1 < dest_size && src[i] != '\0') {
        unsigned char byte = (unsigned char)src[i];
        if (byte < 0x20 && src[i] != '\n' && src[i] != '\r' && src[i] != '\t') {
            dest[i] = '?';
        } else {
            dest[i] = src[i];
        }
        i++;
    }
    dest[i] = '\0';
}
