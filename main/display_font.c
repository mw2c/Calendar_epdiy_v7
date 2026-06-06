#include "display_font.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_memory_utils.h>

static const char *TAG = "display_font";

#ifndef __has_include
#define __has_include(x) 0
#endif

#if __has_include(<ft2build.h>)
#define DISPLAY_FONT_HAS_FREETYPE 1
#include <ft2build.h>
#include FT_FREETYPE_H
#else
#define DISPLAY_FONT_HAS_FREETYPE 0
#endif

#if DISPLAY_FONT_HAS_FREETYPE

#define GLYPH_CACHE_HASH_SIZE 256
#define GLYPH_CACHE_HASH_MASK (GLYPH_CACHE_HASH_SIZE - 1)

typedef struct CachedGlyph {
    uint32_t codepoint;
    int width;
    int height;
    int advance_x;
    int left;
    int top;
    size_t bytes;
    uint8_t *bitmap_4bpp;
    struct CachedGlyph *hash_next;
    struct CachedGlyph *lru_prev;
    struct CachedGlyph *lru_next;
} CachedGlyph;

struct DisplayFontImpl {
    FT_Face face;
    CachedGlyph *hash[GLYPH_CACHE_HASH_SIZE];
    CachedGlyph lru;
    size_t cache_bytes;
    int cache_count;
    struct DisplayFontImpl *next;
};

static FT_Library s_library;
static int s_library_ref_count;
static DisplayFontImpl *s_fonts;
static size_t s_total_cache_bytes;

static uint32_t hash_codepoint(uint32_t codepoint) {
    return (codepoint ^ (codepoint >> 8) ^ (codepoint >> 16)) & GLYPH_CACHE_HASH_MASK;
}

static void *font_malloc(size_t size) {
    void *ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ptr == NULL) {
        ptr = malloc(size);
    }
    return ptr;
}

static bool is_readable_ptr(const void *ptr) {
    return ptr != NULL && esp_ptr_byte_accessible(ptr);
}

static void lru_init(DisplayFontImpl *impl) {
    impl->lru.lru_prev = &impl->lru;
    impl->lru.lru_next = &impl->lru;
}

static void lru_remove(CachedGlyph *glyph) {
    glyph->lru_prev->lru_next = glyph->lru_next;
    glyph->lru_next->lru_prev = glyph->lru_prev;
}

static void lru_push_front(DisplayFontImpl *impl, CachedGlyph *glyph) {
    glyph->lru_prev = &impl->lru;
    glyph->lru_next = impl->lru.lru_next;
    impl->lru.lru_next->lru_prev = glyph;
    impl->lru.lru_next = glyph;
}

static CachedGlyph *cache_lookup(DisplayFontImpl *impl, uint32_t codepoint) {
    CachedGlyph *glyph = impl->hash[hash_codepoint(codepoint)];
    while (glyph != NULL) {
        if (glyph->codepoint == codepoint) {
            lru_remove(glyph);
            lru_push_front(impl, glyph);
            return glyph;
        }
        glyph = glyph->hash_next;
    }
    return NULL;
}

static void cache_evict_one(DisplayFontImpl *impl) {
    CachedGlyph *victim = impl->lru.lru_prev;
    if (victim == &impl->lru) {
        return;
    }

    lru_remove(victim);

    uint32_t hash = hash_codepoint(victim->codepoint);
    CachedGlyph **slot = &impl->hash[hash];
    while (*slot != NULL) {
        if (*slot == victim) {
            *slot = victim->hash_next;
            break;
        }
        slot = &(*slot)->hash_next;
    }

    impl->cache_bytes -= victim->bytes;
    impl->cache_count--;
    if (s_total_cache_bytes >= victim->bytes) {
        s_total_cache_bytes -= victim->bytes;
    } else {
        s_total_cache_bytes = 0;
    }
    heap_caps_free(victim->bitmap_4bpp);
    heap_caps_free(victim);
}

