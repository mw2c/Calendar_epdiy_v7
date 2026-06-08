#include <stdbool.h>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <epdiy.h>

#include "app_config.h"
#include "button_input.h"
#include "display_screen.h"
#include "display_viewport.h"
#include "display_font.h"
#include "sd_content.h"

static const char *TAG = "calendar_epdiy_v7";
static SdDisplayContent s_sd_content;
static DisplayFont s_display_font;

static void button_log_task(void *arg) {
    (void)arg;

    if (!button_input_init()) {
        ESP_LOGE(TAG, "button input initialization failed");
        vTaskDelete(NULL);
        return;
    }

    ButtonInputKey last_key = BUTTON_INPUT_NONE;
    while (true) {
        int raw = 0;
        ButtonInputKey key = button_input_poll(&raw);
        if (key != last_key) {
            if (key == BUTTON_INPUT_NONE) {
                ESP_LOGI(TAG, "button released, raw=%d", raw);
            } else {
                ESP_LOGI(TAG, "%s pressed, raw=%d", button_input_key_name(key), raw);
            }
            last_key = key;
        }
        vTaskDelay(pdMS_TO_TICKS(ADC_BUTTON_POLL_INTERVAL_MS));
    }
}

static bool load_sd_font(void) {
    if (!s_sd_content.font_found) {
        ESP_LOGW(TAG, "configured SD font is not available: %s", s_sd_content.font_path);
        return false;
    }

    if (!display_font_init(
            &s_display_font,
            s_sd_content.font_path,
            s_sd_content.font_name,
            DISPLAY_SD_FONT_POINT_SIZE
        )) {
        ESP_LOGE(TAG, "failed to load configured SD font: %s", s_sd_content.font_path);
        return false;
    }

    ESP_LOGI(TAG, "loaded configured SD font: %s", s_display_font.font_name);
    return true;
}

static void unload_sd_font(void) {
    display_font_deinit(&s_display_font);
}

void app_main(void) {
    xTaskCreate(button_log_task, "button_log", 3072, NULL, 5, NULL);

    epd_init(&DISPLAY_BOARD, &DISPLAY_MODEL, EPD_LUT_64K);
    epd_set_vcom(DISPLAY_VCOM_MV);
    epd_set_rotation(EPD_ROT_PORTRAIT);
    viewport_init();

    EpdiyHighlevelState hl = epd_hl_init(EPD_BUILTIN_WAVEFORM);

    ESP_LOGI(
        TAG,
        "display initialized: %dx%d, viewport: %dx%d at %d,%d",
        epd_rotated_display_width(),
        epd_rotated_display_height(),
        viewport_width(),
        viewport_height(),
        viewport_screen_x(0),
        viewport_screen_y(0)
    );

    display_clear_screen();

    SdCardHandle sd;
    esp_err_t sd_err = sd_mount_card(&sd);
    if (sd_err == ESP_OK) {
        sd_load_display_content(&s_sd_content, SD_DEFAULT_FONT_PATH);
    } else {
        sd_fill_mount_error(&s_sd_content, sd_err);
    }

    bool font_loaded = sd_err == ESP_OK && load_sd_font();

    display_draw_sd_screen(&hl, &s_sd_content, font_loaded ? &s_display_font : NULL);

    epd_poweron();
    enum EpdDrawError err = epd_hl_update_screen(&hl, MODE_GC16, (int)epd_ambient_temperature());
    epd_poweroff();

    if (err != EPD_DRAW_SUCCESS) {
        ESP_LOGE(TAG, "draw error: %d", err);
    }

    unload_sd_font();
    if (sd_err == ESP_OK) {
        sd_unmount_card(&sd);
    }

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
