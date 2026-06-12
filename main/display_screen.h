#pragma once

#include <epdiy.h>

#include "display_font.h"
#include "sd_content.h"

// Initializes the display stack (epd_init, VCOM, rotation, viewport) and
// returns the highlevel state. Shared render entry: main.c and the host
// harness must use this instead of calling epd_* init functions directly.
EpdiyHighlevelState display_render_init(void);

// Powers the panel, flushes the framebuffer to the screen, powers off.
// On the host harness this writes the rendered PNG instead.
enum EpdDrawError display_present(EpdiyHighlevelState *hl);

void display_clear_screen(void);
void display_draw_sd_screen(
    EpdiyHighlevelState *hl,
    const SdDisplayContent *content,
    DisplayFont *font
);
