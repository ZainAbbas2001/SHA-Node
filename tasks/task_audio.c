/**
 * @file task_audio.c
 * @brief Audio recording task — waits for AUDIO_START_BIT, records for
 *        AUDIO_RECORD_DURATION_MS, writes chunks to SD, signals AUDIO_DONE_BIT.
 */

#include "task_audio.h"
#include "sha_config.h"
#include "power_fsm.h"
#include "mic_dma.h"
#include "sd_storage.h"
#include "power_rails.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "esp_log.h"

static const char *TAG = "task_audio";

/* --------------------------------------------------------------------------
 * Task implementation
 * -------------------------------------------------------------------------- */

static void audio_task_fn(void *arg)
{
    (void)arg;

    EventGroupHandle_t eg = power_fsm_get_audio_event_group();
    QueueHandle_t      chunk_q = mic_dma_get_chunk_queue();

    ESP_LOGI(TAG, "audio task running on core %d", xPortGetCoreID());

    while (true) {
        /* Block until the FSM sets AUDIO_START_BIT.
         * This is the self-suspension point described in the spec. */
        EventBits_t bits = xEventGroupWaitBits(eg,
                                                AUDIO_START_BIT,
                                                pdTRUE,    /* clear on exit */
                                                pdFALSE,
                                                portMAX_DELAY);

        if (!(bits & AUDIO_START_BIT)) {
            /* Should never happen with portMAX_DELAY, but handle defensively */
            continue;
        }

        ESP_LOGI(TAG, "recording started (%u ms)", AUDIO_RECORD_DURATION_MS);

        /* Enable mic power rail and allow the LDO to settle */
        power_rails_enable(RAIL_MIC);
        vTaskDelay(pdMS_TO_TICKS(10));

        esp_err_t err = mic_dma_start();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "mic_dma_start failed: %s — aborting session",
                     esp_err_to_name(err));
            power_rails_disable(RAIL_MIC);
            xEventGroupSetBits(eg, AUDIO_DONE_BIT);
            continue;
        }

        /* Drain the DMA chunk queue for AUDIO_RECORD_DURATION_MS */
        TickType_t deadline = xTaskGetTickCount() +
                              pdMS_TO_TICKS(AUDIO_RECORD_DURATION_MS);
        uint32_t   chunks_written = 0;

        while (xTaskGetTickCount() < deadline) {
            audio_chunk_t chunk;
            /* 50 ms receive timeout — re-checks deadline even if no DMA chunks */
            if (xQueueReceive(chunk_q, &chunk, pdMS_TO_TICKS(50)) == pdTRUE) {
                err = sd_write_audio_chunk((const uint8_t *)chunk.data, chunk.len);
                if (err != ESP_OK) {
                    ESP_LOGW(TAG, "sd_write_audio_chunk: %s", esp_err_to_name(err));
                }
                chunks_written++;
            }
        }

        mic_dma_stop();

        /* Flush any chunks that arrived in the DMA pipeline during stop */
        audio_chunk_t tail;
        while (xQueueReceive(chunk_q, &tail, pdMS_TO_TICKS(20)) == pdTRUE) {
            sd_write_audio_chunk((const uint8_t *)tail.data, tail.len);
            chunks_written++;
        }

        power_rails_disable(RAIL_MIC);

        ESP_LOGI(TAG, "recording done — %"PRIu32" chunks written", chunks_written);

        /* Signal the FSM that recording is complete */
        xEventGroupSetBits(eg, AUDIO_DONE_BIT);
        /* Loop returns to xEventGroupWaitBits — task self-suspends */
    }
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

esp_err_t task_audio_start(void)
{
    BaseType_t ret = xTaskCreatePinnedToCore(
        audio_task_fn,
        "task_audio",
        TASK_STACK_AUDIO,
        NULL,
        TASK_PRIORITY_AUDIO,
        NULL,
        TASK_CORE_AUDIO
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "xTaskCreatePinnedToCore failed");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "task created — priority %d, stack %u B, core %d",
             TASK_PRIORITY_AUDIO, TASK_STACK_AUDIO, TASK_CORE_AUDIO);
    return ESP_OK;
}
