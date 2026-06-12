#include "display_screen.h"

#include <stdio.h>
#include <string.h>

#include <esp_log.h>

#include "app_config.h"
#include "display_viewport.h"
#include "font_test_content.h"
#include "text_utils.h"

// Generated font headers contain bidi codepoint comments.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wbidi-chars"
#include "firasans_12.h"
#include "firasans_20.h"
#pragma GCC diagnostic pop

static const char *TAG = "display_screen";

static size_t utf8_char_len(const char *text) {
    unsigned char first = (unsigned char)text[0];
    if (first < 0x80) {
        return 1;
    }
    if ((first & 0xE0) == 0xC0) {
        return 2;
    }
    if ((first & 0xF0) == 0xE0) {
        return 3;
    }
    if ((first & 0xF8) == 0xF0) {
        return 4;
    }
    return 1;
}

static void draw_wrapped_line(
    const EpdFont *font,
    uint8_t *fb,
    EpdFontProperties *props,
    const char *line,
    int *cursor_y,
    int x,
    int max_y
) {
    const int advance_y = font->advance_y;
    const int max_width = viewport_width() - (DISPLAY_MARGIN_X * 2);
    char part[96];
    size_t part_len = 0;
    int current_width = 0;

    for (const char *p = line; *p != '\0'; p++) {
        char ch = text_printable_char(*p);
        if (ch == '\r') {
            continue;
        }
        if (ch == '\t') {
            ch = ' ';
        }

        char tmp[2] = { ch, '\0' };
        int tx = 0;
        int ty = *cursor_y;
        int x1 = 0;
        int y1 = 0;
        int w = 0;
        int h = 0;
        viewport_get_text_bounds(font, tmp, &tx, &ty, &x1, &y1, &w, &h, props);

        if (part_len > 0 && current_width + w > max_width) {
            int draw_x = x;
            viewport_write_string(font, part, &draw_x, cursor_y, fb, props);
            if (*cursor_y + advance_y > max_y) {
                return;
            }
            part_len = 0;
            current_width = 0;
        }

        if (part_len < sizeof(part) - 1) {
            part[part_len++] = ch;
            part[part_len] = '\0';
            current_width += w;
        }
    }

    if (part_len > 0 && *cursor_y <= max_y) {
        int draw_x = x;
        viewport_write_string(font, part, &draw_x, cursor_y, fb, props);
    } else {
        *cursor_y += advance_y;
    }
}

static void draw_wrapped_line_ft(
    DisplayFont *font,
    uint8_t *fb,
    EpdFontProperties *props,
    const char *line,
    int *cursor_y,
    int x,
    int max_y
) {
    const int advance_y = display_font_line_height(font);
    const int max_width = viewport_width() - (DISPLAY_MARGIN_X * 2);
    char part[192];
    size_t part_len = 0;
    int current_width = 0;

    const char *line_end = line + strlen(line);
    for (const char *p = line; *p != '\0';) {
        size_t len = utf8_char_len(p);
        if (p + len > line_end) {
            len = 1;
        }

        char tmp[8];
        if (len >= sizeof(tmp)) {
            len = 1;
        }
        memcpy(tmp, p, len);
        tmp[len] = '\0';

        if (tmp[0] == '\r') {
            p += len;
            continue;
        }
        if (tmp[0] == '\t') {
            tmp[0] = ' ';
            tmp[1] = '\0';
            len = 1;
        }

        int w = display_font_text_width(font, tmp);
        if (part_len > 0 && current_width + w > max_width) {
            int draw_x = x;
            viewport_draw_text(font, fb, part, &draw_x, cursor_y, props);
            *cursor_y += advance_y;
            if (*cursor_y > max_y) {
                return;
            }
            part_len = 0;
            current_width = 0;
        }

        if (part_len + len < sizeof(part)) {
            memcpy(part + part_len, tmp, len);
            part_len += len;
            part[part_len] = '\0';
            current_width += w;
        }

        p += len;
    }

    if (part_len > 0 && *cursor_y <= max_y) {
        int draw_x = x;
        viewport_draw_text(font, fb, part, &draw_x, cursor_y, props);
        *cursor_y += advance_y;
    } else {
        *cursor_y += advance_y;
    }
}

