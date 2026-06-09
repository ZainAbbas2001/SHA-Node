/**
 * @file power_rails.c
 * @brief Peripheral power rail control.
 */

#include "power_rails.h"
#include "sha_config.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "power_rails";

/* Mapping from rail_mask_t bit index to GPIO number */
static const int s_rail_gpios[4] = {
    GPIO_RAIL_MIC,
    GPIO_RAIL_SD,
    GPIO_RAIL_HAPTIC,
    GPIO_RAIL_MODEM,
};

esp_err_t power_rails_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << GPIO_RAIL_MIC)    |
                        (1ULL << GPIO_RAIL_SD)      |
                        (1ULL << GPIO_RAIL_HAPTIC)  |
                        (1ULL << GPIO_RAIL_MODEM),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config failed: %s", esp_err_to_name(ret));
        return ret;
    }
    power_rails_all_off();
    ESP_LOGI(TAG, "initialised — all rails OFF");
    return ESP_OK;
}

esp_err_t power_rails_enable(rail_mask_t mask)
{
    for (int i = 0; i < 4; i++) {
        if (mask & (1 << i)) {
            gpio_set_level(s_rail_gpios[i], 1);
        }
    }
    return ESP_OK;
}

esp_err_t power_rails_disable(rail_mask_t mask)
{
    for (int i = 0; i < 4; i++) {
        if (mask & (1 << i)) {
            gpio_set_level(s_rail_gpios[i], 0);
        }
    }
    return ESP_OK;
}

void power_rails_all_off(void)
{
    /* Direct register writes — safe from any context, no mutex required */
    gpio_set_level(GPIO_RAIL_MIC,    0);
    gpio_set_level(GPIO_RAIL_SD,     0);
    gpio_set_level(GPIO_RAIL_HAPTIC, 0);
    gpio_set_level(GPIO_RAIL_MODEM,  0);
}
