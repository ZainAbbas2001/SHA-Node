/**
 * @file main.c
 * @brief SHA-Node firmware entry point — initialises all subsystems in dependency
 *        order, spawns FreeRTOS tasks, then hands control to the power FSM.
 */

#include "rtc_state.h"
#include "power_fsm.h"
#include "power_rails.h"
#include "fuel_gauge.h"
#include "sd_storage.h"
#include "haptic.h"
#include "mic_dma.h"
#include "wifi_manager.h"
#include "mqtt_client.h"
#include "task_audio.h"
#include "task_telemetry.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include <inttypes.h>
#include <stdbool.h>

static const char *TAG = "main";

void app_main(void)
{
    /* ------------------------------------------------------------------ *
     * Step 1 — RTC state                                                  *
     * Must be first: populates g_rtc_state.boot_count and wake_reason     *
     * before any other subsystem reads them.                              *
     * ------------------------------------------------------------------ */
    rtc_state_load();

    /* ------------------------------------------------------------------ *
     * Step 2 — Power rails                                                *
     * Drive all peripheral supplies LOW before configuring GPIOs to avoid *
     * undefined states on any previously floating rail.                   *
     * ------------------------------------------------------------------ */
    ESP_ERROR_CHECK(power_rails_init());
    power_rails_all_off();

    /* ------------------------------------------------------------------ *
     * Step 3 — NVS                                                        *
     * Required by Wi-Fi, OTA, and any future provisioning flow.          *
     * Erase and reinitialise on corruption rather than halting.          *
     * ------------------------------------------------------------------ */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS corrupt — erasing");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    /* ------------------------------------------------------------------ *
     * Step 4 — Haptic driver                                             *
     * Must be initialised before any call to safety_emergency_flush(),  *
     * which calls haptic_disable() → ledc_set_duty().                   *
     * ------------------------------------------------------------------ */
    err = haptic_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "haptic_init: %s — haptic output disabled",
                 esp_err_to_name(err));
    }

    /* ------------------------------------------------------------------ *
     * Step 5 — Fuel gauge                                                 *
     * Non-fatal: if the MAX17048 is absent, safety functions fall back to *
     * SOC=0 (most-conservative path) and the FSM continues in degraded    *
     * mode without battery-level gating.                                  *
     * ------------------------------------------------------------------ */
    bool fg_degraded = false;
    err = fuel_gauge_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "fuel_gauge_init: %s — degraded mode (no SOC gating)",
                 esp_err_to_name(err));
        fg_degraded = true;
    }

    /* ------------------------------------------------------------------ *
     * Step 5 — SD storage                                                 *
     * Non-fatal: telemetry will not be persisted if the card is absent.  *
     * All sd_* callers already handle ESP_ERR_NOT_FOUND gracefully.      *
     * ------------------------------------------------------------------ */
    bool sd_unavailable = false;
    err = sd_storage_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "sd_storage_init: %s — telemetry buffering disabled",
                 esp_err_to_name(err));
        sd_unavailable = true;
    }

    /* ------------------------------------------------------------------ *
     * Step 6 — Networking                                                 *
     * Wi-Fi and MQTT init are non-blocking; connection is established     *
     * asynchronously via the event loop after this point.                *
     * ------------------------------------------------------------------ */
    ESP_ERROR_CHECK(wifi_manager_init());
    ESP_ERROR_CHECK(mqtt_client_init());

    /* ------------------------------------------------------------------ *
     * Step 7 — FSM resources                                              *
     * Creates the audio EventGroup.  Must happen before any task that     *
     * calls power_fsm_get_audio_event_group().                           *
     * ------------------------------------------------------------------ */
    ESP_ERROR_CHECK(power_fsm_init());

    /* ------------------------------------------------------------------ *
     * Step 8 — I2S / DMA microphone                                     *
     * mic_dma_init() must run before task_audio_start() so that         *
     * mic_dma_get_chunk_queue() returns a valid queue handle.            *
     * ------------------------------------------------------------------ */
    err = mic_dma_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mic_dma_init: %s — audio recording disabled",
                 esp_err_to_name(err));
    }

    /* ------------------------------------------------------------------ *
     * Step 9 — Application tasks                                          *
     * Pinned to dedicated cores; stack sizes and priorities from          *
     * sha_config.h — never hardcoded at the call site.                   *
     * ------------------------------------------------------------------ */
    ESP_ERROR_CHECK(task_audio_start());
    ESP_ERROR_CHECK(task_telemetry_start());

    ESP_LOGI(TAG,
             "boot #%" PRIu32 "  wake=%u  fg=%s  sd=%s — entering FSM",
             g_rtc_state.boot_count,
             g_rtc_state.wake_reason,
             fg_degraded    ? "DEGRADED" : "OK",
             sd_unavailable ? "ABSENT"   : "OK");

    /* ------------------------------------------------------------------ *
     * Step 9 — Power FSM (never returns)                                  *
     *                                                                     *
     * The deep-sleep path is executed inside the FSM's DEEP_SLEEP state: *
     *   safety_emergency_flush()                                          *
     *   power_rails_all_off()                                             *
     *   rtc_state_save()                                                  *
     *   esp_sleep_enable_timer_wakeup(DEEP_SLEEP_DURATION_US)            *
     *   esp_deep_sleep_start()   ← resets the chip                       *
     * ------------------------------------------------------------------ */
    power_fsm_run();
}
