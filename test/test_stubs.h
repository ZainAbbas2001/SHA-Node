/**
 * @file test_stubs.h
 * @brief Stub control API used by test_safety.c and test_fsm.c.
 *
 * Each stub module provides a small set of "control" functions (stub_*) that
 * tests call to configure the fake hardware state before exercising production
 * code.  The stubs also record call counts so tests can verify that safety.c
 * calls all required steps.
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------------------
 * Fuel gauge stub
 * -------------------------------------------------------------------------- */

/** Set the SOC / voltage / charging state returned by fuel_gauge_get_state(). */
void stub_fuel_gauge_set_state(uint8_t soc_percent, uint32_t cell_mv, bool charging);

/** Force fuel_gauge_get_state() to return @p err on the next call. */
void stub_fuel_gauge_set_err(esp_err_t err);

/* --------------------------------------------------------------------------
 * GPIO stub
 *
 * Configures the requested GPIO pin as an OUTPUT and drives it to @p level so
 * that the real gpio_get_level() — used by safety.c — reads back the value.
 * -------------------------------------------------------------------------- */

/** Drive @p gpio_num to @p level (0 or 1).  Configures pin as output first. */
void stub_gpio_set_level(int gpio_num, int level);

/* --------------------------------------------------------------------------
 * Haptic stub
 * -------------------------------------------------------------------------- */

/** Number of times haptic_disable() has been called since the last reset. */
int stub_haptic_disable_call_count(void);

/** Reset the haptic call counter. */
void stub_haptic_reset(void);

/* --------------------------------------------------------------------------
 * SD storage stub
 * -------------------------------------------------------------------------- */

/** Number of times sd_flush_queue() has been called since the last reset. */
int stub_sd_flush_queue_call_count(void);

/** Set the return value that sd_flush_queue() will return. */
void stub_sd_flush_queue_set_result(esp_err_t err);

/** Reset call counter and result to defaults (ESP_OK). */
void stub_sd_reset(void);

/* --------------------------------------------------------------------------
 * Power rails stub
 * -------------------------------------------------------------------------- */

/** Number of times power_rails_disable() has been called since last reset. */
int stub_power_rails_disable_call_count(void);

/** Number of times power_rails_all_off() has been called since last reset. */
int stub_power_rails_all_off_call_count(void);

/** Reset all power-rails counters. */
void stub_power_rails_reset(void);

#ifdef __cplusplus
}
#endif
