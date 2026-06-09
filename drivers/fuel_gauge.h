/**
 * @file fuel_gauge.h
 * @brief MAX17048 fuel gauge driver — non-blocking I2C with periodic background refresh.
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Live state snapshot updated every FUEL_GAUGE_POLL_MS by the background task. */
typedef struct {
    uint32_t cell_voltage_mv; /**< Cell voltage in millivolts */
    uint8_t  soc_percent;     /**< State of charge 0–100 % */
    bool     is_charging;     /**< True when charge current is positive */
    uint8_t  alert_flags;     /**< Raw lower byte of MAX17048 STATUS register */
} fuel_gauge_state_t;

/**
 * @brief Initialise the I2C master bus, add the MAX17048 device, and start the
 *        background polling task.  Must be called once from app_main before any
 *        other fuel_gauge_* function.
 *
 * @return ESP_OK on success, ESP_ERR_NO_MEM if task creation fails, or an
 *         I2C driver error if the bus cannot be configured.
 */
esp_err_t fuel_gauge_init(void);

/**
 * @brief Copy the latest fuel gauge state into @p out.
 *
 * Acquires an internal mutex for up to MUTEX_TIMEOUT_MS milliseconds.
 *
 * @param[out] out         Destination struct; must not be NULL.
 * @param      timeout_ms  Maximum milliseconds to wait for the mutex.
 *                         Pass MUTEX_TIMEOUT_MS for normal callers,
 *                         SAFETY_MUTEX_TIMEOUT_MS from safety-critical paths.
 * @return ESP_OK, ESP_ERR_INVALID_ARG if out is NULL, or ESP_ERR_TIMEOUT if
 *         the mutex is not available within the deadline.
 */
esp_err_t fuel_gauge_get_state(fuel_gauge_state_t *out, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
