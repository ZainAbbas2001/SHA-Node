/**
 * @file task_telemetry.c
 * @brief Telemetry task — wakes every 10 s, samples fuel gauge, publishes JSON
 *        via MQTT or queues to SD, and acts as a haptic safety watchdog.
 */

#include "task_telemetry.h"
#include "sha_config.h"
#include "rtc_state.h"
#include "safety.h"
#include "fuel_gauge.h"
#include "haptic.h"
#include "sd_storage.h"
#include "wifi_manager.h"
#include "mqtt_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <stdio.h>
#include <time.h>
#include <inttypes.h>

static const char *TAG = "task_telemetry";

/* --------------------------------------------------------------------------
 * Task implementation
 * -------------------------------------------------------------------------- */

static void telemetry_task_fn(void *arg)
{
    (void)arg;

    TickType_t last_wake = xTaskGetTickCount();
    ESP_LOGI(TAG, "telemetry task running on core %d", xPortGetCoreID());

    while (true) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(FUEL_GAUGE_POLL_MS));

        /* --- Sample fuel gauge ------------------------------------------ */
        fuel_gauge_state_t fg;
        if (fuel_gauge_get_state(&fg, MUTEX_TIMEOUT_MS) != ESP_OK) {
            ESP_LOGW(TAG, "fuel gauge unavailable — skipping telemetry sample");
            goto check_haptic;
        }

        /* --- Build JSON -------------------------------------------------- */
        char json[SD_RECORD_MAX_LEN];
        int  json_len = snprintf(json, sizeof(json),
                                 "{\"ts\":%" PRId64
                                 ",\"soc\":%u"
                                 ",\"v_mv\":%" PRIu32
                                 ",\"boot\":%" PRIu32 "}",
                                 (int64_t)time(NULL),
                                 fg.soc_percent,
                                 fg.cell_voltage_mv,
                                 g_rtc_state.boot_count);

        if (json_len < 0 || json_len >= (int)sizeof(json)) {
            ESP_LOGE(TAG, "JSON truncated — skipping record");
            goto check_haptic;
        }

        /* --- Route: MQTT if connected, SD otherwise --------------------- */
        if (wifi_is_connected()) {
            esp_err_t ret = mqtt_client_publish("telemetry/sha-node", json);
            if (ret == ESP_OK) {
                ESP_LOGD(TAG, "MQTT published: %s", json);
            } else {
                /* Client signalled disconnect — reroute to SD to avoid loss */
                ESP_LOGW(TAG, "MQTT unavailable (ret=%s) — queuing to SD",
                         esp_err_to_name(ret));
                ret = sd_queue_telemetry_record(json);
                if (ret == ESP_OK) {
                    g_rtc_state.pending_records++;
                } else {
                    ESP_LOGE(TAG, "SD queue failed: %s", esp_err_to_name(ret));
                }
            }
        } else {
            esp_err_t ret = sd_queue_telemetry_record(json);
            if (ret == ESP_OK) {
                g_rtc_state.pending_records++;
                ESP_LOGD(TAG, "queued to SD (pending=%" PRIu32 ")",
                         g_rtc_state.pending_records);
            } else {
                ESP_LOGE(TAG, "SD queue failed: %s", esp_err_to_name(ret));
            }
        }

check_haptic:
        /* --- Haptic safety watchdog ------------------------------------- */
        if (!safety_haptic_allowed()) {
            haptic_disable();
        }
    }
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

esp_err_t task_telemetry_start(void)
{
    BaseType_t ret = xTaskCreatePinnedToCore(
        telemetry_task_fn,
        "task_telemetry",
        TASK_STACK_TELEMETRY,
        NULL,
        TASK_PRIORITY_TELEMETRY,
        NULL,
        TASK_CORE_TELEMETRY
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreatePinnedToCore failed");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "task created — priority %d, stack %u B, core %d",
             TASK_PRIORITY_TELEMETRY, TASK_STACK_TELEMETRY, TASK_CORE_TELEMETRY);
    return ESP_OK;
}
