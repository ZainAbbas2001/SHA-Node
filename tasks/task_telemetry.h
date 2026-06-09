/**
 * @file task_telemetry.h
 * @brief Telemetry task — periodic fuel-gauge sampling, JSON publish, haptic watchdog.
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create and pin the telemetry task to core TASK_CORE_TELEMETRY.
 *
 * Stack size and priority are sourced from sha_config.h.
 *
 * @return ESP_OK or ESP_ERR_NO_MEM if task creation fails.
 */
esp_err_t task_telemetry_start(void);

#ifdef __cplusplus
}
#endif
