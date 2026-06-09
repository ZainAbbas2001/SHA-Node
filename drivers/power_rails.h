/**
 * @file power_rails.h
 * @brief Peripheral power rail control — GPIO15–18, active-HIGH, ISR-safe.
 */

#pragma once

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Bitmask of power rails.  Multiple rails may be combined with bitwise OR. */
typedef enum {
    RAIL_MIC    = (1 << 0), /**< GPIO_RAIL_MIC   — microphone supply */
    RAIL_SD     = (1 << 1), /**< GPIO_RAIL_SD    — SD card supply    */
    RAIL_HAPTIC = (1 << 2), /**< GPIO_RAIL_HAPTIC — motor supply     */
    RAIL_MODEM  = (1 << 3), /**< GPIO_RAIL_MODEM — modem/Wi-Fi supply */
} rail_mask_t;

/**
 * @brief Configure GPIO15–18 as push-pull outputs and drive all rails LOW.
 *        Must be called once before any other power_rails_* function.
 *
 * @return ESP_OK or a GPIO driver error.
 */
esp_err_t power_rails_init(void);

/**
 * @brief Drive the rails indicated by @p mask HIGH (enable peripheral supply).
 *
 * @param mask  Bitwise OR of rail_mask_t values.
 * @return ESP_OK.
 */
esp_err_t power_rails_enable(rail_mask_t mask);

/**
 * @brief Drive the rails indicated by @p mask LOW (cut peripheral supply).
 *
 * @param mask  Bitwise OR of rail_mask_t values.
 * @return ESP_OK.
 */
esp_err_t power_rails_disable(rail_mask_t mask);

/**
 * @brief Drive all four rails LOW unconditionally.
 *
 * Safe to call from any context — task, ISR, or deep-sleep preparation.
 * Does not acquire any mutex.
 */
void power_rails_all_off(void);

#ifdef __cplusplus
}
#endif
