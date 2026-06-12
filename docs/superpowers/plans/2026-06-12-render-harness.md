# Render Harness Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a host-side (macOS) harness that compiles the device's rendering code unchanged, draws the calendar screen into an epdiy-layout 4bpp framebuffer via stub implementations of the epdiy API, and emits a finished PNG — no flashing required.

**Architecture:** A standalone CMake project in `harness/` compiles `main/display_screen.c`, `main/display_viewport.c`, `main/display_font.c`, and `main/text_utils.c` against the **real** epdiy headers in `components/epdiy2/src/`. All epdiy functions are provided by `harness/src/epd_stub.c`, whose drawing behavior (rotation transform, 4bpp nibble layout, glyph rendering) is transcribed from `epdiy.c`/`font.c`. `epd_hl_update_screen()` writes the framebuffer as a grayscale PNG (minimal zlib-based encoder). A small `esp_shim/` directory fakes the ESP-IDF headers the app and epdiy headers include.

**Tech Stack:** C11, CMake >= 3.16, system zlib (glyph decompression + PNG), FreeType via Homebrew (optional — build degrades gracefully without it), ctest for unit/smoke tests.

**Reference spec:** `docs/superpowers/specs/2026-06-12-render-harness-design.md`

---

## File Structure

```text
harness/
  CMakeLists.txt              # host-only project: render_harness + harness_tests
  run.sh                      # one-shot configure + build + run
  README.md                   # usage, requirements, limitations
  src/
    harness_main.c            # CLI entry mirroring main.c's draw flow
    epd_stub.c                # all epd_* / epd_hl_* implementations
    epd_stub.h                # harness-only extras (output path setter)
    png_writer.c              # 8-bit grayscale PNG encoder (zlib)
    png_writer.h
    esp_shim/
      esp_log.h               # ESP_LOGx -> fprintf(stderr)
      esp_heap_caps.h         # heap_caps_malloc/free -> malloc/free
      esp_memory_utils.h      # esp_ptr_byte_accessible -> ptr != NULL
      esp_attr.h              # IRAM_ATTR/DRAM_ATTR -> empty
      esp_err.h               # esp_err_t = int
      sdmmc_cmd.h             # dummy sdmmc_card_t (sd_content.h needs it)
      driver/gpio.h           # gpio_num_t enum (app_config.h needs it)
      driver/i2c_master.h     # i2c_master_bus_handle_t = void*
      xtensa/core-macros.h    # empty (epd_board.h includes it)
  tests/
    harness_tests.c           # CHECK()-based unit tests, one binary
  assets/sdcard/
    content.txt               # sample display text (committed)
    fonts/.gitkeep            # drop a .ttf/.otf here (not committed)
  out/                        # rendered PNGs (gitignored)
```

Compiled from `main/` unchanged: `display_screen.c`, `display_viewport.c`, `display_font.c`, `text_utils.c` (plus headers `app_config.h`, `display_*.h`, `sd_content.h`, `text_utils.h`, `font_test_content.h`, `firasans_12.h`, `firasans_20.h`).

Key facts the implementer must know (verified against the sources):

