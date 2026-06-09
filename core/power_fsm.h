/**
 * @file power_fsm.h
 * @brief Power state machine — orchestrates recording, sync, and deep-sleep cycles.
 */

#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** All FSM states.  Exposed for logging and test inspection only. */
typedef enum {
    FSM_STATE_BOOT            = 0,
    FSM_STATE_ACTIVE_RECORD   = 1,
    FSM_STATE_ACTIVE_SYNC     = 2,
    FSM_STATE_LOW_POWER_FLUSH = 3,
    FSM_STATE_DEEP_SLEEP      = 4,
} power_fsm_state_t;

/**
 * @brief Return the EventGroup used to coordinate with task_audio.
 *
 * Bits defined in sha_config.h: AUDIO_START_BIT, AUDIO_DONE_BIT.
 * The EventGroup is created inside power_fsm_run() on first entry;
 * call this only after power_fsm_run() has been reached in app_main.
 *
 * @return EventGroup handle; NULL if power_fsm_run() has not started.
 */
EventGroupHandle_t power_fsm_get_audio_event_group(void);

/**
 * @brief Notify the FSM that a Wi-Fi connection has been established or lost.
 *        Called from net/wifi_manager event handlers — must not block.
 *
 * @param connected  true = IP obtained, false = disconnected.
 */
void power_fsm_set_wifi_connected(bool connected);

/**
 * @brief Notify the FSM that the store-and-forward burst has completed.
 *        Called from net/store_forward when the queue is drained.
 */
void power_fsm_notify_sync_done(void);

/**
 * @brief Allocate FSM resources (EventGroup for audio coordination).
 *
 * Must be called before spawning any tasks that use
 * power_fsm_get_audio_event_group().  Calling power_fsm_run() without
 * calling this first is a programming error.
 *
 * @return ESP_OK, or ESP_ERR_NO_MEM if the EventGroup cannot be allocated.
 */
esp_err_t power_fsm_init(void);

/**
 * @brief Run the power state machine.
 *
 * Contains an internal while(true) loop and never returns under normal
 * operation.  Enters deep sleep directly when the DEEP_SLEEP state is
 * reached, which calls esp_deep_sleep_start().
 *
 * Must be the last call in app_main().
 */
void power_fsm_run(void);

#ifdef __cplusplus
}
#endif
