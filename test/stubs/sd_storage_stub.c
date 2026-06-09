#include "sd_storage.h"
#include "test_stubs.h"

static int      s_flush_count  = 0;
static esp_err_t s_flush_result = ESP_OK;

int      stub_sd_flush_queue_call_count(void)         { return s_flush_count; }
void     stub_sd_flush_queue_set_result(esp_err_t e)  { s_flush_result = e; }
void     stub_sd_reset(void) { s_flush_count = 0; s_flush_result = ESP_OK; }

/* --- Stub implementations of the driver public API --- */

esp_err_t sd_storage_init(void)
{
    return ESP_OK;
}

esp_err_t sd_write_audio_chunk(const uint8_t *buf, size_t len)
{
    (void)buf; (void)len;
    return ESP_OK;
}

esp_err_t sd_queue_telemetry_record(const char *json)
{
    (void)json;
    return ESP_OK;
}

esp_err_t sd_pop_telemetry_record(char *out, size_t max_len)
{
    (void)out; (void)max_len;
    return ESP_ERR_NOT_FOUND;
}

esp_err_t sd_flush_queue(void)
{
    s_flush_count++;
    return s_flush_result;
}
