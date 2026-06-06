#include "sd_content.h"

#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include <driver/sdspi_host.h>
#include <driver/spi_common.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>

#include "text_utils.h"

static const char *TAG = "sd_content";

static void append_text(SdDisplayContent *content, const char *text) {
    if (content->len >= sizeof(content->text) - 1) {
        return;
    }

    size_t remaining = sizeof(content->text) - content->len - 1;
    int written = snprintf(content->text + content->len, remaining + 1, "%s", text);
    if (written < 0) {
        return;
    }

    if ((size_t)written > remaining) {
        content->len = sizeof(content->text) - 1;
    } else {
        content->len += written;
    }
}

static void append_format(SdDisplayContent *content, const char *format, ...) {
    if (content->len >= sizeof(content->text) - 1) {
        return;
    }

    va_list args;
    va_start(args, format);
    size_t remaining = sizeof(content->text) - content->len - 1;
    int written = vsnprintf(content->text + content->len, remaining + 1, format, args);
    va_end(args);

    if (written < 0) {
        return;
    }

    if ((size_t)written > remaining) {
        content->len = sizeof(content->text) - 1;
    } else {
        content->len += written;
    }
}

static void copy_string(char *dest, size_t dest_size, const char *src) {
    if (dest_size == 0) {
        return;
    }

    size_t i = 0;
    while (i + 1 < dest_size && src[i] != '\0') {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

static bool has_txt_extension(const char *name) {
    const char *dot = strrchr(name, '.');
    if (dot == NULL) {
        return false;
    }

    return strcasecmp(dot, ".txt") == 0;
}

static void append_file_size(SdDisplayContent *content, const struct stat *st) {
    if (S_ISDIR(st->st_mode)) {
        append_text(content, "/");
    } else {
        append_format(content, " (%ld B)", (long)st->st_size);
    }
}

static void remember_first_txt(SdDisplayContent *content, const char *path) {
    if (content->txt_found) {
        return;
    }

    copy_string(content->first_txt_path, sizeof(content->first_txt_path), path);
    content->txt_found = true;
}

static void append_tree_entry(
    SdDisplayContent *content,
    const char *display_name,
    const struct stat *st,
    int depth
) {
    if (content->tree_entries >= SD_MAX_TREE_ENTRIES) {
        content->tree_truncated = true;
        return;
    }

    for (int i = 0; i < depth; i++) {
        append_text(content, "  ");
    }
    append_text(content, "- ");
    append_text(content, display_name);
    append_file_size(content, st);
    append_text(content, "\n");
    content->tree_entries++;
}

static bool resolve_sd_path(char *dest, size_t dest_size, const char *path) {
    if (dest_size == 0) {
        return false;
    }
    dest[0] = '\0';

    if (path == NULL || path[0] == '\0') {
        return false;
    }

    if (path[0] == '/') {
        if (strlen(path) >= dest_size) {
            return false;
        }
        copy_string(dest, dest_size, path);
        return true;
    }

    int written = snprintf(dest, dest_size, "%s/%s", SD_MOUNT_POINT, path);
    if (written < 0 || written >= (int)dest_size) {
        dest[0] = '\0';
        return false;
    }

    return true;
}

static void remember_configured_font(SdDisplayContent *content, const char *font_path) {
    char resolved_path[SD_FONT_PATH_SIZE];
    if (!resolve_sd_path(resolved_path, sizeof(resolved_path), font_path)) {
        return;
    }

    copy_string(content->font_path, sizeof(content->font_path), resolved_path);

    const char *name = strrchr(resolved_path, '/');
    name = name == NULL ? resolved_path : name + 1;
    text_copy_sanitized(content->font_name, sizeof(content->font_name), name);

    struct stat st;
    content->font_found = stat(resolved_path, &st) == 0 && !S_ISDIR(st.st_mode);
}

static void append_font_status(SdDisplayContent *content) {
    if (content->font_path[0] == '\0') {
        append_text(content, "\nFont: no configured font path\n");
        return;
    }

    if (content->font_found) {
        append_format(content, "\nFont: %s\n", content->font_name);
    } else {
        append_format(content, "\nFont missing: %s\n", content->font_path);
    }
}

static void read_directory_tree(SdDisplayContent *content, const char *path, int depth) {
    if (depth > SD_MAX_TREE_DEPTH || content->tree_entries >= SD_MAX_TREE_ENTRIES) {
        content->tree_truncated = true;
        return;
    }

    DIR *dir = opendir(path);
    if (dir == NULL) {
        append_format(content, "Cannot open %s: %s\n", path, strerror(errno));
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        if (content->tree_entries >= SD_MAX_TREE_ENTRIES) {
            content->tree_truncated = true;
            break;
        }

        char child_path[SD_FONT_PATH_SIZE];
        int path_len = snprintf(child_path, sizeof(child_path), "%s/%s", path, entry->d_name);
        if (path_len < 0 || path_len >= (int)sizeof(child_path)) {
            content->tree_truncated = true;
            continue;
        }

        char display_name[96];
        text_copy_sanitized(display_name, sizeof(display_name), entry->d_name);

        struct stat st;
        if (stat(child_path, &st) != 0) {
            append_format(content, "- %s [?]\n", display_name);
            continue;
        }

        append_tree_entry(content, display_name, &st, depth);

        if (depth == 0 && !S_ISDIR(st.st_mode) && has_txt_extension(entry->d_name)) {
            remember_first_txt(content, child_path);
        }

        if (S_ISDIR(st.st_mode) && depth < SD_MAX_TREE_DEPTH) {
            read_directory_tree(content, child_path, depth + 1);
        }
    }

    closedir(dir);
}

static void read_first_txt(SdDisplayContent *content) {
    if (!content->txt_found) {
        append_text(content, "\nFirst root .txt:\nNo .txt file found in root.\n");
        return;
    }

    const char *filename = strrchr(content->first_txt_path, '/');
    filename = filename == NULL ? content->first_txt_path : filename + 1;

    char display_name[96];
    text_copy_sanitized(display_name, sizeof(display_name), filename);
    append_format(content, "\nFirst root .txt: %s\n", display_name);

    FILE *file = fopen(content->first_txt_path, "r");
    if (file == NULL) {
        append_format(content, "Cannot open file: %s\n", strerror(errno));
        return;
    }

    int read_count = 0;
    int ch;
    while ((ch = fgetc(file)) != EOF) {
        if (read_count >= SD_MAX_TXT_BYTES) {
            content->txt_truncated = true;
            break;
        }

        unsigned char byte = (unsigned char)ch;
        if (byte < 0x20 && byte != '\n' && byte != '\r' && byte != '\t') {
            byte = '?';
        }
        char text[2] = { (char)byte, '\0' };
        append_text(content, text);
        read_count++;

        if (content->len >= sizeof(content->text) - 2) {
            content->txt_truncated = true;
            break;
        }
    }

    fclose(file);

    if (content->txt_truncated) {
        append_text(content, "\n...[txt truncated]\n");
    }
}

void sd_fill_mount_error(SdDisplayContent *content, esp_err_t err) {
    memset(content, 0, sizeof(*content));
    append_format(content, "Mount failed: %s\n", esp_err_to_name(err));
    append_text(content, "\nCheck wiring, card format, and pull-ups.\n");
}

esp_err_t sd_mount_card(SdCardHandle *sd) {
    memset(sd, 0, sizeof(*sd));

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.max_freq_khz = 10000;
    sd->host_id = host.slot;

    spi_bus_config_t bus_config = {
        .mosi_io_num = SD_PIN_MOSI,
        .miso_io_num = SD_PIN_MISO,
        .sclk_io_num = SD_PIN_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = SD_MAX_TRANSFER_SIZE,
    };

    ESP_LOGI(
        TAG,
        "initializing SD SPI bus: MISO=%d CLK=%d MOSI=%d CS=%d",
        SD_PIN_MISO,
        SD_PIN_CLK,
        SD_PIN_MOSI,
        SD_PIN_CS
    );

    esp_err_t err = spi_bus_initialize(host.slot, &bus_config, SDSPI_DEFAULT_DMA);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to initialize SD SPI bus: %s", esp_err_to_name(err));
        return err;
    }
    sd->bus_initialized = true;

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = SD_PIN_CS;
    slot_config.host_id = host.slot;

    err = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot_config, &mount_config, &sd->card);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to mount SD card: %s", esp_err_to_name(err));
        spi_bus_free(host.slot);
        sd->bus_initialized = false;
        return err;
    }

    ESP_LOGI(TAG, "SD card mounted");
    sdmmc_card_print_info(stdout, sd->card);
    return ESP_OK;
}

void sd_unmount_card(SdCardHandle *sd) {
    if (sd->card != NULL) {
        esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, sd->card);
        sd->card = NULL;
    }

    if (sd->bus_initialized) {
        spi_bus_free(sd->host_id);
        sd->bus_initialized = false;
    }
}

void sd_load_display_content(SdDisplayContent *content, const char *font_path) {
    memset(content, 0, sizeof(*content));
    remember_configured_font(content, font_path);

    append_text(content, "Directory tree:\n");
    read_directory_tree(content, SD_MOUNT_POINT, 0);
    if (content->tree_entries == 0) {
        append_text(content, "(root is empty)\n");
    }
    if (content->tree_truncated) {
        append_text(content, "...[tree truncated]\n");
    }

    read_first_txt(content);
    append_font_status(content);
}
