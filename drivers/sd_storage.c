/**
 * @file sd_storage.c
 * @brief SD card storage — FATFS over SPI with audio file management and
 *        offset-tracked telemetry queue.
 */

#include "sd_storage.h"
#include "sha_config.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static const char *TAG = "sd_storage";

#define MOUNT_POINT     "/sdcard"
#define QUEUE_FILE      MOUNT_POINT "/queue.jsonl"
#define QUEUE_TMP_FILE  MOUNT_POINT "/queue.tmp"

static sdmmc_card_t    *s_card        = NULL;
static SemaphoreHandle_t s_mutex;
static FILE            *s_audio_fp    = NULL;
static size_t           s_read_offset = 0;
static bool             s_mounted     = false;

/* --------------------------------------------------------------------------
 * Internal helpers
 * -------------------------------------------------------------------------- */

static inline bool card_present(void) { return s_mounted && s_card != NULL; }

/* Open a new audio session file named by current epoch (or boot timer). */
static esp_err_t open_audio_file(void)
{
    if (s_audio_fp) {
        return ESP_OK; /* Session already open */
    }

    time_t epoch = time(NULL);
    if (epoch < 1000000L) {
        /* SNTP not synced — fall back to microseconds-since-boot as filename */
        epoch = (time_t)(esp_timer_get_time() / 1000000LL);
    }

    char path[64];
    snprintf(path, sizeof(path), MOUNT_POINT "/audio_%lld.pcm", (long long)epoch);

    s_audio_fp = fopen(path, "ab");
    if (!s_audio_fp) {
        ESP_LOGE(TAG, "fopen audio file failed: %s", path);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "opened audio session: %s", path);
    return ESP_OK;
}

/* Compact queue.jsonl by discarding already-popped bytes. */
static esp_err_t compact_queue(void)
{
    if (s_read_offset == 0) {
        return ESP_OK;
    }

    FILE *src = fopen(QUEUE_FILE, "r");
    if (!src) {
        s_read_offset = 0;
        return ESP_OK;
    }

    if (fseek(src, (long)s_read_offset, SEEK_SET) != 0) {
        fclose(src);
        s_read_offset = 0;
        return ESP_OK;
    }

    FILE *dst = fopen(QUEUE_TMP_FILE, "w");
    if (!dst) {
        fclose(src);
        return ESP_FAIL;
    }

    char buf[SD_RECORD_MAX_LEN];
    while (fgets(buf, sizeof(buf), src)) {
        fputs(buf, dst);
    }
    fclose(src);
    fflush(dst);
    fclose(dst);

    remove(QUEUE_FILE);
    rename(QUEUE_TMP_FILE, QUEUE_FILE);
    s_read_offset = 0;

    ESP_LOGD(TAG, "queue compacted");
    return ESP_OK;
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

esp_err_t sd_storage_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        return ESP_ERR_NO_MEM;
    }

    spi_bus_config_t bus_cfg = {
        .mosi_io_num     = GPIO_SD_MOSI,
        .miso_io_num     = GPIO_SD_MISO,
        .sclk_io_num     = GPIO_SD_CLK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = AUDIO_DMA_BUF_SIZE * 2,
    };
    esp_err_t ret = spi_bus_initialize(SD_SPI_HOST, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        /* ESP_ERR_INVALID_STATE means bus already initialised — not fatal */
        ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(ret));
        return ret;
    }

    sdspi_device_config_t slot_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_cfg.gpio_cs = GPIO_SD_CS;
    slot_cfg.host_id = SD_SPI_HOST;

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SD_SPI_HOST;

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files              = 8,
        .allocation_unit_size   = 16 * 1024,
    };

    ret = esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot_cfg, &mount_cfg, &s_card);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "mount failed — card absent or unformatted");
        } else {
            ESP_LOGE(TAG, "esp_vfs_fat_sdspi_mount: %s", esp_err_to_name(ret));
        }
        return ESP_ERR_NOT_FOUND;
    }

    s_mounted = true;
    sdmmc_card_print_info(stdout, s_card);
    ESP_LOGI(TAG, "mounted at " MOUNT_POINT);
    return ESP_OK;
}

esp_err_t sd_write_audio_chunk(const uint8_t *buf, size_t len)
{
    if (!buf || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!card_present()) {
        return ESP_ERR_NOT_FOUND;
    }

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = open_audio_file();
    if (ret == ESP_OK) {
        size_t written = fwrite(buf, 1, len, s_audio_fp);
        if (written != len) {
            ESP_LOGW(TAG, "short write: %zu / %zu bytes", written, len);
            ret = ESP_FAIL;
        }
    }

    xSemaphoreGive(s_mutex);
    return ret;
}

esp_err_t sd_queue_telemetry_record(const char *json)
{
    if (!json) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!card_present()) {
        return ESP_ERR_NOT_FOUND;
    }

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = ESP_OK;
    FILE *f = fopen(QUEUE_FILE, "a");
    if (!f) {
        ESP_LOGE(TAG, "fopen queue.jsonl for append failed");
        ret = ESP_FAIL;
    } else {
        if (fprintf(f, "%s\n", json) < 0) {
            ret = ESP_FAIL;
        }
        fclose(f);
    }

    xSemaphoreGive(s_mutex);
    return ret;
}

esp_err_t sd_pop_telemetry_record(char *out, size_t max_len)
{
    if (!out || max_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!card_present()) {
        return ESP_ERR_NOT_FOUND;
    }

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = ESP_ERR_NOT_FOUND;
    FILE *f = fopen(QUEUE_FILE, "r");
    if (f) {
        if (fseek(f, (long)s_read_offset, SEEK_SET) == 0) {
            long before = ftell(f);
            if (fgets(out, (int)max_len, f) != NULL) {
                long after = ftell(f);
                s_read_offset += (size_t)(after - before);

                /* Strip trailing newline */
                size_t slen = strlen(out);
                if (slen > 0 && out[slen - 1] == '\n') {
                    out[--slen] = '\0';
                }
                ret = ESP_OK;
            }
        }
        fclose(f);
    }

    xSemaphoreGive(s_mutex);
    return ret;
}

esp_err_t sd_flush_queue(void)
{
    if (!card_present()) {
        return ESP_ERR_NOT_FOUND;
    }

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = compact_queue();

    /* Close and sync audio session file */
    if (s_audio_fp) {
        fflush(s_audio_fp);
        fclose(s_audio_fp);
        s_audio_fp = NULL;
        ESP_LOGI(TAG, "audio session closed");
    }

    xSemaphoreGive(s_mutex);
    return ret;
}