static void cache_clear(DisplayFontImpl *impl) {
    while (impl->cache_count > 0) {
        cache_evict_one(impl);
    }
    memset(impl->hash, 0, sizeof(impl->hash));
    impl->cache_bytes = 0;
    lru_init(impl);
}

static DisplayFontImpl *cache_find_largest(void) {
    DisplayFontImpl *largest = NULL;
    for (DisplayFontImpl *impl = s_fonts; impl != NULL; impl = impl->next) {
        if (impl->cache_count == 0) {
            continue;
        }
        if (largest == NULL || impl->cache_bytes > largest->cache_bytes) {
            largest = impl;
        }
    }
    return largest;
}

static void cache_make_room(DisplayFontImpl *impl, size_t bytes) {
    while (impl->cache_count > 0 &&
           impl->cache_bytes + bytes > DISPLAY_SD_FONT_CACHE_BYTES) {
        cache_evict_one(impl);
    }

    while (s_total_cache_bytes + bytes > DISPLAY_SD_FONT_TOTAL_CACHE_BYTES) {
        DisplayFontImpl *largest = cache_find_largest();
        if (largest == NULL) {
            break;
        }
        cache_evict_one(largest);
    }
}

static void cache_insert(DisplayFontImpl *impl, CachedGlyph *glyph) {
    cache_make_room(impl, glyph->bytes);

    uint32_t hash = hash_codepoint(glyph->codepoint);
    glyph->hash_next = impl->hash[hash];
    impl->hash[hash] = glyph;
    lru_push_front(impl, glyph);
    impl->cache_bytes += glyph->bytes;
    s_total_cache_bytes += glyph->bytes;
    impl->cache_count++;
}

static bool library_acquire(void) {
    if (s_library == NULL && FT_Init_FreeType(&s_library) != 0) {
        ESP_LOGE(TAG, "FT_Init_FreeType failed");
        return false;
    }
    s_library_ref_count++;
    return true;
}

static void library_release(void) {
    if (s_library_ref_count > 0) {
        s_library_ref_count--;
    }
    if (s_library_ref_count == 0 && s_library != NULL) {
        FT_Done_FreeType(s_library);
        s_library = NULL;
    }
}

static void register_font_impl(DisplayFontImpl *impl) {
    impl->next = s_fonts;
    s_fonts = impl;
}

static void unregister_font_impl(DisplayFontImpl *impl) {
    DisplayFontImpl **slot = &s_fonts;
    while (*slot != NULL) {
        if (*slot == impl) {
            *slot = impl->next;
            impl->next = NULL;
            return;
        }
        slot = &(*slot)->next;
    }
}

static uint32_t utf8_next(const char **text) {
    const unsigned char *p = (const unsigned char *)*text;
    if (*p == '\0') {
        return 0;
    }

    if (*p < 0x80) {
        *text += 1;
        return *p;
    }

    uint32_t codepoint = 0;
    int expected = 0;
    uint32_t min_value = 0;
    if ((*p & 0xE0) == 0xC0) {
        codepoint = *p & 0x1F;
        expected = 2;
        min_value = 0x80;
    } else if ((*p & 0xF0) == 0xE0) {
        codepoint = *p & 0x0F;
        expected = 3;
        min_value = 0x800;
    } else if ((*p & 0xF8) == 0xF0) {
        codepoint = *p & 0x07;
        expected = 4;
        min_value = 0x10000;
    } else {
        *text += 1;
        return '?';
    }

    for (int i = 1; i < expected; i++) {
        if ((p[i] & 0xC0) != 0x80) {
            *text += 1;
            return '?';
        }
        codepoint = (codepoint << 6) | (p[i] & 0x3F);
    }

    if (codepoint < min_value || codepoint > 0x10FFFF ||
        (codepoint >= 0xD800 && codepoint <= 0xDFFF)) {
        *text += 1;
        return '?';
    }

    *text += expected;
    return codepoint;
}

