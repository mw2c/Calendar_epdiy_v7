#include "button_input.h"

#include <esp_adc/adc_oneshot.h>
#include <esp_log.h>
#include <esp_err.h>

#include "app_config.h"

static const char *TAG = "button_input";

static adc_oneshot_unit_handle_t s_adc_unit;
static adc_unit_t s_adc_unit_id;
static adc_channel_t s_adc_channel;
static bool s_initialized;
static ButtonInputKey s_stable_key = BUTTON_INPUT_NONE;
static ButtonInputKey s_candidate_key = BUTTON_INPUT_NONE;
static int s_candidate_count;

static ButtonInputKey classify_raw(int raw) {
    if (raw < ADC_BUTTON_LOW_RAW_MAX) {
        return BUTTON_INPUT_KEY_3;
    }
    if (raw >= ADC_BUTTON_MID_RAW_MIN && raw < ADC_BUTTON_MID_RAW_MAX) {
        return BUTTON_INPUT_KEY_2;
    }
    if (raw >= ADC_BUTTON_HIGH_RAW_MIN && raw < ADC_BUTTON_HIGH_RAW_MAX) {
        return BUTTON_INPUT_KEY_1;
    }
    return BUTTON_INPUT_NONE;
}

bool button_input_init(void) {
    if (s_initialized) {
        return true;
    }

    esp_err_t err = adc_oneshot_io_to_channel(ADC_BUTTON_GPIO, &s_adc_unit_id, &s_adc_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GPIO%d is not an ADC input: %s", (int)ADC_BUTTON_GPIO, esp_err_to_name(err));
        return false;
    }

    adc_oneshot_unit_init_cfg_t unit_config = {
        .unit_id = s_adc_unit_id,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };

    err = adc_oneshot_new_unit(&unit_config, &s_adc_unit);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to initialize ADC unit %d: %s", (int)s_adc_unit_id, esp_err_to_name(err));
        return false;
    }

    adc_oneshot_chan_cfg_t channel_config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };

    err = adc_oneshot_config_channel(s_adc_unit, s_adc_channel, &channel_config);
    if (err != ESP_OK) {
        ESP_LOGE(
            TAG,
            "failed to configure GPIO%d ADC unit %d channel %d: %s",
            (int)ADC_BUTTON_GPIO,
            (int)s_adc_unit_id,
            (int)s_adc_channel,
            esp_err_to_name(err)
        );
        return false;
    }

    s_initialized = true;
    ESP_LOGI(
        TAG,
        "ADC buttons initialized on GPIO%d, adc%d/ch%d",
        (int)ADC_BUTTON_GPIO,
        (int)s_adc_unit_id + 1,
        (int)s_adc_channel
    );
    return true;
}

ButtonInputKey button_input_poll(int *raw_out) {
    if (!s_initialized) {
        return BUTTON_INPUT_NONE;
    }

    int total = 0;
    int valid_samples = 0;
    for (int i = 0; i < ADC_BUTTON_AVG_SAMPLES; ++i) {
        int raw = 0;
        esp_err_t err = adc_oneshot_read(s_adc_unit, s_adc_channel, &raw);
        if (err == ESP_OK) {
            total += raw;
            valid_samples++;
        } else {
            ESP_LOGW(TAG, "ADC read failed: %s", esp_err_to_name(err));
        }
    }

    if (valid_samples == 0) {
        return s_stable_key;
    }

    int raw_average = total / valid_samples;
    if (raw_out != NULL) {
        *raw_out = raw_average;
    }

    ButtonInputKey measured_key = classify_raw(raw_average);
    if (measured_key == s_stable_key) {
        s_candidate_key = BUTTON_INPUT_NONE;
        s_candidate_count = 0;
        return s_stable_key;
    }

    if (measured_key != s_candidate_key) {
        s_candidate_key = measured_key;
        s_candidate_count = 1;
        return s_stable_key;
    }

    s_candidate_count++;
    if (s_candidate_count >= ADC_BUTTON_DEBOUNCE_SAMPLES) {
        s_stable_key = measured_key;
        s_candidate_key = BUTTON_INPUT_NONE;
        s_candidate_count = 0;
    }

    return s_stable_key;
}

const char *button_input_key_name(ButtonInputKey key) {
    switch (key) {
        case BUTTON_INPUT_KEY_1:
            return "key 1";
        case BUTTON_INPUT_KEY_2:
            return "key 2";
        case BUTTON_INPUT_KEY_3:
            return "key 3";
        case BUTTON_INPUT_NONE:
        default:
            return "no key";
    }
}
