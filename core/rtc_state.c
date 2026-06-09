/**
 * @file rtc_state.c
 * @brief RTC slow-memory state with CRC32 integrity validation.
 */

#include "rtc_state.h"
#include "esp_sleep.h"
#include "esp_rom_crc.h"
#include "esp_log.h"
#include <stddef.h>
#include <string.h>

static const char *TAG = "rtc_state";

/* Definition — RTC_DATA_ATTR places this in RTC slow memory */
RTC_DATA_ATTR system_state_t g_rtc_state;

static const system_state_t DEFAULT_STATE = {
    .boot_count      = 0,
    .pending_records = 0,
    .last_sync_epoch = 0,
    .wake_reason     = 0,
    ._pad            = {0, 0, 0},
    .crc32           = 0,
};

/* Compute CRC32 over every field except crc32 itself */
static uint32_t compute_crc(void)
{
    return esp_rom_crc32_le(0,
                            (const uint8_t *)&g_rtc_state,
                            offsetof(system_state_t, crc32));
}

esp_err_t rtc_state_load(void)
{
    uint32_t expected = compute_crc();

    if (g_rtc_state.crc32 != expected) {
        ESP_LOGW(TAG, "CRC mismatch (stored=0x%08" PRIx32 " expected=0x%08" PRIx32
                 ") — resetting to defaults", g_rtc_state.crc32, expected);
        g_rtc_state           = DEFAULT_STATE;
        g_rtc_state.boot_count = 1;
    } else {
        g_rtc_state.boot_count++;
    }

    g_rtc_state.wake_reason = (uint8_t)esp_sleep_get_wakeup_cause();

    ESP_LOGI(TAG, "boot #%" PRIu32 "  wake_reason=%u  pending_records=%" PRIu32,
             g_rtc_state.boot_count,
             g_rtc_state.wake_reason,
             g_rtc_state.pending_records);

    return ESP_OK;
}

esp_err_t rtc_state_save(void)
{
    g_rtc_state.crc32 = compute_crc();
    ESP_LOGD(TAG, "saved — crc32=0x%08" PRIx32, g_rtc_state.crc32);
    return ESP_OK;
}