static void draw_font_test_line(
    DisplayFont *font,
    uint8_t *fb,
    EpdFontProperties *props,
    int *cursor_y,
    int max_y
) {
    if (font == NULL || !font->active || *cursor_y > max_y) {
        return;
    }

    char line[192];
    snprintf(line, sizeof(line), "%s: " DISPLAY_FONT_TEST_SAMPLE, font->font_name);
    draw_wrapped_line_ft(font, fb, props, line, cursor_y, DISPLAY_MARGIN_X, max_y);
    *cursor_y += 4;
}

EpdiyHighlevelState display_render_init(void) {
    epd_init(&DISPLAY_BOARD, &DISPLAY_MODEL, EPD_LUT_64K);
    epd_set_vcom(DISPLAY_VCOM_MV);
    epd_set_rotation(EPD_ROT_PORTRAIT);
    viewport_init();

    EpdiyHighlevelState hl = epd_hl_init(EPD_BUILTIN_WAVEFORM);

    ESP_LOGI(
        TAG,
        "display initialized: %dx%d, viewport: %dx%d at %d,%d",
        epd_rotated_display_width(),
        epd_rotated_display_height(),
        viewport_width(),
        viewport_height(),
        viewport_screen_x(0),
        viewport_screen_y(0)
    );
    return hl;
}

enum EpdDrawError display_present(EpdiyHighlevelState *hl) {
    epd_poweron();
    enum EpdDrawError err = epd_hl_update_screen(hl, MODE_GC16, (int)epd_ambient_temperature());
    epd_poweroff();
    return err;
}

void display_clear_screen(void) {
    ESP_LOGI(TAG, "clearing display before first draw");

    epd_poweron();
    epd_clear();
    epd_poweroff();
}

void display_draw_sd_screen(
    EpdiyHighlevelState *hl,
    const SdDisplayContent *content,
    DisplayFont *font
) {
    uint8_t *fb = epd_hl_get_framebuffer(hl);
    epd_hl_set_all_white(hl);
    DisplayFont *body_font = font != NULL && font->active ? font : NULL;

    EpdFontProperties title_props = epd_font_properties_default();
    title_props.flags = EPD_DRAW_ALIGN_LEFT;
    int cursor_x = DISPLAY_MARGIN_X;
    int cursor_y = DISPLAY_MARGIN_Y + FiraSans_20.ascender;
    viewport_write_string(&FiraSans_20, "SD Card", &cursor_x, &cursor_y, fb, &title_props);

    if (body_font != NULL && body_font->active) {
        cursor_x = DISPLAY_MARGIN_X + 150;
        int font_y = DISPLAY_MARGIN_Y + FiraSans_12.ascender;
        viewport_write_string(&FiraSans_12, "Font", &cursor_x, &font_y, fb, &title_props);
    }

    EpdFontProperties body_props = epd_font_properties_default();
    body_props.flags = EPD_DRAW_ALIGN_LEFT;
    body_props.fallback_glyph = '?';

    cursor_y += 18;
    const int max_y = viewport_height() - DISPLAY_MARGIN_Y;

    draw_font_test_line(body_font, fb, &body_props, &cursor_y, max_y);
    if (body_font != NULL) {
        cursor_y += 8;
    }

    const char *line_start = content->text;

    while (*line_start != '\0' && cursor_y <= max_y) {
        const char *line_end = strchr(line_start, '\n');
        size_t line_len = line_end == NULL ? strlen(line_start) : (size_t)(line_end - line_start);

        char line[160];
        if (line_len >= sizeof(line)) {
            line_len = sizeof(line) - 1;
        }
        memcpy(line, line_start, line_len);
        line[line_len] = '\0';

        if (body_font != NULL && body_font->active) {
            draw_wrapped_line_ft(
                body_font,
                fb,
                &body_props,
                line,
                &cursor_y,
                DISPLAY_MARGIN_X,
                max_y
            );
        } else {
            draw_wrapped_line(
                &FiraSans_12,
                fb,
                &body_props,
                line,
                &cursor_y,
                DISPLAY_MARGIN_X,
                max_y
            );
        }

        if (line_end == NULL) {
            break;
        }
        line_start = line_end + 1;
    }
}
