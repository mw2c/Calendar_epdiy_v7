#pragma once

#include <epdiy.h>

#include "display_font.h"
#include "sd_content.h"

void display_clear_screen(void);
void display_draw_sd_screen(
    EpdiyHighlevelState *hl,
    const SdDisplayContent *content,
    DisplayFont *font
);
