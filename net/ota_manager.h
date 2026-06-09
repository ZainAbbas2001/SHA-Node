/**
 * @file ota_manager.h
 * @brief OTA update manager — safety-gated HTTPS OTA with automatic reboot.
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Download and apply a firmware update from @p firmware_url.
 *
 * Calls safety_ota_allowed() as its first action.  If the battery is too low
 * and USB is absent, logs the reason and returns ESP_ERR_INVALID_STATE without
 * touching flash.
 *
 * On success, calls esp_restart() after a 2-second log-flush delay.
 * On ESP_ERR_OTA_VALIDATE_FAILED, logs the error and returns without rebooting.
 *
 * @param firmware_url  Null-terminated HTTPS URL of the firmware binary.
 * @return ESP_OK (unreachable — device reboots), ESP_ERR_INVALID_STATE if
 *         blocked by safety, ESP_ERR_OTA_VALIDATE_FAILED on bad image, or
 *         another esp-https-ota error code.
 */
esp_err_t ota_start(const char *firmware_url);

#ifdef __cplusplus
}
#endif
