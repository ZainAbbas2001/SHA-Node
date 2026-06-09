/**
 * @file sd_storage.h
 * @brief SD card storage driver — FATFS over SPI with audio and telemetry queuing.
 */

#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Mount the SD card over SPI and initialise FATFS.
 *
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if no card is inserted,
 *         or a FATFS/SPI error code.
 */
esp_err_t sd_storage_init(void);

/**
 * @brief Append @p len bytes of raw PCM audio to the current rolling session file.
 *
 * A new file named @c /sdcard/audio_<epoch>.pcm is created on the first call
 * after @c sd_flush_queue() closes the previous one, or after power-on.
 *
 * @param buf  Source buffer (may be a DMA pointer).
 * @param len  Number of bytes to write.
 * @return ESP_OK, ESP_ERR_NOT_FOUND if no card, or a FATFS error.
 */
esp_err_t sd_write_audio_chunk(const uint8_t *buf, size_t len);

/**
 * @brief Append a newline-terminated JSON record to @c /sdcard/queue.jsonl.
 *
 * @param json  Null-terminated JSON string (no embedded newlines).
 * @return ESP_OK, ESP_ERR_NOT_FOUND if no card, ESP_ERR_NO_MEM if the queue
 *         has reached SD_QUEUE_MAX_RECORDS.
 */
esp_err_t sd_queue_telemetry_record(const char *json);

/**
 * @brief Read and logically remove the oldest telemetry record.
 *
 * Advances an internal read pointer; physical removal happens during
 * sd_flush_queue().  The returned string has any trailing newline stripped.
 *
 * @param[out] out      Destination buffer.
 * @param      max_len  Size of @p out including the null terminator.
 * @return ESP_OK, ESP_ERR_NOT_FOUND if the queue is empty or no card.
 */
esp_err_t sd_pop_telemetry_record(char *out, size_t max_len);

/**
 * @brief Compact queue.jsonl (discard already-popped records), fsync all open
 *        files, and close the current audio session file.
 *
 * @return ESP_OK or a FATFS error.
 */
esp_err_t sd_flush_queue(void);

#ifdef __cplusplus
}
#endif
