#pragma once

#include <stdbool.h>
#include <stddef.h>

#include <esp_err.h>
#include <sdmmc_cmd.h>

#include "app_config.h"

typedef struct {
    char text[SD_TEXT_BUFFER_SIZE];
    size_t len;
    char first_txt_path[SD_FIRST_TXT_PATH_SIZE];
    char font_path[SD_FONT_PATH_SIZE];
    char font_name[SD_FONT_NAME_SIZE];
    bool tree_truncated;
    bool txt_truncated;
    bool txt_found;
    bool font_found;
    int tree_entries;
} SdDisplayContent;

typedef struct {
    sdmmc_card_t *card;
    int host_id;
    bool bus_initialized;
} SdCardHandle;

esp_err_t sd_mount_card(SdCardHandle *sd);
void sd_unmount_card(SdCardHandle *sd);
void sd_fill_mount_error(SdDisplayContent *content, esp_err_t err);
void sd_load_display_content(SdDisplayContent *content, const char *font_path);
