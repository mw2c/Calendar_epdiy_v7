// Host-side stand-in for the epdiy driver.
// Framebuffer layout, rotation, and glyph rendering are transcribed from
// components/epdiy2/src/epdiy.c and font.c so results match the device.
// Hardware control functions are no-ops.

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zlib.h>

#include <epdiy.h>

#include "epd_stub.h"
#include "png_writer.h"

typedef struct {
    uint16_t x;
    uint16_t y;
} Coord_xy;

#define _swap_int(a, b) \
    {                   \
        int t = a;      \
        a = b;          \
        b = t;          \
    }

static inline int min_int(int x, int y) {
    return x < y ? x : y;
}

static inline int max_int(int x, int y) {
    return x > y ? x : y;
}

// ---- board / display data ----------------------------------------------

const EpdDisplay_t ED060KD1 = {
    .width = 1448,
    .height = 1072,
    .bus_width = 8,
    .bus_speed = 20,
    .default_waveform = NULL,
    .display_type = DISPLAY_TYPE_GENERIC,
};

const EpdBoardDefinition epd_board_epdiy2_s3 = { 0 };

static const EpdDisplay_t *s_display = NULL;
static enum EpdRotation s_rotation = EPD_ROT_LANDSCAPE;
static char s_output_path[1024] = "render.png";

void epd_stub_set_output_path(const char *path) {
    snprintf(s_output_path, sizeof(s_output_path), "%s", path);
}

const char *epd_stub_get_output_path(void) {
    return s_output_path;
}

// ---- lifecycle / hardware no-ops -----------------------------------------

void epd_init(
    const EpdBoardDefinition *board, const EpdDisplay_t *display, enum EpdInitOptions options
) {
    (void)board;
    (void)options;
    s_display = display;
}

void epd_deinit(void) {
}

void epd_set_vcom(uint16_t vcom) {
    (void)vcom;
}

void epd_poweron(void) {
}

void epd_poweroff(void) {
}

void epd_clear(void) {
    // On hardware this flashes the panel; the framebuffer is untouched.
}

float epd_ambient_temperature(void) {
    return 25.0f;
}

int epd_width(void) {
    assert(s_display != NULL);
    return s_display->width;
}

int epd_height(void) {
    assert(s_display != NULL);
    return s_display->height;
}

// ---- rotation (transcribed from epdiy.c) ----------------------------------

void epd_set_rotation(enum EpdRotation rotation) {
    s_rotation = rotation;
}

enum EpdRotation epd_get_rotation(void) {
    return s_rotation;
}

int epd_rotated_display_width(void) {
    int display_width = epd_width();
    switch (s_rotation) {
        case EPD_ROT_PORTRAIT:
        case EPD_ROT_INVERTED_PORTRAIT:
            display_width = epd_height();
            break;
        case EPD_ROT_INVERTED_LANDSCAPE:
        case EPD_ROT_LANDSCAPE:
            break;
    }
    return display_width;
}

int epd_rotated_display_height(void) {
    int display_height = epd_height();
    switch (s_rotation) {
        case EPD_ROT_PORTRAIT:
        case EPD_ROT_INVERTED_PORTRAIT:
            display_height = epd_width();
            break;
        case EPD_ROT_INVERTED_LANDSCAPE:
        case EPD_ROT_LANDSCAPE:
            break;
    }
    return display_height;
}

static Coord_xy _rotate(uint16_t x, uint16_t y) {
    switch (s_rotation) {
        case EPD_ROT_LANDSCAPE:
            break;
        case EPD_ROT_PORTRAIT:
            _swap_int(x, y);
            x = epd_width() - x - 1;
            break;
        case EPD_ROT_INVERTED_LANDSCAPE:
            x = epd_width() - x - 1;
            y = epd_height() - y - 1;
            break;
        case EPD_ROT_INVERTED_PORTRAIT:
            _swap_int(x, y);
            y = epd_height() - y - 1;
            break;
    }
    Coord_xy coord = { x, y };
    return coord;
}

// ---- pixel / rect drawing (transcribed from epdiy.c) -----------------------

void epd_draw_pixel(int x, int y, uint8_t color, uint8_t *framebuffer) {
    Coord_xy coord = _rotate((uint16_t)x, (uint16_t)y);
    x = coord.x;
    y = coord.y;

    if (x < 0 || x >= epd_width()) {
        return;
    }
    if (y < 0 || y >= epd_height()) {
        return;
    }

    uint8_t *buf_ptr = &framebuffer[y * epd_width() / 2 + x / 2];
    if (x % 2) {
        *buf_ptr = (*buf_ptr & 0x0F) | (color & 0xF0);
    } else {
        *buf_ptr = (*buf_ptr & 0xF0) | (color >> 4);
    }
}

void epd_draw_hline(int x, int y, int length, uint8_t color, uint8_t *framebuffer) {
    for (int i = 0; i < length; i++) {
        epd_draw_pixel(x + i, y, color, framebuffer);
    }
}

void epd_draw_vline(int x, int y, int length, uint8_t color, uint8_t *framebuffer) {
    for (int i = 0; i < length; i++) {
        epd_draw_pixel(x, y + i, color, framebuffer);
    }
}

void epd_fill_rect(EpdRect rect, uint8_t color, uint8_t *framebuffer) {
    for (int i = rect.y; i < rect.y + rect.height; i++) {
        epd_draw_hline(rect.x, i, rect.width, color, framebuffer);
    }
}

uint8_t epd_get_pixel(int x, int y, int fb_width, int fb_height, const uint8_t *framebuffer) {
    if (x < 0 || x >= fb_width) {
        return 0;
    }
    if (y < 0 || y >= fb_height) {
        return 0;
    }
    int fb_width_bytes = fb_width / 2 + fb_width % 2;
    uint8_t buf_val = framebuffer[y * fb_width_bytes + x / 2];
    if (x % 2) {
        buf_val = (buf_val & 0xF0) >> 4;
    } else {
        buf_val = (buf_val & 0x0F);
    }
    return buf_val << 4;
}
