#include "haptic.h"
#include "test_stubs.h"

static int s_disable_count = 0;

int stub_haptic_disable_call_count(void) { return s_disable_count; }
void stub_haptic_reset(void)             { s_disable_count = 0; }

/* --- Stub implementations of the driver public API --- */

esp_err_t haptic_init(void)
{
    return ESP_OK;
}

esp_err_t haptic_pulse(haptic_pattern_t p)
{
    (void)p;
    return ESP_OK;
}

void haptic_disable(void)
{
    s_disable_count++;
}
