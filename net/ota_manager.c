/**
 * @file ota_manager.c
 * @brief OTA update — safety-gated esp_https_ota with validation and reboot.
 */

#include "ota_manager.h"
#include "safety.h"
#include "fuel_gauge.h"
#include "sha_config.h"
#include "mqtt_client.h"
#include "driver/gpio.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "esp_ota_ops.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ota_mgr";

esp_err_t ota_start(const char *firmware_url)
{
    /* ------------------------------------------------------------------ *
     * Safety interlock — first line of the function body, no exceptions. *
     * ------------------------------------------------------------------ */
    if (!safety_ota_allowed()) {
        fuel_gauge_state_t fg = { 0 };
        /* Re-read for the log message — ignore error, values are 0 on failure */
        fuel_gauge_get_state(&fg, SAFETY_MUTEX_TIMEOUT_MS);
        ESP_LOGW(TAG,
                 "OTA blocked: insufficient battery (SOC=%u%%, USB=%d)",
                 fg.soc_percent,
                 gpio_get_level(GPIO_VBUS));
        return ESP_ERR_INVALID_STATE;
    }

    if (!firmware_url) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "OTA starting from: %s", firmware_url);

    esp_http_client_config_t http_cfg = {
        .url              = firmware_url,
        .cert_pem         = MQTT_CA_CERT_PEM, /* reuse broker CA for firmware server */
        .keep_alive_enable = true,
        .timeout_ms       = 30000,
    };

    esp_https_ota_config_t ota_cfg = {
        .http_config = &http_cfg,
    };

    esp_err_t ret = esp_https_ota(&ota_cfg);

    if (ret == ESP_ERR_OTA_VALIDATE_FAILED) {
        ESP_LOGE(TAG, "OTA image validation failed — not rebooting");
        return ret;
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_https_ota failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Success — brief delay to allow log output to flush, then reboot */
    ESP_LOGI(TAG, "OTA successful — restarting in 2 s");
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();

    /* Unreachable */
    return ESP_OK;
}
