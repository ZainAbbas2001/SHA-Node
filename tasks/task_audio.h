/**
 * @file task_audio.h
 * @brief Audio recording task — PDM capture to SD, EventGroup-driven.
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create and pin the audio task to core TASK_CORE_AUDIO.
 *
 * Must be called after power_fsm_init() so the audio EventGroup exists.
 * Stack size and priority are sourced from sha_config.h.
 *
 * @return ESP_OK or ESP_ERR_NO_MEM if task creation fails.
 */
esp_err_t task_audio_start(void);

#ifdef __cplusplus
}
#endif
