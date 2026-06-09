#include "test_stubs.h"
#include "driver/gpio.h"

/*
 * Configuring the pin as OUTPUT and writing the desired level is the simplest
 * way to make the real gpio_get_level() return a deterministic value without
 * linker wrapping or modifying production code.
 *
 * On a target that has no device on GPIO_VBUS, driving it LOW/HIGH is safe for
 * testing purposes.
 */
void stub_gpio_set_level(int gpio_num, int level)
{
    gpio_config_t cfg = {
        .pin_bit_mask  = (1ULL << gpio_num),
        .mode          = GPIO_MODE_OUTPUT,
        .pull_up_en    = GPIO_PULLUP_DISABLE,
        .pull_down_en  = GPIO_PULLDOWN_DISABLE,
        .intr_type     = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    gpio_set_level((gpio_num_t)gpio_num, (uint32_t)level);
}