- ED060KD1 is 1448x1072 native; `EPD_ROT_PORTRAIT` makes the app see 1072x1448.
- Framebuffer is 4bpp: row stride = `epd_width()/2` = 724 bytes; total = 724*1072 = 776128 bytes; white = `0xFF`.
- Portrait transform (`epdiy.c:_rotate`): swap x/y, then `x = epd_width() - x - 1`.
- Even native x lands in the **low** nibble, odd x in the **high** nibble.
- FiraSans font headers are zlib-compressed (`EpdFont.compressed == true`); glyph decompression must parse a zlib header (zlib's `uncompress()` does).
- `display_screen.h` includes `sd_content.h`, which includes `esp_err.h` and `sdmmc_cmd.h` — hence those shims.
- `epdiy.h` pulls in `epd_board.h` (needs `esp_err.h`, `xtensa/core-macros.h`) and `epd_init_config.h` (needs `driver/i2c_master.h`).
- `display_font.c` enables FreeType through `#if __has_include(<ft2build.h>)`; giving the target FreeType include dirs is all it takes.
- All commands below run from the **repo root**.

---

### Task 1: Scaffolding — shim headers, CMake project, header-compile sanity test

**Files:**
- Create: `harness/src/esp_shim/esp_log.h`
- Create: `harness/src/esp_shim/esp_heap_caps.h`
- Create: `harness/src/esp_shim/esp_memory_utils.h`
- Create: `harness/src/esp_shim/esp_attr.h`
- Create: `harness/src/esp_shim/esp_err.h`
- Create: `harness/src/esp_shim/sdmmc_cmd.h`
- Create: `harness/src/esp_shim/driver/gpio.h`
- Create: `harness/src/esp_shim/driver/i2c_master.h`
- Create: `harness/src/esp_shim/xtensa/core-macros.h`
- Create: `harness/CMakeLists.txt`
- Create: `harness/tests/harness_tests.c`
- Modify: `.gitignore`

- [ ] **Step 1: Create the shim headers**

`harness/src/esp_shim/esp_log.h`:

```c
#pragma once

#include <stdio.h>

#define ESP_LOGE(tag, fmt, ...) fprintf(stderr, "E (%s) " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) fprintf(stderr, "W (%s) " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) fprintf(stderr, "I (%s) " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...) fprintf(stderr, "D (%s) " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGV(tag, fmt, ...) fprintf(stderr, "V (%s) " fmt "\n", tag, ##__VA_ARGS__)
```

`harness/src/esp_shim/esp_heap_caps.h`:

```c
#pragma once

#include <stdlib.h>

#define MALLOC_CAP_SPIRAM 0
#define MALLOC_CAP_INTERNAL 0
#define MALLOC_CAP_8BIT 0
#define MALLOC_CAP_DEFAULT 0

static inline void *heap_caps_malloc(size_t size, unsigned int caps) {
    (void)caps;
    return malloc(size);
}

static inline void heap_caps_free(void *ptr) {
    free(ptr);
}
```

`harness/src/esp_shim/esp_memory_utils.h`:

```c
#pragma once

#include <stdbool.h>
#include <stddef.h>

static inline bool esp_ptr_byte_accessible(const void *ptr) {
    return ptr != NULL;
}
```

`harness/src/esp_shim/esp_attr.h`:

```c
#pragma once

#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif
#ifndef DRAM_ATTR
#define DRAM_ATTR
#endif
```

`harness/src/esp_shim/esp_err.h`:

```c
#pragma once

typedef int esp_err_t;

#define ESP_OK 0
#define ESP_FAIL -1
```

`harness/src/esp_shim/sdmmc_cmd.h`:

```c
#pragma once

typedef struct sdmmc_card_t {
    int unused;
} sdmmc_card_t;
```

`harness/src/esp_shim/driver/gpio.h`:

```c
#pragma once

typedef enum {
    GPIO_NUM_0 = 0,
    GPIO_NUM_1,
    GPIO_NUM_2,
    GPIO_NUM_3,
    GPIO_NUM_4,
    GPIO_NUM_5,
    GPIO_NUM_6,
    GPIO_NUM_7,
    GPIO_NUM_8,
    GPIO_NUM_9,
    GPIO_NUM_10,
    GPIO_NUM_11,
    GPIO_NUM_12,
    GPIO_NUM_13,
    GPIO_NUM_14,
    GPIO_NUM_15,
    GPIO_NUM_16,
    GPIO_NUM_17,
    GPIO_NUM_18,
    GPIO_NUM_19,
    GPIO_NUM_20,
    GPIO_NUM_21,
    GPIO_NUM_22,
    GPIO_NUM_23,
    GPIO_NUM_24,
    GPIO_NUM_25,
    GPIO_NUM_26,
    GPIO_NUM_27,
    GPIO_NUM_28,
    GPIO_NUM_29,
    GPIO_NUM_30,
    GPIO_NUM_31,
    GPIO_NUM_32,
    GPIO_NUM_33,
    GPIO_NUM_34,
    GPIO_NUM_35,
    GPIO_NUM_36,
    GPIO_NUM_37,
    GPIO_NUM_38,
    GPIO_NUM_39,
    GPIO_NUM_40,
    GPIO_NUM_41,
    GPIO_NUM_42,
    GPIO_NUM_43,
    GPIO_NUM_44,
    GPIO_NUM_45,
    GPIO_NUM_46,
    GPIO_NUM_47,
    GPIO_NUM_48,
} gpio_num_t;
```

`harness/src/esp_shim/driver/i2c_master.h`:

```c
#pragma once

typedef void *i2c_master_bus_handle_t;
```

`harness/src/esp_shim/xtensa/core-macros.h`:

```c
#pragma once
```

- [ ] **Step 2: Create the CMake project (tests target only for now)**

`harness/CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.16)
project(render_harness C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

find_package(ZLIB REQUIRED)

enable_testing()

add_executable(harness_tests
    tests/harness_tests.c
)
target_include_directories(harness_tests PRIVATE
    src
    src/esp_shim
    ${REPO_ROOT}/main
    ${REPO_ROOT}/components/epdiy2/src
)
target_link_libraries(harness_tests PRIVATE ZLIB::ZLIB)
target_compile_options(harness_tests PRIVATE -Wall -Wextra)
add_test(NAME harness_tests COMMAND harness_tests)
```

- [ ] **Step 3: Write the header-compile sanity test**

`harness/tests/harness_tests.c`:

```c
#include <stdio.h>

#include <epdiy.h>

#include "app_config.h"

static int failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
            failures++;                                                      \
        }                                                                    \
    } while (0)

int main(void) {
    CHECK(DISPLAY_VCOM_MV == 1560);
    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("all checks passed\n");
    return 0;
}
```

This proves the real `epdiy.h` (and its `epd_board.h`/`epd_init_config.h` chain) plus `app_config.h` compile against the shims.

- [ ] **Step 4: Build and run**

```sh
cmake -S harness -B harness/build
cmake --build harness/build
ctest --test-dir harness/build --output-on-failure
```

Expected: configure/build succeed; `1/1 Test #1: harness_tests .... Passed`.

- [ ] **Step 5: Add gitignore entries**

Append to `.gitignore`:

```text
harness/build/
harness/out/
harness/assets/sdcard/fonts/*
!harness/assets/sdcard/fonts/.gitkeep
```

- [ ] **Step 6: Commit**

```sh
git add harness .gitignore
git commit -m "Add render harness scaffolding with ESP shim headers"
```

---

### Task 2: PNG writer (zlib-based, grayscale 8-bit)

**Files:**
- Create: `harness/src/png_writer.h`
- Create: `harness/src/png_writer.c`
- Modify: `harness/CMakeLists.txt` (add source)
- Modify: `harness/tests/harness_tests.c` (add test)

- [ ] **Step 1: Write the failing test**

Add to `harness/tests/harness_tests.c` (below the CHECK macro; also add `#include <stdint.h>`, `#include <stdlib.h>`, `#include <string.h>` and `#include "png_writer.h"` at the top):

```c
static void test_png_writer_roundtrip(void) {
    const char *path = "harness_test_out.png";
    uint8_t pixels[4 * 2] = { 0, 85, 170, 255, 255, 170, 85, 0 };
    CHECK(png_write_gray8(path, pixels, 4, 2) == 0);

    FILE *f = fopen(path, "rb");
    CHECK(f != NULL);
    if (f) {
        uint8_t header[24] = { 0 };
        CHECK(fread(header, 1, 24, f) == 24);
        const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
        CHECK(memcmp(header, sig, 8) == 0);
        uint32_t w = ((uint32_t)header[16] << 24) | ((uint32_t)header[17] << 16)
            | ((uint32_t)header[18] << 8) | (uint32_t)header[19];
        uint32_t h = ((uint32_t)header[20] << 24) | ((uint32_t)header[21] << 16)
            | ((uint32_t)header[22] << 8) | (uint32_t)header[23];
        CHECK(w == 4);
        CHECK(h == 2);
        fclose(f);
        remove(path);
    }
}
```

Call it from `main()` before the failures check:

```c
    test_png_writer_roundtrip();
```

`harness/src/png_writer.h`:

```c
#pragma once

#include <stdint.h>

// Writes an 8-bit grayscale PNG. Returns 0 on success, -1 on failure.
int png_write_gray8(const char *path, const uint8_t *pixels, int width, int height);
```

- [ ] **Step 2: Run test to verify it fails**

```sh
cmake --build harness/build 2>&1 | tail -5
```

Expected: FAIL — linker error, `png_write_gray8` undefined.

- [ ] **Step 3: Implement the PNG writer**

`harness/src/png_writer.c`:

```c
#include "png_writer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zlib.h>

static void put_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static int write_chunk(FILE *f, const char type[4], const uint8_t *data, uint32_t len) {
    uint8_t hdr[8];
    put_be32(hdr, len);
    memcpy(hdr + 4, type, 4);
    if (fwrite(hdr, 1, 8, f) != 8) {
        return -1;
    }
    if (len > 0 && fwrite(data, 1, len, f) != len) {
        return -1;
    }
    uint32_t crc = (uint32_t)crc32(0, (const uint8_t *)type, 4);
    if (len > 0) {
        crc = (uint32_t)crc32(crc, data, len);
    }
    uint8_t crc_be[4];
    put_be32(crc_be, crc);
    if (fwrite(crc_be, 1, 4, f) != 4) {
        return -1;
    }
    return 0;
}

int png_write_gray8(const char *path, const uint8_t *pixels, int width, int height) {
    if (path == NULL || pixels == NULL || width <= 0 || height <= 0) {
        return -1;
    }

    // Raw image stream: one filter byte (0 = None) before each scanline.
    size_t stride = (size_t)width + 1;
    size_t raw_size = stride * (size_t)height;
    uint8_t *raw = malloc(raw_size);
    if (raw == NULL) {
        return -1;
    }
    for (int y = 0; y < height; y++) {
        raw[(size_t)y * stride] = 0;
        memcpy(raw + (size_t)y * stride + 1, pixels + (size_t)y * width, (size_t)width);
    }

    uLongf comp_size = compressBound((uLong)raw_size);
    uint8_t *comp = malloc(comp_size);
    if (comp == NULL) {
        free(raw);
        return -1;
    }
    int zrc = compress2(comp, &comp_size, raw, (uLong)raw_size, Z_BEST_SPEED);
    free(raw);
    if (zrc != Z_OK) {
        free(comp);
        return -1;
    }

    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        free(comp);
        return -1;
    }

    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    uint8_t ihdr[13];
    put_be32(ihdr, (uint32_t)width);
    put_be32(ihdr + 4, (uint32_t)height);
    ihdr[8] = 8;   // bit depth
    ihdr[9] = 0;   // color type: grayscale
    ihdr[10] = 0;  // compression method
    ihdr[11] = 0;  // filter method
    ihdr[12] = 0;  // interlace: none

    int ok = fwrite(sig, 1, 8, f) == 8 && write_chunk(f, "IHDR", ihdr, 13) == 0
        && write_chunk(f, "IDAT", comp, (uint32_t)comp_size) == 0
        && write_chunk(f, "IEND", NULL, 0) == 0;
    free(comp);
    if (fclose(f) != 0) {
        ok = 0;
    }
    return ok ? 0 : -1;
}
```

Add the source to `harness/CMakeLists.txt` (in `add_executable(harness_tests ...)`):

```cmake
add_executable(harness_tests
    tests/harness_tests.c
    src/png_writer.c
)
```

- [ ] **Step 4: Run test to verify it passes**

```sh
cmake --build harness/build && ctest --test-dir harness/build --output-on-failure
```

Expected: PASS (`all checks passed`).

- [ ] **Step 5: Commit**

```sh
git add harness
git commit -m "Add zlib-based grayscale PNG writer"
```

---

### Task 3: epd_stub core — display/board data, rotation, pixel and rect drawing

**Files:**
- Create: `harness/src/epd_stub.h`
- Create: `harness/src/epd_stub.c`
- Modify: `harness/CMakeLists.txt` (add source)
- Modify: `harness/tests/harness_tests.c` (add tests)

- [ ] **Step 1: Write the failing tests**

Add to `harness/tests/harness_tests.c` (also `#include "epd_stub.h"` at the top):

```c
static uint8_t *alloc_white_fb(void) {
    size_t size = (size_t)(epd_width() / 2) * (size_t)epd_height();
    uint8_t *fb = malloc(size);
    CHECK(fb != NULL);
    if (fb != NULL) {
        memset(fb, 0xFF, size);
    }
    return fb;
}

static size_t count_ink_nibbles(const uint8_t *fb) {
    size_t size = (size_t)(epd_width() / 2) * (size_t)epd_height();
    size_t ink = 0;
    for (size_t i = 0; i < size; i++) {
        if ((fb[i] & 0x0F) != 0x0F) {
            ink++;
        }
        if ((fb[i] & 0xF0) != 0xF0) {
            ink++;
        }
    }
    return ink;
}

static void test_display_geometry(void) {
    CHECK(epd_width() == 1448);
    CHECK(epd_height() == 1072);
    CHECK(epd_rotated_display_width() == 1072);
    CHECK(epd_rotated_display_height() == 1448);
}

static void test_portrait_pixel_mapping(void) {
    uint8_t *fb = alloc_white_fb();
    if (fb == NULL) {
        return;
    }
    // App-space (0,0) under EPD_ROT_PORTRAIT maps to native (1447, 0):
    // byte 1447/2 = 723, odd native x = high nibble.
    epd_draw_pixel(0, 0, 0x00, fb);
    CHECK(fb[723] == 0x0F);
    // App-space (1,0) maps to native (1447, 1): row stride 724.
    epd_draw_pixel(1, 0, 0x00, fb);
    CHECK(fb[724 + 723] == 0x0F);
    free(fb);
}

static void test_fill_rect_ink_count(void) {
    uint8_t *fb = alloc_white_fb();
    if (fb == NULL) {
        return;
    }
    EpdRect r = { .x = 10, .y = 20, .width = 3, .height = 2 };
    epd_fill_rect(r, 0x00, fb);
    CHECK(count_ink_nibbles(fb) == 6);
    free(fb);
}
```

In `main()`, initialize the stub like the device and call the tests:

```c
int main(void) {
    CHECK(DISPLAY_VCOM_MV == 1560);

    epd_init(&epd_board_epdiy2_s3, &ED060KD1, EPD_LUT_64K);
    epd_set_rotation(EPD_ROT_PORTRAIT);

    test_display_geometry();
    test_portrait_pixel_mapping();
    test_fill_rect_ink_count();
    test_png_writer_roundtrip();

    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("all checks passed\n");
    return 0;
}
```

`harness/src/epd_stub.h`:

```c
#pragma once

// Harness-only controls for the epdiy stub.
void epd_stub_set_output_path(const char *path);
const char *epd_stub_get_output_path(void);
```

- [ ] **Step 2: Run tests to verify they fail**

```sh
cmake --build harness/build 2>&1 | tail -5
```

Expected: FAIL — linker errors for `epd_init`, `epd_draw_pixel`, `ED060KD1`, etc.

- [ ] **Step 3: Implement the stub core**

`harness/src/epd_stub.c` (drawing logic transcribed from `components/epdiy2/src/epdiy.c`):

```c
// Host-side stand-in for the epdiy driver.
// Framebuffer layout, rotation, and glyph rendering are transcribed from
// components/epdiy2/src/epdiy.c and font.c so results match the device.
// Hardware control functions are no-ops.

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zlib.h>

#include <epdiy.h>

#include "epd_stub.h"
#include "png_writer.h"

typedef struct {
    uint16_t x;
    uint16_t y;
} Coord_xy;

#define _swap_int(a, b) \
    {                   \
        int t = a;      \
        a = b;          \
        b = t;          \
    }

static inline int min_int(int x, int y) {
    return x < y ? x : y;
}

static inline int max_int(int x, int y) {
    return x > y ? x : y;
}

// ---- board / display data ---------------------------------------------

const EpdDisplay_t ED060KD1 = {
    .width = 1448,
    .height = 1072,
    .bus_width = 8,
    .bus_speed = 20,
    .default_waveform = NULL,
    .display_type = DISPLAY_TYPE_GENERIC,
};

const EpdBoardDefinition epd_board_epdiy2_s3 = { 0 };

static const EpdDisplay_t *s_display = NULL;
static enum EpdRotation s_rotation = EPD_ROT_LANDSCAPE;
static char s_output_path[1024] = "render.png";

void epd_stub_set_output_path(const char *path) {
    snprintf(s_output_path, sizeof(s_output_path), "%s", path);
}

const char *epd_stub_get_output_path(void) {
    return s_output_path;
}

// ---- lifecycle / hardware no-ops ---------------------------------------

void epd_init(
    const EpdBoardDefinition *board, const EpdDisplay_t *display, enum EpdInitOptions options
) {
    (void)board;
    (void)options;
    s_display = display;
}

void epd_deinit(void) {
}

void epd_set_vcom(uint16_t vcom) {
    (void)vcom;
}

void epd_poweron(void) {
}

void epd_poweroff(void) {
}

void epd_clear(void) {
    // On hardware this flashes the panel; the framebuffer is untouched.
}

float epd_ambient_temperature(void) {
    return 25.0f;
}

int epd_width(void) {
    assert(s_display != NULL);
    return s_display->width;
}

int epd_height(void) {
    assert(s_display != NULL);
    return s_display->height;
}

// ---- rotation (transcribed from epdiy.c) --------------------------------

void epd_set_rotation(enum EpdRotation rotation) {
    s_rotation = rotation;
}

enum EpdRotation epd_get_rotation(void) {
    return s_rotation;
}

int epd_rotated_display_width(void) {
    int display_width = epd_width();
    switch (s_rotation) {
        case EPD_ROT_PORTRAIT:
        case EPD_ROT_INVERTED_PORTRAIT:
            display_width = epd_height();
            break;
        case EPD_ROT_INVERTED_LANDSCAPE:
        case EPD_ROT_LANDSCAPE:
            break;
    }
    return display_width;
}

int epd_rotated_display_height(void) {
    int display_height = epd_height();
    switch (s_rotation) {
        case EPD_ROT_PORTRAIT:
        case EPD_ROT_INVERTED_PORTRAIT:
            display_height = epd_width();
            break;
        case EPD_ROT_INVERTED_LANDSCAPE:
        case EPD_ROT_LANDSCAPE:
            break;
    }
    return display_height;
}

static Coord_xy _rotate(uint16_t x, uint16_t y) {
    switch (s_rotation) {
        case EPD_ROT_LANDSCAPE:
            break;
        case EPD_ROT_PORTRAIT:
            _swap_int(x, y);
            x = epd_width() - x - 1;
            break;
        case EPD_ROT_INVERTED_LANDSCAPE:
            x = epd_width() - x - 1;
            y = epd_height() - y - 1;
            break;
        case EPD_ROT_INVERTED_PORTRAIT:
            _swap_int(x, y);
            y = epd_height() - y - 1;
            break;
    }
    Coord_xy coord = { x, y };
    return coord;
}

// ---- pixel / rect drawing (transcribed from epdiy.c) ---------------------

void epd_draw_pixel(int x, int y, uint8_t color, uint8_t *framebuffer) {
    Coord_xy coord = _rotate((uint16_t)x, (uint16_t)y);
    x = coord.x;
    y = coord.y;

    if (x < 0 || x >= epd_width()) {
        return;
    }
    if (y < 0 || y >= epd_height()) {
        return;
    }

    uint8_t *buf_ptr = &framebuffer[y * epd_width() / 2 + x / 2];
    if (x % 2) {
        *buf_ptr = (*buf_ptr & 0x0F) | (color & 0xF0);
    } else {
        *buf_ptr = (*buf_ptr & 0xF0) | (color >> 4);
    }
}

void epd_draw_hline(int x, int y, int length, uint8_t color, uint8_t *framebuffer) {
    for (int i = 0; i < length; i++) {
        epd_draw_pixel(x + i, y, color, framebuffer);
    }
}

void epd_draw_vline(int x, int y, int length, uint8_t color, uint8_t *framebuffer) {
    for (int i = 0; i < length; i++) {
        epd_draw_pixel(x, y + i, color, framebuffer);
    }
}

void epd_fill_rect(EpdRect rect, uint8_t color, uint8_t *framebuffer) {
    for (int i = rect.y; i < rect.y + rect.height; i++) {
        epd_draw_hline(rect.x, i, rect.width, color, framebuffer);
    }
}

uint8_t epd_get_pixel(int x, int y, int fb_width, int fb_height, const uint8_t *framebuffer) {
    if (x < 0 || x >= fb_width) {
        return 0;
    }
    if (y < 0 || y >= fb_height) {
        return 0;
    }
    int fb_width_bytes = fb_width / 2 + fb_width % 2;
    uint8_t buf_val = framebuffer[y * fb_width_bytes + x / 2];
    if (x % 2) {
        buf_val = (buf_val & 0xF0) >> 4;
    } else {
        buf_val = (buf_val & 0x0F);
    }
    return buf_val << 4;
}
```

Add the source to `harness/CMakeLists.txt`:

```cmake
add_executable(harness_tests
    tests/harness_tests.c
    src/png_writer.c
    src/epd_stub.c
)
```

- [ ] **Step 4: Run tests to verify they pass**

```sh
cmake --build harness/build && ctest --test-dir harness/build --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit**

```sh
git add harness
git commit -m "Add epdiy stub core with rotation and pixel drawing"
```

---

### Task 4: epd_stub text rendering and highlevel API

**Files:**
- Modify: `harness/src/epd_stub.c` (append)
- Modify: `harness/tests/harness_tests.c` (add tests)

- [ ] **Step 1: Write the failing tests**

Add to `harness/tests/harness_tests.c`. The FiraSans data header defines the arrays, so include it **only here** in the test binary (top of file, after the other includes):

```c
#include "firasans_12.h"
```

New tests:

```c
static void test_text_bounds(void) {
    EpdFontProperties props = epd_font_properties_default();
    int x = 50;
    int y = 100;
    int x1 = 0;
    int y1 = 0;
    int w = 0;
    int h = 0;
    epd_get_text_bounds(&FiraSans_12, "Hello", &x, &y, &x1, &y1, &w, &h, &props);
    CHECK(w > 0);
    CHECK(h > 0);
}

static void test_write_string_draws_ink(void) {
    uint8_t *fb = alloc_white_fb();
    if (fb == NULL) {
        return;
    }
    EpdFontProperties props = epd_font_properties_default();
    int x = 50;
    int y = 100;
    enum EpdDrawError err = epd_write_string(&FiraSans_12, "Ag", &x, &y, fb, &props);
    CHECK(err == EPD_DRAW_SUCCESS);
    CHECK(x > 50);
    CHECK(count_ink_nibbles(fb) > 0);
    free(fb);
}

static void test_highlevel_framebuffer(void) {
    EpdiyHighlevelState hl = epd_hl_init(EPD_BUILTIN_WAVEFORM);
    uint8_t *fb = epd_hl_get_framebuffer(&hl);
    CHECK(fb != NULL);
    epd_draw_pixel(0, 0, 0x00, fb);
    epd_hl_set_all_white(&hl);
    CHECK(fb[723] == 0xFF);
    free(hl.front_fb);
}
```

Call them from `main()` after `test_fill_rect_ink_count();`:

```c
    test_text_bounds();
    test_write_string_draws_ink();
    test_highlevel_framebuffer();
```

- [ ] **Step 2: Run tests to verify they fail**

```sh
cmake --build harness/build 2>&1 | tail -5
```

Expected: FAIL — linker errors for `epd_font_properties_default`, `epd_get_text_bounds`, `epd_write_string`, `epd_hl_init`.

- [ ] **Step 3: Implement text rendering and highlevel API**

Append to `harness/src/epd_stub.c` (transcribed from `components/epdiy2/src/font.c`; the only change: glyph decompression uses zlib's `uncompress()` instead of miniz tinfl, and the static text-line helper is named `write_text_line` to avoid clashing with epdiy.c's graphics `epd_write_line`):

```c
// ---- UTF-8 decoding (transcribed from font.c) ----------------------------

typedef struct {
    uint8_t mask;
    uint8_t lead;
    uint32_t beg;
    uint32_t end;
    int bits_stored;
} utf_t;

static utf_t *utf[] = {
    [0] = &(utf_t){ 0b00111111, 0b10000000, 0, 0, 6 },
    [1] = &(utf_t){ 0b01111111, 0b00000000, 0000, 0177, 7 },
    [2] = &(utf_t){ 0b00011111, 0b11000000, 0200, 03777, 5 },
    [3] = &(utf_t){ 0b00001111, 0b11100000, 04000, 0177777, 4 },
    [4] = &(utf_t){ 0b00000111, 0b11110000, 0200000, 04177777, 3 },
    &(utf_t){ 0 },
};

static int utf8_len(const uint8_t ch) {
    int len = 0;
    for (utf_t **u = utf; (*u)->mask; ++u) {
        if ((ch & ~(*u)->mask) == (*u)->lead) {
            break;
        }
        ++len;
    }
    if (len > 4) {
        assert("invalid unicode.");
    }
    return len;
}

static uint32_t next_cp(const uint8_t **string) {
    if (**string == 0) {
        return 0;
    }
    int bytes = utf8_len(**string);
    const uint8_t *chr = *string;
    *string += bytes;
    int shift = utf[0]->bits_stored * (bytes - 1);
    uint32_t codep = (*chr++ & utf[bytes]->mask) << shift;

    for (int i = 1; i < bytes; ++i, ++chr) {
        shift -= utf[0]->bits_stored;
        codep |= ((const uint8_t)*chr & utf[0]->mask) << shift;
    }

    return codep;
}

// ---- font rendering (transcribed from font.c) -----------------------------

EpdFontProperties epd_font_properties_default(void) {
    EpdFontProperties props
        = { .fg_color = 0, .bg_color = 15, .fallback_glyph = 0, .flags = EPD_DRAW_ALIGN_LEFT };
    return props;
}

const EpdGlyph *epd_get_glyph(const EpdFont *font, uint32_t code_point) {
    const EpdUnicodeInterval *intervals = font->intervals;
    for (uint32_t i = 0; i < font->interval_count; i++) {
        const EpdUnicodeInterval *interval = &intervals[i];
        if (code_point >= interval->first && code_point <= interval->last) {
            return &font->glyph[interval->offset + (code_point - interval->first)];
        }
        if (code_point < interval->first) {
            return NULL;
        }
    }
    return NULL;
}

static int stub_uncompress(
    uint8_t *dest, size_t uncompressed_size, const uint8_t *source, size_t source_size
) {
    if (uncompressed_size == 0 || dest == NULL || source_size == 0 || source == NULL) {
        return -1;
    }
    uLongf out_len = uncompressed_size;
    int rc = uncompress(dest, &out_len, source, (uLong)source_size);
    return (rc == Z_OK && out_len == uncompressed_size) ? 0 : -1;
}

static enum EpdDrawError draw_char(
    const EpdFont *font,
    uint8_t *buffer,
    int *cursor_x,
    int cursor_y,
    uint32_t cp,
    const EpdFontProperties *props
) {
    assert(props != NULL);

    const EpdGlyph *glyph = epd_get_glyph(font, cp);
    if (!glyph) {
        glyph = epd_get_glyph(font, props->fallback_glyph);
    }

    if (!glyph) {
        return EPD_DRAW_GLYPH_FALLBACK_FAILED;
    }

    uint32_t offset = glyph->data_offset;
    uint16_t width = glyph->width, height = glyph->height;
    int left = glyph->left;

    int byte_width = (width / 2 + width % 2);
    unsigned long bitmap_size = (unsigned long)byte_width * height;
    const uint8_t *bitmap = NULL;
    if (bitmap_size > 0 && font->compressed) {
        uint8_t *tmp_bitmap = (uint8_t *)malloc(bitmap_size);
        if (tmp_bitmap == NULL) {
            fprintf(stderr, "epd_stub: glyph malloc failed\n");
            return EPD_DRAW_FAILED_ALLOC;
        }
        stub_uncompress(tmp_bitmap, bitmap_size, &font->bitmap[offset], glyph->compressed_size);
        bitmap = tmp_bitmap;
    } else {
        bitmap = &font->bitmap[offset];
    }

    uint8_t color_lut[16];
    for (int c = 0; c < 16; c++) {
        int color_difference = (int)props->fg_color - (int)props->bg_color;
        color_lut[c] = max_int(0, min_int(15, props->bg_color + c * color_difference / 15));
    }
    bool background_needed = props->flags & EPD_DRAW_BACKGROUND;

    for (int y = 0; y < height; y++) {
        int yy = cursor_y - glyph->top + y;
        int start_pos = *cursor_x + left;
        int x = max_int(0, -start_pos);
        int max_x = start_pos + width;

        for (int xx = start_pos; xx < max_x; xx++) {
            uint8_t bm = bitmap[y * byte_width + x / 2];
            if ((x & 1) == 0) {
                bm = bm & 0xF;
            } else {
                bm = bm >> 4;
            }
            if (background_needed || bm) {
                uint8_t color = color_lut[bm] << 4;
                epd_draw_pixel(xx, yy, color, buffer);
            }
            x++;
        }
    }
    if (bitmap_size > 0 && font->compressed) {
        free((uint8_t *)bitmap);
    }
    *cursor_x += glyph->advance_x;
    return EPD_DRAW_SUCCESS;
}

static void get_char_bounds(
    const EpdFont *font,
    uint32_t cp,
    int *x,
    int *y,
    int *minx,
    int *miny,
    int *maxx,
    int *maxy,
    const EpdFontProperties *props
) {
    assert(props != NULL);

    const EpdGlyph *glyph = epd_get_glyph(font, cp);
    if (!glyph) {
        glyph = epd_get_glyph(font, props->fallback_glyph);
    }
    if (!glyph) {
        return;
    }

    int x1 = *x + glyph->left, y1 = *y + glyph->top - glyph->height, x2 = x1 + glyph->width,
        y2 = y1 + glyph->height;

    if (props->flags & EPD_DRAW_BACKGROUND) {
        *minx = min_int(*x, min_int(*minx, x1));
        *maxx = max_int(max_int(*x + glyph->advance_x, x2), *maxx);
        *miny = min_int(*y + font->descender, min_int(*miny, y1));
        *maxy = max_int(*y + font->ascender, max_int(*maxy, y2));
    } else {
        if (x1 < *minx)
            *minx = x1;
        if (y1 < *miny)
            *miny = y1;
        if (x2 > *maxx)
            *maxx = x2;
        if (y2 > *maxy)
            *maxy = y2;
    }
    *x += glyph->advance_x;
}

void epd_get_text_bounds(
    const EpdFont *font,
    const char *string,
    const int *x,
    const int *y,
    int *x1,
    int *y1,
    int *w,
    int *h,
    const EpdFontProperties *properties
) {
    assert(properties != NULL);
    EpdFontProperties props = *properties;

    if (*string == '\0') {
        *w = 0;
        *h = 0;
        *y1 = *y;
        *x1 = *x;
        return;
    }
    int minx = 100000, miny = 100000, maxx = -1, maxy = -1;
    int original_x = *x;
    int temp_x = *x;
    int temp_y = *y;
    uint32_t c;
    while ((c = next_cp((const uint8_t **)&string))) {
        get_char_bounds(font, c, &temp_x, &temp_y, &minx, &miny, &maxx, &maxy, &props);
    }
    *x1 = min_int(original_x, minx);
    *w = maxx - *x1;
    *y1 = miny;
    *h = maxy - miny;
}

static enum EpdDrawError write_text_line(
    const EpdFont *font,
    const char *string,
    int *cursor_x,
    int *cursor_y,
    uint8_t *framebuffer,
    const EpdFontProperties *properties
) {
    assert(framebuffer != NULL);

    if (*string == '\0') {
        return EPD_DRAW_SUCCESS;
    }

    assert(properties != NULL);
    EpdFontProperties props = *properties;
    enum EpdFontFlags alignment_mask
        = EPD_DRAW_ALIGN_LEFT | EPD_DRAW_ALIGN_RIGHT | EPD_DRAW_ALIGN_CENTER;
    enum EpdFontFlags alignment = props.flags & alignment_mask;

    if ((alignment & (alignment - 1)) != 0) {
        return EPD_DRAW_INVALID_FONT_FLAGS;
    }

    int x1 = 0, y1 = 0, w = 0, h = 0;
    int tmp_cur_x = *cursor_x;
    int tmp_cur_y = *cursor_y;
    epd_get_text_bounds(font, string, &tmp_cur_x, &tmp_cur_y, &x1, &y1, &w, &h, &props);

    if (w < 0 || h < 0) {
        return EPD_DRAW_NO_DRAWABLE_CHARACTERS;
    }

    uint8_t *buffer = framebuffer;
    int local_cursor_x = *cursor_x;
    int local_cursor_y = *cursor_y;
    uint32_t c;

    int cursor_x_init = local_cursor_x;
    int cursor_y_init = local_cursor_y;

    switch (alignment) {
        case EPD_DRAW_ALIGN_CENTER:
            local_cursor_x -= w / 2;
            break;
        case EPD_DRAW_ALIGN_RIGHT:
            local_cursor_x -= w;
            break;
        case EPD_DRAW_ALIGN_LEFT:
        default:
            break;
    }

    uint8_t bg = props.bg_color;
    if (props.flags & EPD_DRAW_BACKGROUND) {
        for (int l = local_cursor_y - font->ascender; l < local_cursor_y - font->descender; l++) {
            epd_draw_hline(local_cursor_x, l, w, bg << 4, buffer);
        }
    }
    enum EpdDrawError err = EPD_DRAW_SUCCESS;
    while ((c = next_cp((const uint8_t **)&string))) {
        err |= draw_char(font, buffer, &local_cursor_x, local_cursor_y, c, &props);
    }

    *cursor_x += local_cursor_x - cursor_x_init;
    *cursor_y += local_cursor_y - cursor_y_init;
    return err;
}

enum EpdDrawError epd_write_string(
    const EpdFont *font,
    const char *string,
    int *cursor_x,
    int *cursor_y,
    uint8_t *framebuffer,
    const EpdFontProperties *properties
) {
    char *token, *newstring, *tofree;
    if (string == NULL) {
        fprintf(stderr, "epd_stub: cannot draw a NULL string\n");
        return EPD_DRAW_STRING_INVALID;
    }
    tofree = newstring = strdup(string);
    if (newstring == NULL) {
        fprintf(stderr, "epd_stub: cannot allocate string copy\n");
        return EPD_DRAW_FAILED_ALLOC;
    }

    enum EpdDrawError err = EPD_DRAW_SUCCESS;
    int line_start = *cursor_x;
    while ((token = strsep(&newstring, "\n")) != NULL) {
        *cursor_x = line_start;
        err |= write_text_line(font, token, cursor_x, cursor_y, framebuffer, properties);
        *cursor_y += font->advance_y;
    }

    free(tofree);
    return err;
}

// ---- highlevel API -------------------------------------------------------

static size_t fb_size(void) {
    return (size_t)(epd_width() / 2) * (size_t)epd_height();
}

EpdiyHighlevelState epd_hl_init(const EpdWaveform *waveform) {
    EpdiyHighlevelState state = { 0 };
    state.waveform = waveform;
    state.front_fb = malloc(fb_size());
    if (state.front_fb == NULL) {
        fprintf(stderr, "epd_stub: framebuffer allocation failed\n");
        exit(1);
    }
    memset(state.front_fb, 0xFF, fb_size());
    return state;
}

uint8_t *epd_hl_get_framebuffer(EpdiyHighlevelState *state) {
    return state->front_fb;
}

void epd_hl_set_all_white(EpdiyHighlevelState *state) {
    memset(state->front_fb, 0xFF, fb_size());
}

enum EpdDrawError epd_hl_update_screen(
    EpdiyHighlevelState *state, enum EpdDrawMode mode, int temperature
) {
    (void)mode;
    (void)temperature;

    int out_w = epd_rotated_display_width();
    int out_h = epd_rotated_display_height();
    uint8_t *gray = malloc((size_t)out_w * (size_t)out_h);
    if (gray == NULL) {
        return EPD_DRAW_FAILED_ALLOC;
    }

    for (int y = 0; y < out_h; y++) {
        for (int x = 0; x < out_w; x++) {
            Coord_xy c = _rotate((uint16_t)x, (uint16_t)y);
            uint8_t nibble = epd_get_pixel(c.x, c.y, epd_width(), epd_height(), state->front_fb) >> 4;
            gray[(size_t)y * out_w + x] = (uint8_t)(nibble * 17);
        }
    }

    int rc = png_write_gray8(s_output_path, gray, out_w, out_h);
    free(gray);
    if (rc != 0) {
        fprintf(stderr, "epd_stub: failed to write %s\n", s_output_path);
        return EPD_DRAW_FAILED_ALLOC;
    }
    return EPD_DRAW_SUCCESS;
}
```

- [ ] **Step 4: Run tests to verify they pass**

```sh
cmake --build harness/build && ctest --test-dir harness/build --output-on-failure
```

Expected: PASS. The `test_write_string_draws_ink` test exercises the zlib glyph decompression path (FiraSans is compressed).

- [ ] **Step 5: Commit**

```sh
git add harness
git commit -m "Add stub text rendering and highlevel framebuffer API"
```

---

### Task 5: render_harness binary — full draw flow, assets, run script

**Files:**
- Create: `harness/src/harness_main.c`
- Create: `harness/assets/sdcard/content.txt`
- Create: `harness/assets/sdcard/fonts/.gitkeep` (empty file)
- Create: `harness/run.sh`
- Modify: `harness/CMakeLists.txt` (add render_harness target + smoke test)

- [ ] **Step 1: Write harness_main.c**

`harness/src/harness_main.c`:

```c
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
        snprintf(
            content->text, sizeof(content->text), "harness: cannot open %s\n", txt_path
        );
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
```

- [ ] **Step 2: Add the render_harness target and smoke test to CMake**

Append to `harness/CMakeLists.txt`:

```cmake
add_executable(render_harness
    src/harness_main.c
    src/epd_stub.c
    src/png_writer.c
    ${REPO_ROOT}/main/display_screen.c
    ${REPO_ROOT}/main/display_viewport.c
    ${REPO_ROOT}/main/display_font.c
    ${REPO_ROOT}/main/text_utils.c
)
target_include_directories(render_harness PRIVATE
    src
    src/esp_shim
    ${REPO_ROOT}/main
    ${REPO_ROOT}/components/epdiy2/src
)
target_link_libraries(render_harness PRIVATE ZLIB::ZLIB)
target_compile_options(render_harness PRIVATE -Wall -Wextra)

find_package(Freetype)
if(FREETYPE_FOUND)
    target_link_libraries(render_harness PRIVATE Freetype::Freetype)
else()
    message(WARNING "FreeType not found; SD fonts disabled, built-in font fallback only")
endif()

add_test(
    NAME render_smoke
    COMMAND render_harness
        --sdcard ${CMAKE_CURRENT_SOURCE_DIR}/assets/sdcard
        --out ${CMAKE_CURRENT_BINARY_DIR}/smoke.png
)
```

- [ ] **Step 3: Create the sample assets**

`harness/assets/sdcard/content.txt` (sample text covering ASCII, Chinese, and a long wrapping line):

```text
Render Harness Sample
=====================

This is line-wrapped ASCII content to exercise draw_wrapped_line with a deliberately long sentence that should exceed the viewport width and wrap onto the next line cleanly.

中文渲染测试：这一行用来验证 FreeType 字体在主机端的渲染结果，包括标点、换行与字宽计算。

1234567890 !@#$%^&*()
End of sample.
```

Create the empty placeholder `harness/assets/sdcard/fonts/.gitkeep`.

- [ ] **Step 4: Create run.sh**

`harness/run.sh`:

```sh
#!/bin/sh
set -e
cd "$(dirname "$0")/.."

cmake -S harness -B harness/build
cmake --build harness/build
mkdir -p harness/out
./harness/build/render_harness "$@"
```

Make it executable: `chmod +x harness/run.sh`

- [ ] **Step 5: Build, run, and verify the output PNG**

```sh
cmake --build harness/build && ctest --test-dir harness/build --output-on-failure
./harness/run.sh
sips -g pixelWidth -g pixelHeight harness/out/render.png
```

Expected:
- Both tests pass (`harness_tests`, `render_smoke`).
- run.sh prints `display: 1072x1448, viewport: 1072x1448 at 0,0` and `rendered: harness/out/render.png (1072x1448)`.
- sips reports `pixelWidth: 1072`, `pixelHeight: 1448`.
- Without a font in `assets/sdcard/fonts/`, stderr shows the built-in-font fallback message — same degradation as the device.

Open the PNG and visually confirm the "SD Card" title and wrapped body text:

```sh
open harness/out/render.png
```

- [ ] **Step 6: Commit**

```sh
git add harness
git commit -m "Add render_harness binary with sample assets and run script"
```

---

### Task 6: README and final verification

**Files:**
- Create: `harness/README.md`

- [ ] **Step 1: Write the README**

`harness/README.md`:

```markdown
# Render Harness

在电脑上直接编译运行设备端渲染代码（`main/display_screen.c` 等，零修改），
输出渲染完成的 PNG，无需烧录开发板。

## 依赖

- CMake >= 3.16，主机 C 编译器（Xcode CLT 自带 clang 即可）
- zlib（macOS 系统自带）
- FreeType（可选）：`brew install freetype`；缺失时自动降级为内置 FiraSans 字体

## 用法

```sh
./harness/run.sh
open harness/out/render.png
```

可选参数：

```sh
./harness/run.sh --sdcard <目录> --out <png路径>
```

## 模拟 SD 卡

`harness/assets/sdcard/` 模拟设备的 SD 卡：

- 第一个（按文件名排序）`.txt` 作为显示内容，截断行为与设备一致（2300 字节）
- 字体优先用 `app_config.h` 中 `SD_DEFAULT_FONT_PATH` 配置的路径，
  否则取 `fonts/` 下第一个 `.ttf`/`.otf`（字体文件不入库，自行放入）

## 工作原理

- 应用渲染源码原样编译，include 真实的 `components/epdiy2/src/epdiy.h`
- epdiy 驱动由 `src/epd_stub.c` 替身实现：4bpp framebuffer 布局、
  旋转变换、glyph 解压绘制均对照 epdiy 源码移植
- `epd_hl_update_screen()` 在主机上的语义是把 framebuffer 按
  `EPD_ROT_PORTRAIT` 方向导出为 1072x1448 灰度 PNG

## 局限

- 不模拟电子纸波形、残影、刷新闪烁，输出为理想灰度
- `epd_clear()` 在主机上是 no-op（设备上是面板闪刷，不影响 framebuffer）
- 单元测试：`ctest --test-dir harness/build`
```

- [ ] **Step 2: Full clean verification**

```sh
rm -rf harness/build
./harness/run.sh
ctest --test-dir harness/build --output-on-failure
```

Expected: clean configure/build, PNG rendered, all tests pass.

- [ ] **Step 3: Commit**

```sh
git add harness
git commit -m "Add render harness README"
```

---

## Verification checklist (after all tasks)

- [ ] `./harness/run.sh` produces `harness/out/render.png`, 1072x1448, viewable.
- [ ] `ctest --test-dir harness/build` — all tests pass.
- [ ] With a `.ttf` dropped into `harness/assets/sdcard/fonts/`, the render uses the FreeType path (font name shown next to "SD Card" title in the image).
- [ ] Without a font, output falls back to FiraSans (same as device).
- [ ] `idf.py build` still succeeds (no device-side files were modified).
