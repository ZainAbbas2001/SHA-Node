/**
 * @file store_forward.c
 * @brief Store-and-forward burst — drains SD queue to MQTT on Wi-Fi reconnect.
 */

#include "store_forward.h"
#include "mqtt_client.h"
#include "sd_storage.h"
#include "rtc_state.h"
#include "power_fsm.h"
#include "sha_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "store_fwd";

/* Guard against spawning multiple burst tasks on rapid reconnect events */
static volatile bool s_burst_running = false;

/* --------------------------------------------------------------------------
 * One-shot burst task — self-deletes when the queue is drained or on error
 * -------------------------------------------------------------------------- */

static void store_forward_task_fn(void *arg)
{
    (void)arg;

    SemaphoreHandle_t puback_sem = mqtt_client_get_puback_sem();
    char record[SD_RECORD_MAX_LEN];
    uint32_t forwarded = 0;
    uint32_t failed    = 0;

    ESP_LOGI(TAG, "burst started — pending=%" PRIu32, g_rtc_state.pending_records);

    while (true) {
        /* Pop the oldest unsynced record */
        esp_err_t ret = sd_pop_telemetry_record(record, sizeof(record));
        if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGI(TAG, "queue empty — burst done (%"PRIu32" forwarded)", forwarded);
            break;
        }
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "sd_pop failed: %s — aborting burst", esp_err_to_name(ret));
            break;
        }

        /* Publish with QoS 1 */
        ret = mqtt_client_publish("telemetry/sha-node", record);
        if (ret != ESP_OK) {
            /* MQTT unavailable mid-burst — put the record back and stop */
            ESP_LOGW(TAG, "MQTT unavailable — re-queuing record and stopping");
            sd_queue_telemetry_record(record);
            failed++;
            break;
        }

        /* Wait for PUBACK — xSemaphoreTake consumes the token given by the
         * MQTT event handler.  The semaphore is shared; a PUBACK from the
         * telemetry task's publish can unblock us here (acceptable for this
         * application because all QoS-1 messages are eventually delivered). */
        if (xSemaphoreTake(puback_sem,
                           pdMS_TO_TICKS(STORE_FWD_PUBACK_TIMEOUT_MS)) != pdTRUE) {
            ESP_LOGW(TAG, "PUBACK timeout — re-queuing record and aborting burst");
            sd_queue_telemetry_record(record);
            failed++;
            break;
        }

        /* Successful delivery */
        if (g_rtc_state.pending_records > 0) {
            g_rtc_state.pending_records--;
        }
        forwarded++;
    }

    if (forwarded > 0 || failed > 0) {
        ESP_LOGI(TAG, "burst complete — forwarded=%"PRIu32" failed=%"PRIu32
                 " pending=%" PRIu32, forwarded, failed, g_rtc_state.pending_records);
    }

    /* Notify FSM that the sync window is done */
    power_fsm_notify_sync_done();

    s_burst_running = false;
    vTaskDelete(NULL);
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

void store_forward_on_reconnect(void)
{
    if (s_burst_running) {
        ESP_LOGD(TAG, "burst already in progress — ignoring reconnect signal");
        return;
    }

    if (g_rtc_state.pending_records == 0) {
        /* Nothing to forward — notify FSM immediately so it can proceed */
        power_fsm_notify_sync_done();
        return;
    }

    s_burst_running = true;

    BaseType_t created = xTaskCreatePinnedToCore(
        store_forward_task_fn, "store_fwd",
        TASK_STACK_STORE_FWD, NULL,
        TASK_PRIORITY_STORE_FWD, NULL,
        TASK_CORE_STORE_FWD
    );

    if (created != pdPASS) {
        ESP_LOGE(TAG, "failed to spawn burst task");
        s_burst_running = false;
        power_fsm_notify_sync_done();
    } else {
        ESP_LOGI(TAG, "burst task spawned — pending=%" PRIu32,
                 g_rtc_state.pending_records);
    }
}
