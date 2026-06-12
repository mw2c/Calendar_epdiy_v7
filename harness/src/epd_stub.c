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

// ---- UTF-8 decoding (transcribed from font.c) ------------------------------

typedef struct {
    uint8_t mask;
    uint8_t lead;
    uint32_t beg;
    uint32_t end;
    int bits_stored;
} utf_t;

static utf_t *utf[] = {
    [0] = &(utf_t){ 0b00111111, 0b10000000, 0, 0, 6 },
    [1] = &(utf_t){ 0b01111111, 0b00000000, 0000, 0177, 7 },
    [2] = &(utf_t){ 0b00011111, 0b11000000, 0200, 03777, 5 },
    [3] = &(utf_t){ 0b00001111, 0b11100000, 04000, 0177777, 4 },
    [4] = &(utf_t){ 0b00000111, 0b11110000, 0200000, 04177777, 3 },
    &(utf_t){ 0 },
};

static int utf8_len(const uint8_t ch) {
    int len = 0;
    for (utf_t **u = utf; (*u)->mask; ++u) {
        if ((ch & ~(*u)->mask) == (*u)->lead) {
            break;
        }
        ++len;
    }
    assert(len <= 4);
    return len;
}

static uint32_t next_cp(const uint8_t **string) {
    if (**string == 0) {
        return 0;
    }
    int bytes = utf8_len(**string);
    const uint8_t *chr = *string;
    *string += bytes;
    int shift = utf[0]->bits_stored * (bytes - 1);
    uint32_t codep = (*chr++ & utf[bytes]->mask) << shift;

    for (int i = 1; i < bytes; ++i, ++chr) {
        shift -= utf[0]->bits_stored;
        codep |= ((const uint8_t)*chr & utf[0]->mask) << shift;
    }

    return codep;
}

// ---- font rendering (transcribed from font.c) -------------------------------

EpdFontProperties epd_font_properties_default(void) {
    EpdFontProperties props
        = { .fg_color = 0, .bg_color = 15, .fallback_glyph = 0, .flags = EPD_DRAW_ALIGN_LEFT };
    return props;
}

const EpdGlyph *epd_get_glyph(const EpdFont *font, uint32_t code_point) {
    const EpdUnicodeInterval *intervals = font->intervals;
    for (uint32_t i = 0; i < font->interval_count; i++) {
        const EpdUnicodeInterval *interval = &intervals[i];
        if (code_point >= interval->first && code_point <= interval->last) {
            return &font->glyph[interval->offset + (code_point - interval->first)];
        }
        if (code_point < interval->first) {
            return NULL;
        }
    }
    return NULL;
}

static int stub_uncompress(
    uint8_t *dest, size_t uncompressed_size, const uint8_t *source, size_t source_size
) {
    if (uncompressed_size == 0 || dest == NULL || source_size == 0 || source == NULL) {
        return -1;
    }
    uLongf out_len = uncompressed_size;
    int rc = uncompress(dest, &out_len, source, (uLong)source_size);
    return (rc == Z_OK && out_len == uncompressed_size) ? 0 : -1;
}

