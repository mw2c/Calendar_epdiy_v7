#pragma once

#include <stdbool.h>

typedef enum {
    BUTTON_INPUT_NONE = 0,
    BUTTON_INPUT_KEY_1 = 1,
    BUTTON_INPUT_KEY_2 = 2,
    BUTTON_INPUT_KEY_3 = 3,
} ButtonInputKey;

bool button_input_init(void);
ButtonInputKey button_input_poll(int *raw_out);
const char *button_input_key_name(ButtonInputKey key);
