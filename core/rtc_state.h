/**
 * @file rtc_state.h
 * @brief RTC slow-memory state — survives deep sleep, CRC32-validated on wake.
 */

#pragma once

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Persistent system state stored in RTC slow memory.
 *
 * All fields except @c crc32 are covered by the integrity check.
 * Layout must remain stable across firmware versions; add fields at the end.
 */
typedef struct {
    uint32_t boot_count;       /**< Incremented on every valid wake */
    uint32_t pending_records;  /**< Telemetry records queued on SD awaiting upload */
    int64_t  last_sync_epoch;  /**< Unix timestamp of last successful MQTT sync */
    uint8_t  wake_reason;      /**< esp_sleep_source_t value from last wake */
    uint8_t  _pad[3];          /**< Alignment padding — do not use */
    uint32_t crc32;            /**< CRC32 over all preceding bytes */
} system_state_t;

/**
 * @brief Live instance — backed by RTC_DATA_ATTR in rtc_state.c.
 *
 * External modules may read/write fields directly.  Call rtc_state_save()
 * before entering deep sleep to commit the CRC.
 */
extern system_state_t g_rtc_state;

/**
 * @brief Validate the RTC state CRC32.  On mismatch, reset to defaults and
 *        log a warning.  Always increments boot_count and sets wake_reason.
 *
 * @return ESP_OK always (degraded mode resets to safe defaults).
 */
esp_err_t rtc_state_load(void);

/**
 * @brief Recompute CRC32 and write it into g_rtc_state.crc32.
 *        Must be called immediately before esp_deep_sleep_start().
 *
 * @return ESP_OK always.
 */
esp_err_t rtc_state_save(void);

#ifdef __cplusplus
}
#endif
