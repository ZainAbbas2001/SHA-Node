/**
 * @file safety.c
 * @brief Safety interlocks — OTA gate, haptic gate, emergency flush.
 *
 * CRITICAL: Do not bypass, inline, or duplicate these checks elsewhere.
 */

#include "safety.h"
#include "sha_config.h"
#include "fuel_gauge.h"
#include "haptic.h"
#include "power_rails.h"
#include "sd_storage.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "safety";

/* --------------------------------------------------------------------------
 * Internal helper — reads fuel gauge with the safety-specific tight timeout.
 * Returns false (conservative/blocking) on read failure.
 * -------------------------------------------------------------------------- */

static bool read_soc(uint8_t *soc_out)
{
    fuel_gauge_state_t fg;
    if (fuel_gauge_get_state(&fg, SAFETY_MUTEX_TIMEOUT_MS) != ESP_OK) {
        ESP_LOGW(TAG, "fuel gauge read failed — assuming SOC=0");
        *soc_out = 0;
        return false;
    }
    *soc_out = fg.soc_percent;
    return true;
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

bool safety_ota_allowed(void)
{
    uint8_t soc = 0;
    read_soc(&soc);

    int vbus = gpio_get_level(GPIO_VBUS);

    if (soc >= SAFETY_OTA_MIN_SOC) {
        return true;
    }
    if (vbus == 1) {
        return true;
    }

    ESP_LOGW(TAG, "OTA blocked: SOC=%u%% (need >=%u%%) USB=%s",
             soc, SAFETY_OTA_MIN_SOC, vbus ? "present" : "absent");
    return false;
}

bool safety_haptic_allowed(void)
{
    uint8_t soc = 0;
    read_soc(&soc);

    if (soc >= SAFETY_HAPTIC_MIN_SOC) {
        return true;
    }

    ESP_LOGD(TAG, "haptic blocked: SOC=%u%% (need >=%u%%)", soc, SAFETY_HAPTIC_MIN_SOC);
    return false;
}

esp_err_t safety_emergency_flush(void)
{
    ESP_LOGW(TAG, "emergency flush initiated");

    esp_err_t first_err = ESP_OK;

    /* 1. Stop haptic motor and set the permanent-disable flag */
    haptic_disable();

    /* 2. Cut power rails for motor and modem — no further blocking I/O */
    esp_err_t err = power_rails_disable((rail_mask_t)(RAIL_HAPTIC | RAIL_MODEM));
    if (err != ESP_OK && first_err == ESP_OK) {
        first_err = err;
        ESP_LOGE(TAG, "power_rails_disable failed: %s", esp_err_to_name(err));
    }

    /* 3. Flush pending telemetry to SD */
    err = sd_flush_queue();
    if (err != ESP_OK && first_err == ESP_OK) {
        first_err = err;
        ESP_LOGE(TAG, "sd_flush_queue failed: %s", esp_err_to_name(err));
    }

    ESP_LOGW(TAG, "emergency flush complete — result: %s", esp_err_to_name(first_err));
    return first_err;
}
