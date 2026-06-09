/**
 * @file safety.h
 * @brief Safety interlocks — must not be bypassed, inlined, or duplicated.
 *
 * All callers that need to gate OTA, haptic, or emergency shutdown MUST go
 * through this API.  Functions are synchronous, reentrant, and cap any mutex
 * wait at SAFETY_MUTEX_TIMEOUT_MS.
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Check whether an OTA update is permitted.
 *
 * Permits OTA when: soc_percent >= SAFETY_OTA_MIN_SOC OR USB VBUS is present.
 * Logs the blocking reason when returning false.
 *
 * @return true if OTA may proceed, false otherwise.
 */
bool safety_ota_allowed(void);

/**
 * @brief Check whether haptic output is permitted.
 *
 * Permits haptic when: soc_percent >= SAFETY_HAPTIC_MIN_SOC.
 *
 * @return true if haptic pulse may be triggered, false otherwise.
 */
bool safety_haptic_allowed(void);

/**
 * @brief Emergency shutdown sequence for low-battery conditions.
 *
 * In order: disables haptic PWM + blocked flag, cuts HAPTIC and MODEM power
 * rails, flushes the SD telemetry queue.  All steps execute regardless of
 * individual errors; the first non-OK error code is returned.
 *
 * @return ESP_OK, or the first error encountered during the sequence.
 */
esp_err_t safety_emergency_flush(void);

#ifdef __cplusplus
}
#endif
