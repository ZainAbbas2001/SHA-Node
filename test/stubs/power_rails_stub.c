#include "power_rails.h"
#include "test_stubs.h"

static int s_disable_count  = 0;
static int s_all_off_count  = 0;

int  stub_power_rails_disable_call_count(void)  { return s_disable_count; }
int  stub_power_rails_all_off_call_count(void)  { return s_all_off_count; }
void stub_power_rails_reset(void) { s_disable_count = 0; s_all_off_count = 0; }

/* --- Stub implementations of the driver public API --- */

esp_err_t power_rails_init(void)
{
    return ESP_OK;
}

esp_err_t power_rails_enable(rail_mask_t mask)
{
    (void)mask;
    return ESP_OK;
}

esp_err_t power_rails_disable(rail_mask_t mask)
{
    (void)mask;
    s_disable_count++;
    return ESP_OK;
}

void power_rails_all_off(void)
{
    s_all_off_count++;
}
