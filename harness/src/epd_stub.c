// Host-side hardware boundary for the epdiy driver.
// The real epdiy rendering code (epdiy.c, font.c, displays.c,
// builtin_waveforms.c) is compiled into the harness unchanged; this file
// stubs only the board/render layer that would touch ESP32-S3 peripherals,
// and gives epd_hl_update_screen() its host semantics: exporting the
// framebuffer as a grayscale PNG.

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <epdiy.h>

#include "output_common/render_method.h"
#include "render.h"

#include "epd_stub.h"
#include "png_writer.h"

static char s_output_path[1024] = "render.png";

void epd_stub_set_output_path(const char *path) {
    snprintf(s_output_path, sizeof(s_output_path), "%s", path);
}

const char *epd_stub_get_output_path(void) {
    return s_output_path;
}

// ---- board (normally board/epd_board.c + a board definition) ---------------

static void stub_poweron(epd_ctrl_state_t *state) {
    (void)state;
}

static void stub_poweroff(epd_ctrl_state_t *state) {
    (void)state;
}

static void stub_set_vcom(int vcom) {
    (void)vcom;
}

static float stub_get_temperature(void) {
    return 25.0f;
}

const EpdBoardDefinition epd_board_epdiy2_s3 = {
    .poweron = stub_poweron,
    .poweroff = stub_poweroff,
    .set_vcom = stub_set_vcom,
    .get_temperature = stub_get_temperature,
};

static const EpdBoardDefinition *s_board = NULL;
static epd_ctrl_state_t s_ctrl_state;

void epd_set_board(const EpdBoardDefinition *board) {
    s_board = board;
}

const EpdBoardDefinition *epd_current_board(void) {
    return s_board;
}

epd_ctrl_state_t *epd_ctrl_state(void) {
    return &s_ctrl_state;
}

// ---- render layer (normally render.c and the LCD/I2S drivers) ---------------

const enum EpdRenderMethod EPD_CURRENT_RENDER_METHOD = RENDER_METHOD_LCD;

void epd_renderer_init(enum EpdInitOptions options, const EpdInitConfig *config) {
    (void)options;
    (void)config;
}

void epd_renderer_deinit(void) {
}

void epd_clear_area(EpdRect area) {
    // On hardware this flashes the panel; the framebuffer is untouched.
    (void)area;
}

enum EpdDrawError epd_draw_base(
    EpdRect area,
    const uint8_t *data,
    EpdRect crop_to,
    enum EpdDrawMode mode,
    int temperature,
    const bool *drawn_lines,
    const uint8_t *drawn_columns,
    const EpdWaveform *waveform
) {
    (void)area;
    (void)data;
    (void)crop_to;
    (void)mode;
    (void)temperature;
    (void)drawn_lines;
    (void)drawn_columns;
    (void)waveform;
    return EPD_DRAW_SUCCESS;
}

void epd_lcd_set_pixel_clock_MHz(int frequency);
void epd_lcd_set_pixel_clock_MHz(int frequency) {
    (void)frequency;
}

// ---- highlevel API (normally highlevel.c; PNG export on the host) -----------

static size_t fb_size(void) {
    return (size_t)(epd_width() / 2) * (size_t)epd_height();
}

EpdiyHighlevelState epd_hl_init(const EpdWaveform *waveform) {
    EpdiyHighlevelState state = { 0 };
    state.waveform = waveform;
    state.front_fb = malloc(fb_size());
    if (state.front_fb == NULL) {
        fprintf(stderr, "epd_stub: framebuffer allocation failed\n");
        exit(1);
    }
    memset(state.front_fb, 0xFF, fb_size());
    return state;
}

uint8_t *epd_hl_get_framebuffer(EpdiyHighlevelState *state) {
    return state->front_fb;
}

void epd_hl_set_all_white(EpdiyHighlevelState *state) {
    memset(state->front_fb, 0xFF, fb_size());
}

// Maps a rotated (app-space) coordinate to the native framebuffer coordinate,
// mirroring epdiy's rotation transform. The relationship to the real
// epd_draw_pixel() is locked down by test_portrait_pixel_mapping.
static void rotated_to_native(int x, int y, int *nx, int *ny) {
    switch (epd_get_rotation()) {
        case EPD_ROT_PORTRAIT:
            *nx = epd_width() - y - 1;
            *ny = x;
            break;
        case EPD_ROT_INVERTED_LANDSCAPE:
            *nx = epd_width() - x - 1;
            *ny = epd_height() - y - 1;
            break;
        case EPD_ROT_INVERTED_PORTRAIT:
            *nx = y;
            *ny = epd_height() - x - 1;
            break;
        case EPD_ROT_LANDSCAPE:
        default:
            *nx = x;
            *ny = y;
            break;
    }
}

enum EpdDrawError epd_hl_update_screen(
    EpdiyHighlevelState *state, enum EpdDrawMode mode, int temperature
) {
    (void)mode;
    (void)temperature;

    int out_w = epd_rotated_display_width();
    int out_h = epd_rotated_display_height();
    uint8_t *gray = malloc((size_t)out_w * (size_t)out_h);
    if (gray == NULL) {
        return EPD_DRAW_FAILED_ALLOC;
    }

    for (int y = 0; y < out_h; y++) {
        for (int x = 0; x < out_w; x++) {
            int nx = 0;
            int ny = 0;
            rotated_to_native(x, y, &nx, &ny);
            uint8_t nibble
                = epd_get_pixel(nx, ny, epd_width(), epd_height(), state->front_fb) >> 4;
            gray[(size_t)y * out_w + x] = (uint8_t)(nibble * 17);
        }
    }

    int rc = png_write_gray8(s_output_path, gray, out_w, out_h);
    free(gray);
    if (rc != 0) {
        fprintf(stderr, "epd_stub: failed to write %s\n", s_output_path);
        return EPD_DRAW_FAILED_ALLOC;
    }
    return EPD_DRAW_SUCCESS;
}