static CachedGlyph *rasterize_glyph(DisplayFont *font, uint32_t codepoint) {
    if (font == NULL || font->impl == NULL) {
        return NULL;
    }

    DisplayFontImpl *impl = font->impl;
    FT_Face face = impl->face;
    if (!is_readable_ptr(face)) {
        if (face != NULL) {
            ESP_LOGE(TAG, "invalid FreeType face pointer: %p", face);
            impl->face = NULL;
            font->active = false;
        }
        return NULL;
    }

    CachedGlyph *cached = cache_lookup(impl, codepoint);
    if (cached != NULL) {
        return cached;
    }

    FT_UInt glyph_index = FT_Get_Char_Index(face, codepoint);
    if (glyph_index == 0 && codepoint != '?') {
        glyph_index = FT_Get_Char_Index(face, '?');
    }
    if (glyph_index == 0) {
        return NULL;
    }

    if (FT_Load_Glyph(face, glyph_index, FT_LOAD_DEFAULT) != 0) {
        return NULL;
    }

    FT_GlyphSlot slot = face->glyph;
    if (!is_readable_ptr(slot)) {
        ESP_LOGE(TAG, "invalid FreeType glyph slot for U+%04" PRIX32 ": %p", codepoint, slot);
        return NULL;
    }

    if (FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL) != 0) {
        return NULL;
    }

    slot = face->glyph;
    if (!is_readable_ptr(slot)) {
        ESP_LOGE(TAG, "invalid FreeType glyph slot after render for U+%04" PRIX32 ": %p", codepoint, slot);
        return NULL;
    }

    int width = (int)slot->bitmap.width;
    int height = (int)slot->bitmap.rows;
    int byte_width = (width + 1) / 2;
    size_t bitmap_bytes = (size_t)byte_width * (size_t)height;

    CachedGlyph *glyph = (CachedGlyph *)font_malloc(sizeof(*glyph));
    if (glyph == NULL) {
        ESP_LOGE(TAG, "glyph allocation failed for U+%04" PRIX32, codepoint);
        return NULL;
    }

    memset(glyph, 0, sizeof(*glyph));
    glyph->codepoint = codepoint;
    glyph->width = width;
    glyph->height = height;
    glyph->advance_x = (int)(slot->advance.x >> 6);
    glyph->left = slot->bitmap_left;
    glyph->top = slot->bitmap_top;
    glyph->bytes = sizeof(*glyph) + bitmap_bytes;

    if (bitmap_bytes > 0) {
        glyph->bitmap_4bpp = (uint8_t *)font_malloc(bitmap_bytes);
        if (glyph->bitmap_4bpp == NULL) {
            ESP_LOGE(TAG, "bitmap allocation failed for U+%04" PRIX32, codepoint);
            heap_caps_free(glyph);
            return NULL;
        }

        if (!is_readable_ptr(slot->bitmap.buffer)) {
            ESP_LOGE(TAG, "invalid FreeType bitmap buffer for U+%04" PRIX32, codepoint);
            heap_caps_free(glyph->bitmap_4bpp);
            heap_caps_free(glyph);
            return NULL;
        }

        int pitch = slot->bitmap.pitch;
        int pitch_abs = pitch < 0 ? -pitch : pitch;
        if (pitch_abs < width) {
            ESP_LOGE(TAG, "invalid FreeType bitmap pitch for U+%04" PRIX32 ": %d", codepoint, pitch);
            heap_caps_free(glyph->bitmap_4bpp);
            heap_caps_free(glyph);
            return NULL;
        }

        memset(glyph->bitmap_4bpp, 0, bitmap_bytes);
        for (int y = 0; y < height; y++) {
            int src_y = pitch >= 0 ? y : height - 1 - y;
            const uint8_t *src = slot->bitmap.buffer + (size_t)src_y * (size_t)pitch_abs;
            uint8_t *dst = glyph->bitmap_4bpp + (size_t)y * (size_t)byte_width;
            for (int x = 0; x < width; x++) {
                uint8_t value = src[x] >> 4;
                if ((x & 1) == 0) {
                    dst[x / 2] = value;
                } else {
                    dst[x / 2] |= value << 4;
                }
            }
        }
    }

    cache_insert(impl, glyph);
    return glyph;
}

