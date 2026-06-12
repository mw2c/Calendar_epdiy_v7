#include <dirent.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <epdiy.h>

#include "app_config.h"
#include "display_font.h"
#include "display_screen.h"
#include "display_viewport.h"
#include "sd_content.h"

#include "epd_stub.h"

#define HARNESS_DEFAULT_SDCARD "harness/assets/sdcard"
#define HARNESS_DEFAULT_OUT "harness/out/render.png"

static bool has_suffix_ci(const char *name, const char *suffix) {
    size_t name_len = strlen(name);
    size_t suffix_len = strlen(suffix);
    if (name_len < suffix_len) {
        return false;
    }
    return strcasecmp(name + name_len - suffix_len, suffix) == 0;
}

// Picks the lexicographically first matching entry for deterministic output.
static bool find_first_entry(
    const char *dir, const char *const *suffixes, char *out, size_t out_size
) {
    DIR *d = opendir(dir);
    if (d == NULL) {
        return false;
    }
    char best[512] = "";
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') {
            continue;
        }
        bool match = false;
        for (const char *const *s = suffixes; *s != NULL; s++) {
            if (has_suffix_ci(ent->d_name, *s)) {
                match = true;
                break;
            }
        }
        if (!match) {
            continue;
        }
        if (best[0] == '\0' || strcmp(ent->d_name, best) < 0) {
            snprintf(best, sizeof(best), "%s", ent->d_name);
        }
    }
    closedir(d);
    if (best[0] == '\0') {
        return false;
    }
    snprintf(out, out_size, "%s/%s", dir, best);
    return true;
}

static void load_text_content(SdDisplayContent *content, const char *sdcard_dir) {
    memset(content, 0, sizeof(*content));

    char txt_path[512];
    static const char *const txt_suffixes[] = { ".txt", NULL };
    if (!find_first_entry(sdcard_dir, txt_suffixes, txt_path, sizeof(txt_path))) {
        snprintf(
            content->text,
            sizeof(content->text),
            "harness: no .txt found in %s\nThis is fallback content.\n",
            sdcard_dir
        );
        content->len = strlen(content->text);
        return;
    }

    snprintf(content->first_txt_path, sizeof(content->first_txt_path), "%s", txt_path);
    FILE *f = fopen(txt_path, "rb");
    if (f == NULL) {
        snprintf(content->text, sizeof(content->text), "harness: cannot open %s\n", txt_path);
        content->len = strlen(content->text);
        return;
    }

    size_t max_bytes = SD_MAX_TXT_BYTES;
    if (max_bytes >= sizeof(content->text)) {
        max_bytes = sizeof(content->text) - 1;
    }
    size_t n = fread(content->text, 1, max_bytes, f);
    content->text[n] = '\0';
    content->len = n;
    content->txt_found = true;
    content->txt_truncated = fgetc(f) != EOF;
    fclose(f);
}

static void find_font(SdDisplayContent *content, const char *sdcard_dir) {
    char configured[512];
    snprintf(configured, sizeof(configured), "%s/%s", sdcard_dir, SD_DEFAULT_FONT_PATH);

    char found[512] = "";
    struct stat st;
    if (stat(configured, &st) == 0 && S_ISREG(st.st_mode)) {
        snprintf(found, sizeof(found), "%s", configured);
    } else {
        char fonts_dir[512];
        snprintf(fonts_dir, sizeof(fonts_dir), "%s/fonts", sdcard_dir);
        static const char *const font_suffixes[] = { ".ttf", ".otf", NULL };
        if (!find_first_entry(fonts_dir, font_suffixes, found, sizeof(found))) {
            return;
        }
    }

    snprintf(content->font_path, sizeof(content->font_path), "%s", found);
    const char *base = strrchr(found, '/');
    base = base != NULL ? base + 1 : found;
    snprintf(content->font_name, sizeof(content->font_name), "%s", base);
    char *dot = strrchr(content->font_name, '.');
    if (dot != NULL && dot != content->font_name) {
        *dot = '\0';
    }
    content->font_found = true;
}

// Best effort: creates the immediate parent directory of path.
static void ensure_parent_dir(const char *path) {
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    char *slash = strrchr(tmp, '/');
    if (slash == NULL || slash == tmp) {
        return;
    }
    *slash = '\0';
    mkdir(tmp, 0755);
}

int main(int argc, char **argv) {
    const char *sdcard_dir = HARNESS_DEFAULT_SDCARD;
    const char *out_path = HARNESS_DEFAULT_OUT;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--sdcard") == 0 && i + 1 < argc) {
            sdcard_dir = argv[++i];
        } else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            out_path = argv[++i];
        } else {
            fprintf(stderr, "usage: %s [--sdcard <dir>] [--out <png>]\n", argv[0]);
            return 2;
        }
    }

    epd_stub_set_output_path(out_path);
    ensure_parent_dir(out_path);

    // Same init sequence as main.c on the device.
    epd_init(&DISPLAY_BOARD, &DISPLAY_MODEL, EPD_LUT_64K);
    epd_set_vcom(DISPLAY_VCOM_MV);
    epd_set_rotation(EPD_ROT_PORTRAIT);
    viewport_init();

    EpdiyHighlevelState hl = epd_hl_init(EPD_BUILTIN_WAVEFORM);

    printf(
        "display: %dx%d, viewport: %dx%d at %d,%d\n",
        epd_rotated_display_width(),
        epd_rotated_display_height(),
        viewport_width(),
        viewport_height(),
        viewport_screen_x(0),
        viewport_screen_y(0)
    );

    static SdDisplayContent content;
    load_text_content(&content, sdcard_dir);
    find_font(&content, sdcard_dir);

    static DisplayFont font;
    bool font_loaded = false;
    if (content.font_found) {
        font_loaded = display_font_init(
            &font, content.font_path, content.font_name, DISPLAY_SD_FONT_POINT_SIZE
        );
        if (!font_loaded) {
            fprintf(stderr, "harness: font init failed: %s\n", content.font_path);
        }
    } else {
        fprintf(stderr, "harness: no font under %s; using built-in font\n", sdcard_dir);
    }

    display_clear_screen();
    display_draw_sd_screen(&hl, &content, font_loaded ? &font : NULL);

    epd_poweron();
    enum EpdDrawError err = epd_hl_update_screen(&hl, MODE_GC16, (int)epd_ambient_temperature());
    epd_poweroff();

    if (font_loaded) {
        display_font_deinit(&font);
    }
    free(hl.front_fb);

    if (err != EPD_DRAW_SUCCESS) {
        fprintf(stderr, "harness: render failed: %d\n", err);
        return 1;
    }
    printf(
        "rendered: %s (%dx%d)\n",
        out_path,
        epd_rotated_display_width(),
        epd_rotated_display_height()
    );
    return 0;
}
