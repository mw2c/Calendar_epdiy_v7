#include "display_viewport.h"

#include <esp_log.h>

#include "app_config.h"

static const char *TAG = "display_viewport";
static DisplayViewport s_viewport;
static bool s_viewport_initialized;

static int clamp_nonnegative(int value) {
    return value < 0 ? 0 : value;
}

void viewport_init(void) {
    int screen_width = epd_rotated_display_width();
    int screen_height = epd_rotated_display_height();
    int left = clamp_nonnegative(DISPLAY_OCCLUDE_LEFT);
    int top = clamp_nonnegative(DISPLAY_OCCLUDE_TOP);
    int right = clamp_nonnegative(DISPLAY_OCCLUDE_RIGHT);
    int bottom = clamp_nonnegative(DISPLAY_OCCLUDE_BOTTOM);

    if (left + right >= screen_width) {
        ESP_LOGW(TAG, "invalid horizontal occlusion; falling back to full width");
        left = 0;
        right = 0;
    }
    if (top + bottom >= screen_height) {
        ESP_LOGW(TAG, "invalid vertical occlusion; falling back to full height");
        top = 0;
        bottom = 0;
    }

    s_viewport.x = left;
    s_viewport.y = top;
    s_viewport.width = screen_width - left - right;
    s_viewport.height = screen_height - top - bottom;
    s_viewport_initialized = true;
}

const DisplayViewport *viewport_get(void) {
    if (!s_viewport_initialized) {
        viewport_init();
    }
    return &s_viewport;
}

int viewport_width(void) {
    return viewport_get()->width;
}

int viewport_height(void) {
    return viewport_get()->height;
}

int viewport_screen_x(int x) {
    return viewport_get()->x + x;
}

int viewport_screen_y(int y) {
    return viewport_get()->y + y;
}

bool viewport_contains_point(int x, int y) {
    const DisplayViewport *viewport = viewport_get();
    return x >= 0 && y >= 0 && x < viewport->width && y < viewport->height;
}

void viewport_get_text_bounds(
    const EpdFont *font,
    const char *string,
    const int *x,
    const int *y,
    int *x1,
    int *y1,
    int *w,
    int *h,
    const EpdFontProperties *props
) {
    const DisplayViewport *viewport = viewport_get();
    int screen_x = viewport->x + *x;
    int screen_y = viewport->y + *y;
    int screen_x1 = 0;
    int screen_y1 = 0;

    epd_get_text_bounds(font, string, &screen_x, &screen_y, &screen_x1, &screen_y1, w, h, props);

    *x1 = screen_x1 - viewport->x;
    *y1 = screen_y1 - viewport->y;
}

enum EpdDrawError viewport_write_string(
    const EpdFont *font,
    const char *text,
    int *cursor_x,
    int *cursor_y,
    uint8_t *fb,
    EpdFontProperties *props
) {
    if (cursor_x == NULL || cursor_y == NULL) {
        return EPD_DRAW_STRING_INVALID;
    }

    const DisplayViewport *viewport = viewport_get();
    int screen_x = viewport->x + *cursor_x;
    int screen_y = viewport->y + *cursor_y;
    enum EpdDrawError err = epd_write_string(font, text, &screen_x, &screen_y, fb, props);

    *cursor_x = screen_x - viewport->x;
    *cursor_y = screen_y - viewport->y;
    return err;
}

enum EpdDrawError viewport_draw_text(
    DisplayFont *font,
    uint8_t *fb,
    const char *text,
    int *cursor_x,
    int *cursor_y,
    const EpdFontProperties *props
) {
    if (cursor_x == NULL || cursor_y == NULL) {
        return EPD_DRAW_STRING_INVALID;
    }

    const DisplayViewport *viewport = viewport_get();
    int screen_x = viewport->x + *cursor_x;
    int screen_y = viewport->y + *cursor_y;
    enum EpdDrawError err = display_font_draw_text(font, fb, text, &screen_x, &screen_y, props);

    *cursor_x = screen_x - viewport->x;
    *cursor_y = screen_y - viewport->y;
    return err;
}

void viewport_fill_rect(EpdRect rect, uint8_t color, uint8_t *fb) {
    const DisplayViewport *viewport = viewport_get();
    int x0 = rect.x < 0 ? 0 : rect.x;
    int y0 = rect.y < 0 ? 0 : rect.y;
    int x1 = rect.x + rect.width;
    int y1 = rect.y + rect.height;

    if (x1 > viewport->width) {
        x1 = viewport->width;
    }
    if (y1 > viewport->height) {
        y1 = viewport->height;
    }
    if (x0 >= x1 || y0 >= y1) {
        return;
    }

    EpdRect screen_rect = {
        .x = viewport->x + x0,
        .y = viewport->y + y0,
        .width = x1 - x0,
        .height = y1 - y0,
    };
    epd_fill_rect(screen_rect, color, fb);
}

void viewport_draw_pixel(int x, int y, uint8_t color, uint8_t *fb) {
    if (!viewport_contains_point(x, y)) {
        return;
    }

    const DisplayViewport *viewport = viewport_get();
    epd_draw_pixel(viewport->x + x, viewport->y + y, color, fb);
}
