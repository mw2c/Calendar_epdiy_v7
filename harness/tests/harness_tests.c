#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <epdiy.h>

#include "app_config.h"
#include "epd_stub.h"
#include "png_writer.h"

// Font data arrays are defined in the header; include it in this binary only.
#include "firasans_12.h"

static int failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static uint8_t *alloc_white_fb(void) {
    size_t size = (size_t)(epd_width() / 2) * (size_t)epd_height();
    uint8_t *fb = malloc(size);
    CHECK(fb != NULL);
    if (fb != NULL) {
        memset(fb, 0xFF, size);
    }
    return fb;
}

static size_t count_ink_nibbles(const uint8_t *fb) {
    size_t size = (size_t)(epd_width() / 2) * (size_t)epd_height();
    size_t ink = 0;
    for (size_t i = 0; i < size; i++) {
        if ((fb[i] & 0x0F) != 0x0F) {
            ink++;
        }
        if ((fb[i] & 0xF0) != 0xF0) {
            ink++;
        }
    }
    return ink;
}

static void test_display_geometry(void) {
    CHECK(epd_width() == 1448);
    CHECK(epd_height() == 1072);
    CHECK(epd_rotated_display_width() == 1072);
    CHECK(epd_rotated_display_height() == 1448);
}

static void test_portrait_pixel_mapping(void) {
    uint8_t *fb = alloc_white_fb();
    if (fb == NULL) {
        return;
    }
    // App-space (0,0) under EPD_ROT_PORTRAIT maps to native (1447, 0):
    // byte 1447/2 = 723, odd native x = high nibble.
    epd_draw_pixel(0, 0, 0x00, fb);
    CHECK(fb[723] == 0x0F);
    // App-space (1,0) maps to native (1447, 1): row stride 724.
    epd_draw_pixel(1, 0, 0x00, fb);
    CHECK(fb[724 + 723] == 0x0F);
    free(fb);
}

static void test_fill_rect_ink_count(void) {
    uint8_t *fb = alloc_white_fb();
    if (fb == NULL) {
        return;
    }
    EpdRect r = { .x = 10, .y = 20, .width = 3, .height = 2 };
    epd_fill_rect(r, 0x00, fb);
    CHECK(count_ink_nibbles(fb) == 6);
    free(fb);
}

static void test_text_bounds(void) {
    EpdFontProperties props = epd_font_properties_default();
    int x = 50;
    int y = 100;
    int x1 = 0;
    int y1 = 0;
    int w = 0;
    int h = 0;
    epd_get_text_bounds(&FiraSans_12, "Hello", &x, &y, &x1, &y1, &w, &h, &props);
    CHECK(w > 0);
    CHECK(h > 0);
}

static void test_write_string_draws_ink(void) {
    uint8_t *fb = alloc_white_fb();
    if (fb == NULL) {
        return;
    }
    EpdFontProperties props = epd_font_properties_default();
    int x = 50;
    int y = 100;
    enum EpdDrawError err = epd_write_string(&FiraSans_12, "Ag", &x, &y, fb, &props);
    CHECK(err == EPD_DRAW_SUCCESS);
    CHECK(x > 50);
    CHECK(count_ink_nibbles(fb) > 0);
    free(fb);
}

static void test_highlevel_framebuffer(void) {
    EpdiyHighlevelState hl = epd_hl_init(EPD_BUILTIN_WAVEFORM);
    uint8_t *fb = epd_hl_get_framebuffer(&hl);
    CHECK(fb != NULL);
    epd_draw_pixel(0, 0, 0x00, fb);
    epd_hl_set_all_white(&hl);
    CHECK(fb[723] == 0xFF);
    free(hl.front_fb);
}

static void test_png_writer_roundtrip(void) {
    const char *path = "harness_test_out.png";
    uint8_t pixels[4 * 2] = { 0, 85, 170, 255, 255, 170, 85, 0 };
    CHECK(png_write_gray8(path, pixels, 4, 2) == 0);

    FILE *f = fopen(path, "rb");
    CHECK(f != NULL);
    if (f) {
        uint8_t header[24] = { 0 };
        CHECK(fread(header, 1, 24, f) == 24);
        const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
        CHECK(memcmp(header, sig, 8) == 0);
        uint32_t w = ((uint32_t)header[16] << 24) | ((uint32_t)header[17] << 16)
            | ((uint32_t)header[18] << 8) | (uint32_t)header[19];
        uint32_t h = ((uint32_t)header[20] << 24) | ((uint32_t)header[21] << 16)
            | ((uint32_t)header[22] << 8) | (uint32_t)header[23];
        CHECK(w == 4);
        CHECK(h == 2);
        fclose(f);
        remove(path);
    }
}

int main(void) {
    CHECK(DISPLAY_VCOM_MV == 1560);

    epd_init(&epd_board_epdiy2_s3, &ED060KD1, EPD_LUT_64K);
    epd_set_rotation(EPD_ROT_PORTRAIT);

    test_display_geometry();
    test_portrait_pixel_mapping();
    test_fill_rect_ink_count();
    test_text_bounds();
    test_write_string_draws_ink();
    test_highlevel_framebuffer();
    test_png_writer_roundtrip();

    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("all checks passed\n");
    return 0;
}
