/**
 * @file store_forward.h
 * @brief Store-and-forward — drains SD telemetry queue to MQTT on reconnect.
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Spawn a one-shot task that drains queue.jsonl to MQTT, one record at
 *        a time, waiting for QoS-1 PUBACK between records.
 *
 * Safe to call from a Wi-Fi event handler — non-blocking.  If a forwarding
 * burst is already in progress the call is silently ignored.
 */
void store_forward_on_reconnect(void);

#ifdef __cplusplus
}
#endif
