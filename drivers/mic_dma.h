/**
 * @file mic_dma.h
 * @brief PDM microphone driver — I2S0 with ping-pong DMA and zero-copy ISR delivery.
 */

#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Audio chunk descriptor posted from the DMA ISR to the chunk queue.
 *
 * @p data points directly into the I2S driver's DMA ring buffer — valid only
 * until the next DMA wrap for that descriptor.  Consumers must read all bytes
 * before the ISR fires again for the same descriptor (≈ AUDIO_DMA_BUF_SIZE
 * bytes / sample rate ≈ 128 ms at 16 kHz / 16-bit).
 */
typedef struct {
    void   *data; /**< Pointer into DMA buffer — do NOT free */
    size_t  len;  /**< Valid bytes in this chunk */
} audio_chunk_t;

/**
 * @brief Initialise I2S0 in PDM RX mode with two DMA descriptors (ping-pong).
 *        Creates the audio chunk queue.  Does NOT start DMA transfers.
 *
 * @return ESP_OK or an I2S driver error code.
 */
esp_err_t mic_dma_init(void);

/**
 * @brief Enable the I2S channel and begin DMA transfers.
 *        ISR will start posting audio_chunk_t items to the chunk queue.
 *
 * @return ESP_OK or ESP_ERR_INVALID_STATE if not yet initialised.
 */
esp_err_t mic_dma_start(void);

/**
 * @brief Disable the I2S channel and halt DMA transfers.
 *        Does not destroy the queue — any queued items remain.
 *
 * @return ESP_OK or ESP_ERR_INVALID_STATE if not yet initialised.
 */
esp_err_t mic_dma_stop(void);

/**
 * @brief Return the FreeRTOS queue handle for audio chunk delivery.
 *
 * @return Queue handle, or NULL if mic_dma_init() has not been called.
 */
QueueHandle_t mic_dma_get_chunk_queue(void);

#ifdef __cplusplus
}
#endif