static enum EpdDrawError draw_char(
    const EpdFont *font,
    uint8_t *buffer,
    int *cursor_x,
    int cursor_y,
    uint32_t cp,
    const EpdFontProperties *props
) {
    assert(props != NULL);

    const EpdGlyph *glyph = epd_get_glyph(font, cp);
    if (!glyph) {
        glyph = epd_get_glyph(font, props->fallback_glyph);
    }

    if (!glyph) {
        return EPD_DRAW_GLYPH_FALLBACK_FAILED;
    }

    uint32_t offset = glyph->data_offset;
    uint16_t width = glyph->width, height = glyph->height;
    int left = glyph->left;

    int byte_width = (width / 2 + width % 2);
    unsigned long bitmap_size = (unsigned long)byte_width * height;
    const uint8_t *bitmap = NULL;
    if (bitmap_size > 0 && font->compressed) {
        uint8_t *tmp_bitmap = (uint8_t *)malloc(bitmap_size);
        if (tmp_bitmap == NULL) {
            fprintf(stderr, "epd_stub: glyph malloc failed\n");
            return EPD_DRAW_FAILED_ALLOC;
        }
        stub_uncompress(tmp_bitmap, bitmap_size, &font->bitmap[offset], glyph->compressed_size);
        bitmap = tmp_bitmap;
    } else {
        bitmap = &font->bitmap[offset];
    }

    uint8_t color_lut[16];
    for (int c = 0; c < 16; c++) {
        int color_difference = (int)props->fg_color - (int)props->bg_color;
        color_lut[c] = max_int(0, min_int(15, props->bg_color + c * color_difference / 15));
    }
    bool background_needed = props->flags & EPD_DRAW_BACKGROUND;

    for (int y = 0; y < height; y++) {
        int yy = cursor_y - glyph->top + y;
        int start_pos = *cursor_x + left;
        int x = max_int(0, -start_pos);
        int max_x = start_pos + width;

        for (int xx = start_pos; xx < max_x; xx++) {
            uint8_t bm = bitmap[y * byte_width + x / 2];
            if ((x & 1) == 0) {
                bm = bm & 0xF;
            } else {
                bm = bm >> 4;
            }
            if (background_needed || bm) {
                uint8_t color = color_lut[bm] << 4;
                epd_draw_pixel(xx, yy, color, buffer);
            }
            x++;
        }
    }
    if (bitmap_size > 0 && font->compressed) {
        free((uint8_t *)bitmap);
    }
    *cursor_x += glyph->advance_x;
    return EPD_DRAW_SUCCESS;
}

static void get_char_bounds(
    const EpdFont *font,
    uint32_t cp,
    int *x,
    int *y,
    int *minx,
    int *miny,
    int *maxx,
    int *maxy,
    const EpdFontProperties *props
) {
    assert(props != NULL);

    const EpdGlyph *glyph = epd_get_glyph(font, cp);
    if (!glyph) {
        glyph = epd_get_glyph(font, props->fallback_glyph);
    }
    if (!glyph) {
        return;
    }

    int x1 = *x + glyph->left, y1 = *y + glyph->top - glyph->height, x2 = x1 + glyph->width,
        y2 = y1 + glyph->height;

    if (props->flags & EPD_DRAW_BACKGROUND) {
        *minx = min_int(*x, min_int(*minx, x1));
        *maxx = max_int(max_int(*x + glyph->advance_x, x2), *maxx);
        *miny = min_int(*y + font->descender, min_int(*miny, y1));
        *maxy = max_int(*y + font->ascender, max_int(*maxy, y2));
    } else {
        if (x1 < *minx)
            *minx = x1;
        if (y1 < *miny)
            *miny = y1;
        if (x2 > *maxx)
            *maxx = x2;
        if (y2 > *maxy)
            *maxy = y2;
    }
    *x += glyph->advance_x;
}

void epd_get_text_bounds(
    const EpdFont *font,
    const char *string,
    const int *x,
    const int *y,
    int *x1,
    int *y1,
    int *w,
    int *h,
    const EpdFontProperties *properties
) {
    assert(properties != NULL);
    EpdFontProperties props = *properties;

    if (*string == '\0') {
        *w = 0;
        *h = 0;
        *y1 = *y;
        *x1 = *x;
        return;
    }
    int minx = 100000, miny = 100000, maxx = -1, maxy = -1;
    int original_x = *x;
    int temp_x = *x;
    int temp_y = *y;
    uint32_t c;
    while ((c = next_cp((const uint8_t **)&string))) {
        get_char_bounds(font, c, &temp_x, &temp_y, &minx, &miny, &maxx, &maxy, &props);
    }
    *x1 = min_int(original_x, minx);
    *w = maxx - *x1;
    *y1 = miny;
    *h = maxy - miny;
}

