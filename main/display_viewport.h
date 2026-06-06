#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <epdiy.h>

#include "display_font.h"

typedef struct {
    int x;
    int y;
    int width;
    int height;
} DisplayViewport;

void viewport_init(void);
const DisplayViewport *viewport_get(void);

int viewport_width(void);
int viewport_height(void);
int viewport_screen_x(int x);
int viewport_screen_y(int y);
bool viewport_contains_point(int x, int y);

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
);

enum EpdDrawError viewport_write_string(
    const EpdFont *font,
    const char *text,
    int *cursor_x,
    int *cursor_y,
    uint8_t *fb,
    EpdFontProperties *props
);

enum EpdDrawError viewport_draw_text(
    DisplayFont *font,
    uint8_t *fb,
    const char *text,
    int *cursor_x,
    int *cursor_y,
    const EpdFontProperties *props
);

void viewport_fill_rect(EpdRect rect, uint8_t color, uint8_t *fb);
void viewport_draw_pixel(int x, int y, uint8_t color, uint8_t *fb);
