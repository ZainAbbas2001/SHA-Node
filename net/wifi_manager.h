/**
 * @file wifi_manager.h
 * @brief Wi-Fi STA manager — non-blocking init with exponential-backoff reconnect.
 *
 * Implementation: net/wifi_manager.c (Phase 5).
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise the Wi-Fi STA interface and register event handlers.
 *        Non-blocking — connection is established asynchronously.
 *
 * @return ESP_OK or an ESP-IDF Wi-Fi initialisation error.
 */
esp_err_t wifi_manager_init(void);

/**
 * @brief Return true if an IPv4 address has been obtained.
 *        Safe to call from any task context.
 */
bool wifi_is_connected(void);

#ifdef __cplusplus
}
#endif
