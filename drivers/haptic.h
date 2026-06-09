/**
 * @file haptic.h
 * @brief Haptic motor driver — LEDC PWM with non-blocking timer-sequenced patterns.
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Available haptic feedback patterns. */
typedef enum {
    HAPTIC_SHORT_PULSE  = 0, /**< Single 200 ms buzz */
    HAPTIC_DOUBLE_PULSE = 1, /**< Two 100 ms buzzes separated by 100 ms silence */
    HAPTIC_LONG_BUZZ    = 2, /**< Single 500 ms buzz */
} haptic_pattern_t;

/**
 * @brief Initialise LEDC timer and channel for haptic output.
 *        Creates the one-shot esp_timer used to sequence pattern phases.
 *
 * @return ESP_OK or an LEDC / esp_timer error code.
 */
esp_err_t haptic_init(void);

/**
 * @brief Play a haptic pattern.  Non-blocking — returns immediately after
 *        setting the first duty cycle and arming the phase timer.
 *
 * Cancels any in-progress pattern before starting the new one.
 *
 * @param p  Pattern to play.
 * @return ESP_OK, ESP_ERR_INVALID_ARG for unknown patterns, or
 *         ESP_ERR_NOT_ALLOWED if haptic_disable() has been called.
 */
esp_err_t haptic_pulse(haptic_pattern_t p);

/**
 * @brief Permanently disable haptic output for this boot cycle.
 *
 * Stops any in-progress pattern, sets duty to 0, and sets an internal flag
 * that causes all subsequent haptic_pulse() calls to return
 * ESP_ERR_NOT_ALLOWED silently.
 */
void haptic_disable(void);

#ifdef __cplusplus
}
#endif
