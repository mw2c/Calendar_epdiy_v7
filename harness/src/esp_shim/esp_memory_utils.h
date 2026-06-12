#pragma once

#include <stdbool.h>
#include <stddef.h>

static inline bool esp_ptr_byte_accessible(const void *ptr) {
    return ptr != NULL;
}