static enum EpdDrawError write_text_line(
    const EpdFont *font,
    const char *string,
    int *cursor_x,
    int *cursor_y,
    uint8_t *framebuffer,
    const EpdFontProperties *properties
) {
    assert(framebuffer != NULL);

    if (*string == '\0') {
        return EPD_DRAW_SUCCESS;
    }

    assert(properties != NULL);
    EpdFontProperties props = *properties;
    enum EpdFontFlags alignment_mask
        = EPD_DRAW_ALIGN_LEFT | EPD_DRAW_ALIGN_RIGHT | EPD_DRAW_ALIGN_CENTER;
    enum EpdFontFlags alignment = props.flags & alignment_mask;

    if ((alignment & (alignment - 1)) != 0) {
        return EPD_DRAW_INVALID_FONT_FLAGS;
    }

    int x1 = 0, y1 = 0, w = 0, h = 0;
    int tmp_cur_x = *cursor_x;
    int tmp_cur_y = *cursor_y;
    epd_get_text_bounds(font, string, &tmp_cur_x, &tmp_cur_y, &x1, &y1, &w, &h, &props);

    if (w < 0 || h < 0) {
        return EPD_DRAW_NO_DRAWABLE_CHARACTERS;
    }

    uint8_t *buffer = framebuffer;
    int local_cursor_x = *cursor_x;
    int local_cursor_y = *cursor_y;
    uint32_t c;

    int cursor_x_init = local_cursor_x;
    int cursor_y_init = local_cursor_y;

    switch (alignment) {
        case EPD_DRAW_ALIGN_CENTER:
            local_cursor_x -= w / 2;
            break;
        case EPD_DRAW_ALIGN_RIGHT:
            local_cursor_x -= w;
            break;
        case EPD_DRAW_ALIGN_LEFT:
        default:
            break;
    }

    uint8_t bg = props.bg_color;
    if (props.flags & EPD_DRAW_BACKGROUND) {
        for (int l = local_cursor_y - font->ascender; l < local_cursor_y - font->descender; l++) {
            epd_draw_hline(local_cursor_x, l, w, bg << 4, buffer);
        }
    }
    enum EpdDrawError err = EPD_DRAW_SUCCESS;
    while ((c = next_cp((const uint8_t **)&string))) {
        err |= draw_char(font, buffer, &local_cursor_x, local_cursor_y, c, &props);
    }

    *cursor_x += local_cursor_x - cursor_x_init;
    *cursor_y += local_cursor_y - cursor_y_init;
    return err;
}

enum EpdDrawError epd_write_string(
    const EpdFont *font,
    const char *string,
    int *cursor_x,
    int *cursor_y,
    uint8_t *framebuffer,
    const EpdFontProperties *properties
) {
    char *token, *newstring, *tofree;
    if (string == NULL) {
        fprintf(stderr, "epd_stub: cannot draw a NULL string\n");
        return EPD_DRAW_STRING_INVALID;
    }
    tofree = newstring = strdup(string);
    if (newstring == NULL) {
        fprintf(stderr, "epd_stub: cannot allocate string copy\n");
        return EPD_DRAW_FAILED_ALLOC;
    }

    enum EpdDrawError err = EPD_DRAW_SUCCESS;
    int line_start = *cursor_x;
    while ((token = strsep(&newstring, "\n")) != NULL) {
        *cursor_x = line_start;
        err |= write_text_line(font, token, cursor_x, cursor_y, framebuffer, properties);
        *cursor_y += font->advance_y;
    }

    free(tofree);
    return err;
}

// ---- highlevel API ----------------------------------------------------------

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
            Coord_xy c = _rotate((uint16_t)x, (uint16_t)y);
            uint8_t nibble
                = epd_get_pixel(c.x, c.y, epd_width(), epd_height(), state->front_fb) >> 4;
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
