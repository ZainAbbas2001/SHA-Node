/**
 * @file power_fsm.c
 * @brief Power state machine implementation.
 */

#include "power_fsm.h"
#include "rtc_state.h"
#include "safety.h"
#include "sha_config.h"
#include "fuel_gauge.h"
#include "power_rails.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_sleep.h"
#include "esp_log.h"
#include <stdbool.h>
#include <stddef.h>

static const char *TAG = "power_fsm";

/* Sync timeout: how long ACTIVE_SYNC waits before giving up and sleeping */
#define SYNC_TIMEOUT_MS  30000

static const char *STATE_NAMES[] = {
    "BOOT", "ACTIVE_RECORD", "ACTIVE_SYNC", "LOW_POWER_FLUSH", "DEEP_SLEEP"
};

/* --------------------------------------------------------------------------
 * Shared state — written by net/ callbacks, read by FSM
 * -------------------------------------------------------------------------- */

static EventGroupHandle_t s_audio_eg       = NULL;
static volatile bool      s_wifi_connected = false;
static volatile bool      s_sync_done      = false;

/* --------------------------------------------------------------------------
 * Helpers
 * -------------------------------------------------------------------------- */

static void log_transition(power_fsm_state_t from, power_fsm_state_t to)
{
    ESP_LOGI(TAG, "%s → %s", STATE_NAMES[from], STATE_NAMES[to]);
}

static bool soc_below_threshold(void)
{
    fuel_gauge_state_t fg;
    if (fuel_gauge_get_state(&fg, SAFETY_MUTEX_TIMEOUT_MS) != ESP_OK) {
        return false;  /* Fail-safe: assume OK if we can't read */
    }
    return fg.soc_percent < SAFETY_LOW_BATTERY_SOC;
}

/* --------------------------------------------------------------------------
 * State handlers — each returns the NEXT state.
 * Use static flags to track per-state entry; flags reset on departure.
 * -------------------------------------------------------------------------- */

static power_fsm_state_t run_boot(void)
{
    fuel_gauge_state_t fg;
    esp_err_t err = fuel_gauge_get_state(&fg, SAFETY_MUTEX_TIMEOUT_MS);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "fuel gauge unavailable in BOOT — proceeding to ACTIVE_RECORD");
        return FSM_STATE_ACTIVE_RECORD;
    }

    if (fg.soc_percent >= SAFETY_LOW_BATTERY_SOC) {
        return FSM_STATE_ACTIVE_RECORD;
    }

    ESP_LOGW(TAG, "low SOC (%u%%) at boot — skipping record", fg.soc_percent);
    return FSM_STATE_LOW_POWER_FLUSH;
}

static power_fsm_state_t run_active_record(void)
{
    static bool s_triggered = false;

    if (!s_triggered) {
        xEventGroupClearBits(s_audio_eg, AUDIO_DONE_BIT);
        xEventGroupSetBits(s_audio_eg, AUDIO_START_BIT);
        s_triggered = true;
        ESP_LOGI(TAG, "audio recording started");
    }

    /* Poll every 500 ms so the outer loop can catch a mid-cycle low-battery */
    EventBits_t bits = xEventGroupWaitBits(s_audio_eg, AUDIO_DONE_BIT,
                                            pdTRUE,   /* clear on exit */
                                            pdFALSE,
                                            pdMS_TO_TICKS(500));
    if (bits & AUDIO_DONE_BIT) {
        s_triggered = false;
        ESP_LOGI(TAG, "audio recording done");
        return FSM_STATE_ACTIVE_SYNC;
    }

    return FSM_STATE_ACTIVE_RECORD;
}

static power_fsm_state_t run_active_sync(void)
{
    static TickType_t s_entry_tick = 0;

    if (s_entry_tick == 0) {
        s_entry_tick = xTaskGetTickCount();
        ESP_LOGI(TAG, "sync window open (timeout %u s)", SYNC_TIMEOUT_MS / 1000);
    }

    bool timed_out = (xTaskGetTickCount() - s_entry_tick) >= pdMS_TO_TICKS(SYNC_TIMEOUT_MS);

    if (s_sync_done || timed_out) {
        if (timed_out && !s_sync_done) {
            ESP_LOGW(TAG, "sync timed out after %u s", SYNC_TIMEOUT_MS / 1000);
        }
        s_entry_tick = 0;
        s_sync_done  = false;
        return FSM_STATE_DEEP_SLEEP;
    }

    vTaskDelay(pdMS_TO_TICKS(500));
    return FSM_STATE_ACTIVE_SYNC;
}

static power_fsm_state_t run_low_power_flush(void)
{
    ESP_LOGW(TAG, "low-power flush — disabling non-essential peripherals");
    esp_err_t err = safety_emergency_flush();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "emergency flush error: %s", esp_err_to_name(err));
    }
    return FSM_STATE_DEEP_SLEEP;
}

static power_fsm_state_t run_deep_sleep(void)
{
    ESP_LOGI(TAG, "entering deep sleep for %llu s",
             DEEP_SLEEP_DURATION_US / 1000000ULL);

    /* Final safety flush + rail shutdown (idempotent if already done) */
    safety_emergency_flush();
    power_rails_all_off();
    rtc_state_save();

    esp_sleep_enable_timer_wakeup(DEEP_SLEEP_DURATION_US);
    esp_deep_sleep_start();

    /* Unreachable — deep sleep resets the chip */
    return FSM_STATE_BOOT;
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

esp_err_t power_fsm_init(void)
{
    s_audio_eg = xEventGroupCreate();
    if (!s_audio_eg) {
        ESP_LOGE(TAG, "failed to create audio event group");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "FSM resources allocated");
    return ESP_OK;
}

EventGroupHandle_t power_fsm_get_audio_event_group(void)
{
    return s_audio_eg;
}

void power_fsm_set_wifi_connected(bool connected)
{
    s_wifi_connected = connected;
}

void power_fsm_notify_sync_done(void)
{
    s_sync_done = true;
}

void power_fsm_run(void)
{
    if (!s_audio_eg) {
        ESP_LOGE(TAG, "power_fsm_init() was not called — halting");
        for (;;) { vTaskDelay(portMAX_DELAY); }
    }

    power_fsm_state_t state = FSM_STATE_BOOT;
    ESP_LOGI(TAG, "FSM started in state %s", STATE_NAMES[state]);

    while (true) {
        /* Mid-cycle low-battery check — any state except BOOT, FLUSH, SLEEP */
        if (state != FSM_STATE_BOOT &&
            state != FSM_STATE_LOW_POWER_FLUSH &&
            state != FSM_STATE_DEEP_SLEEP) {
            if (soc_below_threshold()) {
                ESP_LOGW(TAG, "mid-cycle low SOC — forcing LOW_POWER_FLUSH");
                log_transition(state, FSM_STATE_LOW_POWER_FLUSH);
                state = FSM_STATE_LOW_POWER_FLUSH;
            }
        }

        power_fsm_state_t next;
        switch (state) {
            case FSM_STATE_BOOT:              next = run_boot();             break;
            case FSM_STATE_ACTIVE_RECORD:     next = run_active_record();    break;
            case FSM_STATE_ACTIVE_SYNC:       next = run_active_sync();      break;
            case FSM_STATE_LOW_POWER_FLUSH:   next = run_low_power_flush();  break;
            case FSM_STATE_DEEP_SLEEP:        next = run_deep_sleep();       break;
            default:
                ESP_LOGE(TAG, "unknown state %d", state);
                next = FSM_STATE_DEEP_SLEEP;
                break;
        }

        if (next != state) {
            log_transition(state, next);
            state = next;
        }
    }
}
