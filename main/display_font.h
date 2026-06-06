#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <epdiy.h>

#include "app_config.h"

typedef struct DisplayFontImpl DisplayFontImpl;

typedef struct {
    bool freetype_compiled;
    bool active;
    char font_path[SD_FONT_PATH_SIZE];
    char font_name[SD_FONT_NAME_SIZE];
    int point_size;
    int ascent;
    int descent;
    int line_height;
    DisplayFontImpl *impl;
} DisplayFont;

bool display_font_init(DisplayFont *font, const char *path, const char *name, int point_size);
void display_font_deinit(DisplayFont *font);
int display_font_line_height(const DisplayFont *font);
int display_font_text_width(DisplayFont *font, const char *text);
enum EpdDrawError display_font_draw_text(
    DisplayFont *font,
    uint8_t *fb,
    const char *text,
    int *cursor_x,
    int *cursor_y,
    const EpdFontProperties *props
);
