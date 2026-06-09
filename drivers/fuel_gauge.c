/**
 * @file fuel_gauge.c
 * @brief MAX17048 fuel gauge driver — I2C master with background polling task.
 */

#include "fuel_gauge.h"
#include "sha_config.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "fuel_gauge";

/* MAX17048 register addresses */
#define REG_VCELL   0x02u
#define REG_SOC     0x04u
#define REG_CRATE   0x16u
#define REG_STATUS  0x1Au
#define REG_CONFIG  0x0Cu

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;
static SemaphoreHandle_t       s_mutex;
static fuel_gauge_state_t      s_state;
static bool                    s_initialised = false;

/* --------------------------------------------------------------------------
 * Low-level register I/O
 * -------------------------------------------------------------------------- */

static esp_err_t reg_read(uint8_t reg, uint16_t *val)
{
    uint8_t buf[2];
    esp_err_t ret = i2c_master_transmit_receive(s_dev, &reg, 1, buf, 2,
                                                pdMS_TO_TICKS(MUTEX_TIMEOUT_MS));
    if (ret == ESP_OK) {
        *val = ((uint16_t)buf[0] << 8) | buf[1];
    }
    return ret;
}

static esp_err_t reg_write(uint8_t reg, uint16_t val)
{
    uint8_t buf[3] = { reg, (uint8_t)(val >> 8), (uint8_t)(val & 0xFF) };
    return i2c_master_transmit(s_dev, buf, sizeof(buf),
                               pdMS_TO_TICKS(MUTEX_TIMEOUT_MS));
}

/* --------------------------------------------------------------------------
 * Background polling task
 * -------------------------------------------------------------------------- */

static void fuel_gauge_task(void *arg)
{
    (void)arg;
    TickType_t last_wake = xTaskGetTickCount();

    while (true) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(FUEL_GAUGE_POLL_MS));

        uint16_t vcell_raw, soc_raw, crate_raw, status_raw;
        esp_err_t err = ESP_OK;

        err |= reg_read(REG_VCELL,  &vcell_raw);
        err |= reg_read(REG_SOC,    &soc_raw);
        err |= reg_read(REG_CRATE,  &crate_raw);
        err |= reg_read(REG_STATUS, &status_raw);

        if (err != ESP_OK) {
            ESP_LOGW(TAG, "I2C read error: %s", esp_err_to_name(err));
            continue;
        }

        /* Clear the reset-indicator bit after first read */
        if (status_raw & 0x01u) {
            reg_write(REG_STATUS, status_raw & ~0x01u);
        }

        fuel_gauge_state_t fresh = {
            /* VCELL[15:4] * 1.25 mV; use integer: (raw >> 4) * 125 / 100 */
            .cell_voltage_mv = ((uint32_t)(vcell_raw >> 4) * 125u) / 100u,
            /* SOC[15:8] = whole percent */
            .soc_percent  = (uint8_t)(soc_raw >> 8),
            /* CRATE is a signed 16-bit value; positive → charging */
            .is_charging  = ((int16_t)crate_raw) > 0,
            .alert_flags  = (uint8_t)(status_raw & 0xFFu),
        };

        if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) == pdTRUE) {
            s_state = fresh;
            xSemaphoreGive(s_mutex);
        } else {
            ESP_LOGW(TAG, "mutex timeout on write — dropping sample");
        }
    }
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

esp_err_t fuel_gauge_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        return ESP_ERR_NO_MEM;
    }

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port            = I2C_MASTER_PORT,
        .sda_io_num          = GPIO_I2C_SDA,
        .scl_io_num          = GPIO_I2C_SCL,
        .clk_source          = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt   = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t ret = i2c_new_master_bus(&bus_cfg, &s_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(ret));
        return ret;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = MAX17048_I2C_ADDR,
        .scl_speed_hz    = I2C_MASTER_FREQ_HZ,
    };
    ret = i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_bus_add_device failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Probe the device */
    ret = i2c_master_probe(s_bus, MAX17048_I2C_ADDR, pdMS_TO_TICKS(50));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "MAX17048 not found on I2C bus: %s", esp_err_to_name(ret));
        return ret;
    }

    BaseType_t created = xTaskCreate(
        fuel_gauge_task, "fg_poll",
        TASK_STACK_FUEL_GAUGE,
        NULL,
        TASK_PRIORITY_FUEL_GAUGE,
        NULL
    );
    if (created != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    s_initialised = true;
    ESP_LOGI(TAG, "MAX17048 initialised on I2C%d (SDA=%d, SCL=%d)",
             I2C_MASTER_PORT, GPIO_I2C_SDA, GPIO_I2C_SCL);
    return ESP_OK;
}

esp_err_t fuel_gauge_get_state(fuel_gauge_state_t *out, uint32_t timeout_ms)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    *out = s_state;
    xSemaphoreGive(s_mutex);
    return ESP_OK;
}
