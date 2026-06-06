#include <stddef.h>
#include <stdint.h>

#include "epd_board.h"
#include "epdiy.h"

#include "../output_common/render_method.h"
#include "../output_lcd/lcd_driver.h"
#include "esp_log.h"

#include <driver/gpio.h>
#include <sdkconfig.h>

// Make this compile on the ESP32 without ifdefing the whole file.
#ifndef CONFIG_IDF_TARGET_ESP32S3
#define GPIO_NUM_39 -1
#define GPIO_NUM_40 -1
#define GPIO_NUM_41 -1
#define GPIO_NUM_42 -1
#define GPIO_NUM_45 -1
#define GPIO_NUM_46 -1
#define GPIO_NUM_47 -1
#define GPIO_NUM_48 -1
#endif

#define DRIVER_ENABLE GPIO_NUM_46

#define D15 GPIO_NUM_47
#define D14 GPIO_NUM_21
#define D13 GPIO_NUM_14
#define D12 GPIO_NUM_13
#define D11 GPIO_NUM_12
#define D10 GPIO_NUM_11
#define D9 GPIO_NUM_10
#define D8 GPIO_NUM_9

#define D7 GPIO_NUM_8
#define D6 GPIO_NUM_18
#define D5 GPIO_NUM_17
#define D4 GPIO_NUM_16
#define D3 GPIO_NUM_15
#define D2 GPIO_NUM_7
#define D1 GPIO_NUM_6
#define D0 GPIO_NUM_5

/* Control Lines */
#define CKV GPIO_NUM_48
#define STH GPIO_NUM_41
#define LEH GPIO_NUM_42
#define STV GPIO_NUM_45

/* Edges */
#define CKH GPIO_NUM_4

static const char* TAG = "epdiy2_s3";

static int vcom = 1560;

static lcd_bus_config_t lcd_config = {
    .clock = CKH,
    .ckv = CKV,
    .leh = LEH,
    .start_pulse = STH,
    .stv = STV,
    .data[0] = D0,
    .data[1] = D1,
    .data[2] = D2,
    .data[3] = D3,
    .data[4] = D4,
    .data[5] = D5,
    .data[6] = D6,
    .data[7] = D7,
    .data[8] = D8,
    .data[9] = D9,
    .data[10] = D10,
    .data[11] = D11,
    .data[12] = D12,
    .data[13] = D13,
    .data[14] = D14,
    .data[15] = D15,
};

static void epd_board_init(uint32_t epd_row_width, const EpdInitConfig* init_config) {
    (void)epd_row_width;
    (void)init_config;

    gpio_hold_dis(CKH);

    gpio_set_direction(DRIVER_ENABLE, GPIO_MODE_OUTPUT);
    gpio_set_level(DRIVER_ENABLE, 0);

    const EpdDisplay_t* display = epd_get_display();
    if (display->bus_width > 16) {
        ESP_LOGE(TAG, "displays > 16 bit width are not supported");
    }

    LcdEpdConfig_t config = {
        .pixel_clock = display->bus_speed * 1000 * 1000,
        .ckv_high_time = 60,
        .line_front_porch = 4,
        .le_high_time = 4,
        .bus_width = display->bus_width,
        .bus = lcd_config,
    };
    epd_lcd_init(&config, display->width, display->height);
}

static void epd_board_deinit() {
    epd_lcd_deinit();
    gpio_set_level(DRIVER_ENABLE, 0);
}

static void epd_board_set_ctrl(epd_ctrl_state_t* state, const epd_ctrl_state_t* const mask) {
    (void)state;
    (void)mask;
}

static void epd_board_poweron(epd_ctrl_state_t* state) {
    if (state) {
        state->ep_stv = true;
        state->ep_mode = false;
        state->ep_output_enable = true;
    }
    gpio_set_level(DRIVER_ENABLE, 1);
}

static void epd_board_measure_vcom(epd_ctrl_state_t* state) {
    (void)state;
}

static void epd_board_poweroff(epd_ctrl_state_t* state) {
    if (state) {
        state->ep_stv = false;
        state->ep_mode = false;
        state->ep_output_enable = false;
    }
    gpio_set_level(DRIVER_ENABLE, 0);
}

static void epd_board_set_vcom(int value) {
    vcom = value;
    ESP_LOGI(TAG, "recorded VCOM %dmV; hardware driver voltage is controlled by GPIO46", vcom);
}

static float epd_board_ambient_temperature() {
    return 21.0f;
}

const EpdBoardDefinition epd_board_epdiy2_s3 = {
    .init = epd_board_init,
    .deinit = epd_board_deinit,
    .set_ctrl = epd_board_set_ctrl,
    .poweron = epd_board_poweron,
    .measure_vcom = epd_board_measure_vcom,
    .poweroff = epd_board_poweroff,
    .set_vcom = epd_board_set_vcom,
    .get_temperature = epd_board_ambient_temperature,
    .gpio_set_direction = NULL,
    .gpio_read = NULL,
    .gpio_write = NULL,
};