static void copy_string(char *dest, size_t dest_size, const char *src) {
    if (dest_size == 0) {
        return;
    }
    size_t i = 0;
    while (i + 1 < dest_size && src != NULL && src[i] != '\0') {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

static void update_metrics(DisplayFont *font) {
    if (font == NULL || font->impl == NULL || font->impl->face == NULL) {
        return;
    }

    FT_Face face = font->impl->face;
    font->ascent = (int)(face->size->metrics.ascender >> 6);
    font->descent = (int)(face->size->metrics.descender >> 6);
    int raw_line_height = (int)(face->size->metrics.height >> 6);
    int fallback = (font->point_size * DISPLAY_SD_FONT_DPI + 71) / 72 + 4;
    font->line_height = raw_line_height > 0 ? raw_line_height : fallback;
}

#endif

static void display_font_reset(DisplayFont *font) {
    if (font == NULL) {
        return;
    }
    memset(font, 0, sizeof(*font));
    font->freetype_compiled = DISPLAY_FONT_HAS_FREETYPE != 0;
}

bool display_font_init(DisplayFont *font, const char *path, const char *name, int point_size) {
    if (font == NULL) {
        return false;
    }

    display_font_deinit(font);

#if DISPLAY_FONT_HAS_FREETYPE
    if (path == NULL || path[0] == '\0') {
        return false;
    }

    if (!library_acquire()) {
        return false;
    }

    DisplayFontImpl *impl = (DisplayFontImpl *)font_malloc(sizeof(*impl));
    if (impl == NULL) {
        ESP_LOGE(TAG, "font impl allocation failed");
        library_release();
        return false;
    }
    memset(impl, 0, sizeof(*impl));
    lru_init(impl);

    if (FT_New_Face(s_library, path, 0, &impl->face) != 0) {
        ESP_LOGE(TAG, "FT_New_Face failed: %s", path);
        heap_caps_free(impl);
        library_release();
        return false;
    }

    if (FT_Set_Char_Size(impl->face, 0, point_size << 6, DISPLAY_SD_FONT_DPI, DISPLAY_SD_FONT_DPI) != 0) {
        ESP_LOGE(TAG, "FT_Set_Char_Size failed: %dpt @ %ddpi", point_size, DISPLAY_SD_FONT_DPI);
        FT_Done_Face(impl->face);
        heap_caps_free(impl);
        library_release();
        return false;
    }

    font->freetype_compiled = true;
    font->active = true;
    font->point_size = point_size;
    font->impl = impl;
    copy_string(font->font_path, sizeof(font->font_path), path);
    copy_string(font->font_name, sizeof(font->font_name), name != NULL && name[0] != '\0' ? name : path);
    register_font_impl(impl);
    update_metrics(font);

    ESP_LOGI(
        TAG,
        "loaded SD font: %s size=%dpt dpi=%d ascent=%d descent=%d line=%d cache=%dK total_cache=%dK",
        font->font_name,
        font->point_size,
        DISPLAY_SD_FONT_DPI,
        font->ascent,
        font->descent,
        font->line_height,
        DISPLAY_SD_FONT_CACHE_BYTES / 1024,
        DISPLAY_SD_FONT_TOTAL_CACHE_BYTES / 1024
    );
    return true;
#else
    (void)path;
    (void)name;
    (void)point_size;
    ESP_LOGW(TAG, "FreeType component is not available; SD fonts are disabled");
    return false;
#endif
}

void display_font_deinit(DisplayFont *font) {
    if (font == NULL) {
        return;
    }

#if DISPLAY_FONT_HAS_FREETYPE
    DisplayFontImpl *impl = font->impl;
    if (impl != NULL) {
        cache_clear(impl);
        unregister_font_impl(impl);
        if (impl->face != NULL) {
            FT_Done_Face(impl->face);
            impl->face = NULL;
        }
        heap_caps_free(impl);
        library_release();
    }
#endif
    display_font_reset(font);
}

int display_font_line_height(const DisplayFont *font) {
    int fallback = (DISPLAY_SD_FONT_POINT_SIZE * DISPLAY_SD_FONT_DPI + 71) / 72 + 4;
    return font != NULL && font->line_height > 0 ? font->line_height : fallback;
}

int display_font_text_width(DisplayFont *font, const char *text) {
#if DISPLAY_FONT_HAS_FREETYPE
    if (font == NULL || !font->active || font->impl == NULL || text == NULL) {
        return 0;
    }

    int width = 0;
    const char *cursor = text;
    uint32_t codepoint;
    while ((codepoint = utf8_next(&cursor)) != 0) {
        if (codepoint == '\n' || codepoint == '\r') {
            break;
        }
        CachedGlyph *glyph = rasterize_glyph(font, codepoint);
        if (glyph != NULL) {
            width += glyph->advance_x;
        }
    }
    return width;
#else
    (void)font;
    (void)text;
    return 0;
#endif
}

enum EpdDrawError display_font_draw_text(
    DisplayFont *font,
    uint8_t *fb,
    const char *text,
    int *cursor_x,
    int *cursor_y,
    const EpdFontProperties *props
) {
#if DISPLAY_FONT_HAS_FREETYPE
    if (font == NULL || !font->active || font->impl == NULL ||
        fb == NULL || text == NULL || props == NULL) {
        return EPD_DRAW_STRING_INVALID;
    }

    enum EpdDrawError err = EPD_DRAW_SUCCESS;
    uint8_t color_lut[16];
    for (int c = 0; c < 16; c++) {
        int diff = (int)props->fg_color - (int)props->bg_color;
        int value = (int)props->bg_color + c * diff / 15;
        if (value < 0) {
            value = 0;
        } else if (value > 15) {
            value = 15;
        }
        color_lut[c] = (uint8_t)value;
    }

    int line_start = *cursor_x;
    if (props->flags & EPD_DRAW_BACKGROUND) {
        EpdRect background = {
            .x = *cursor_x,
            .y = *cursor_y - font->ascent,
            .width = display_font_text_width(font, text),
            .height = font->ascent - font->descent,
        };
        epd_fill_rect(background, props->bg_color << 4, fb);
    }

    const char *cursor = text;
    uint32_t codepoint;
    while ((codepoint = utf8_next(&cursor)) != 0) {
        if (codepoint == '\r') {
            continue;
        }
        if (codepoint == '\n') {
            *cursor_x = line_start;
            *cursor_y += display_font_line_height(font);
            continue;
        }

        CachedGlyph *glyph = rasterize_glyph(font, codepoint);
        if (glyph == NULL && props->fallback_glyph != 0) {
            glyph = rasterize_glyph(font, props->fallback_glyph);
        }
        if (glyph == NULL) {
            err |= EPD_DRAW_GLYPH_FALLBACK_FAILED;
            continue;
        }

        const int byte_width = (glyph->width + 1) / 2;
        if (glyph->bitmap_4bpp != NULL) {
            for (int y = 0; y < glyph->height; y++) {
                int yy = *cursor_y - glyph->top + y;
                int x_offset = 0;
                int start_x = *cursor_x + glyph->left;
                if (start_x < 0) {
                    x_offset = -start_x;
                }

                for (int x = x_offset; x < glyph->width; x++) {
                    uint8_t packed = glyph->bitmap_4bpp[(size_t)y * (size_t)byte_width + (size_t)x / 2];
                    uint8_t alpha = (x & 1) ? (packed >> 4) : (packed & 0x0F);
                    if (alpha != 0 || (props->flags & EPD_DRAW_BACKGROUND)) {
                        epd_draw_pixel(start_x + x, yy, color_lut[alpha] << 4, fb);
                    }
                }
            }
        }

        *cursor_x += glyph->advance_x;
    }

    return err;
#else
    (void)font;
    (void)fb;
    (void)text;
    (void)cursor_x;
    (void)cursor_y;
    (void)props;
    return EPD_DRAW_STRING_INVALID;
#endif
}
