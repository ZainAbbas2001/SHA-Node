/**
 * @file haptic.c
 * @brief Haptic motor driver — LEDC PWM with timer-sequenced patterns.
 */

#include "haptic.h"
#include "sha_config.h"
#include "driver/ledc.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_err.h"
#include <stddef.h>
#include <stdbool.h>

static const char *TAG = "haptic";

/* --------------------------------------------------------------------------
 * Pattern tables — each row is (duty, duration_ms); {0,0} terminates.
 * -------------------------------------------------------------------------- */

typedef struct {
    uint32_t duty;
    uint32_t duration_ms;
} haptic_phase_t;

static const haptic_phase_t SEQ_SHORT_PULSE[] = {
    { HAPTIC_DUTY_ON, 200 },
    { 0, 0 },
};

static const haptic_phase_t SEQ_DOUBLE_PULSE[] = {
    { HAPTIC_DUTY_ON, 100 },
    { 0,              100 },
    { HAPTIC_DUTY_ON, 100 },
    { 0, 0 },
};

static const haptic_phase_t SEQ_LONG_BUZZ[] = {
    { HAPTIC_DUTY_ON, 500 },
    { 0, 0 },
};

/* --------------------------------------------------------------------------
 * Driver state
 * -------------------------------------------------------------------------- */

static esp_timer_handle_t    s_timer;
static const haptic_phase_t *s_sequence;
static int                   s_phase_idx;
static volatile bool         s_blocked = false;
static bool                  s_initialised = false;

/* --------------------------------------------------------------------------
 * Helpers
 * -------------------------------------------------------------------------- */

static void set_duty(uint32_t duty)
{
    ledc_set_duty(LEDC_LOW_SPEED_MODE, HAPTIC_LEDC_CHANNEL, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, HAPTIC_LEDC_CHANNEL);
}

/* Called by esp_timer dispatch task — not in ISR context */
static void haptic_timer_cb(void *arg)
{
    (void)arg;

    if (s_blocked) {
        set_duty(0);
        return;
    }

    s_phase_idx++;
    const haptic_phase_t *phase = &s_sequence[s_phase_idx];

    if (phase->duration_ms == 0) {
        set_duty(0);
        return;
    }

    set_duty(phase->duty);
    esp_timer_start_once(s_timer, (uint64_t)phase->duration_ms * 1000ULL);
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

esp_err_t haptic_init(void)
{
    ledc_timer_config_t timer_cfg = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .timer_num       = HAPTIC_LEDC_TIMER,
        .duty_resolution = (ledc_timer_bit_t)HAPTIC_LEDC_DUTY_RES,
        .freq_hz         = HAPTIC_PWM_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    esp_err_t ret = ledc_timer_config(&timer_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ledc_timer_config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ledc_channel_config_t ch_cfg = {
        .gpio_num   = GPIO_HAPTIC,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = HAPTIC_LEDC_CHANNEL,
        .timer_sel  = HAPTIC_LEDC_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    ret = ledc_channel_config(&ch_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ledc_channel_config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    esp_timer_create_args_t timer_args = {
        .callback        = haptic_timer_cb,
        .arg             = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name            = "haptic_seq",
    };
    ret = esp_timer_create(&timer_args, &s_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_timer_create failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_initialised = true;
    ESP_LOGI(TAG, "initialised — %u Hz, %u-bit resolution", HAPTIC_PWM_FREQ_HZ, HAPTIC_LEDC_DUTY_RES);
    return ESP_OK;
}

esp_err_t haptic_pulse(haptic_pattern_t p)
{
    if (s_blocked) {
        return ESP_ERR_NOT_ALLOWED;
    }
    if (!s_initialised) {
        return ESP_ERR_INVALID_STATE;
    }

    const haptic_phase_t *seq;
    switch (p) {
        case HAPTIC_SHORT_PULSE:  seq = SEQ_SHORT_PULSE;  break;
        case HAPTIC_DOUBLE_PULSE: seq = SEQ_DOUBLE_PULSE; break;
        case HAPTIC_LONG_BUZZ:    seq = SEQ_LONG_BUZZ;    break;
        default:
            return ESP_ERR_INVALID_ARG;
    }

    /* Cancel any in-progress pattern before starting a new one */
    esp_timer_stop(s_timer);

    s_sequence  = seq;
    s_phase_idx = 0;

    set_duty(seq[0].duty);

    if (seq[0].duration_ms > 0) {
        esp_timer_start_once(s_timer, (uint64_t)seq[0].duration_ms * 1000ULL);
    }

    return ESP_OK;
}

void haptic_disable(void)
{
    s_blocked = true;
    esp_timer_stop(s_timer);
    set_duty(0);
    ESP_LOGI(TAG, "haptic permanently disabled for this boot");
}
