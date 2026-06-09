/**
 * @file mic_dma.c
 * @brief PDM microphone driver — I2S0 ping-pong DMA with zero-copy ISR chunk delivery.
 */

#include "mic_dma.h"
#include "sha_config.h"
#include "driver/i2s_pdm.h"
#include "driver/i2s_common.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_attr.h"

static const char *TAG = "mic_dma";

static i2s_chan_handle_t s_rx_handle;
static QueueHandle_t     s_chunk_queue;
static bool              s_initialised = false;

/* --------------------------------------------------------------------------
 * DMA receive callback — executes in ISR context.
 * Contract: complete in < 1 µs; post pointer only, no memcpy.
 * -------------------------------------------------------------------------- */

static IRAM_ATTR bool on_recv_cb(i2s_chan_handle_t handle,
                                  i2s_event_data_t *event,
                                  void             *user_ctx)
{
    (void)handle;
    (void)user_ctx;

    BaseType_t higher_prio_woken = pdFALSE;

    audio_chunk_t chunk = {
        .data = event->dma_buf,
        .len  = event->size,
    };

    /* Drop if queue full rather than block — audio task should drain faster */
    xQueueSendFromISR(s_chunk_queue, &chunk, &higher_prio_woken);

    return higher_prio_woken == pdTRUE;
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

esp_err_t mic_dma_init(void)
{
    s_chunk_queue = xQueueCreate(AUDIO_CHUNK_QUEUE_DEPTH, sizeof(audio_chunk_t));
    if (!s_chunk_queue) {
        return ESP_ERR_NO_MEM;
    }

    /* Channel config: 2 DMA descriptors (ping-pong), each AUDIO_DMA_BUF_SIZE bytes */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num  = AUDIO_DMA_DESC_COUNT;
    /* dma_frame_num = bytes per descriptor / bytes per sample (2 bytes for 16-bit) */
    chan_cfg.dma_frame_num = AUDIO_DMA_BUF_SIZE / 2;

    esp_err_t ret = i2s_new_channel(&chan_cfg, NULL, &s_rx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel failed: %s", esp_err_to_name(ret));
        vQueueDelete(s_chunk_queue);
        return ret;
    }

    i2s_pdm_rx_config_t pdm_cfg = {
        .clk_cfg  = I2S_PDM_RX_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE_HZ),
        .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                    I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .clk = GPIO_MIC_CLK,
            .din = GPIO_MIC_DATA,
            .invert_flags = {
                .clk_inv = false,
            },
        },
    };

    ret = i2s_channel_init_pdm_rx_mode(s_rx_handle, &pdm_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_init_pdm_rx_mode failed: %s", esp_err_to_name(ret));
        i2s_del_channel(s_rx_handle);
        vQueueDelete(s_chunk_queue);
        return ret;
    }

    /* Register the receive callback — fires from ISR when a DMA descriptor fills */
    i2s_event_callbacks_t cbs = {
        .on_recv          = on_recv_cb,
        .on_recv_q_ovf    = NULL,
        .on_sent          = NULL,
        .on_send_q_ovf    = NULL,
    };
    ret = i2s_channel_register_event_callback(s_rx_handle, &cbs, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "register_event_callback failed: %s", esp_err_to_name(ret));
        i2s_del_channel(s_rx_handle);
        vQueueDelete(s_chunk_queue);
        return ret;
    }

    s_initialised = true;
    ESP_LOGI(TAG, "PDM RX initialised — %u Hz, %u-byte DMA bufs, %u descriptors",
             AUDIO_SAMPLE_RATE_HZ, AUDIO_DMA_BUF_SIZE, AUDIO_DMA_DESC_COUNT);
    return ESP_OK;
}

esp_err_t mic_dma_start(void)
{
    if (!s_initialised) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t ret = i2s_channel_enable(s_rx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_enable failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t mic_dma_stop(void)
{
    if (!s_initialised) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t ret = i2s_channel_disable(s_rx_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_disable failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

QueueHandle_t mic_dma_get_chunk_queue(void)
{
    return s_chunk_queue;
}
