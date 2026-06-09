#include "fuel_gauge.h"
#include "test_stubs.h"

static fuel_gauge_state_t s_state = { .soc_percent = 50, .cell_voltage_mv = 4000 };
static esp_err_t          s_err   = ESP_OK;

void stub_fuel_gauge_set_state(uint8_t soc, uint32_t mv, bool charging)
{
    s_state.soc_percent     = soc;
    s_state.cell_voltage_mv = mv;
    s_state.is_charging     = charging;
    s_err = ESP_OK;
}

void stub_fuel_gauge_set_err(esp_err_t err)
{
    s_err = err;
}

/* --- Stub implementations of the driver public API --- */

esp_err_t fuel_gauge_init(void)
{
    return ESP_OK;
}

esp_err_t fuel_gauge_get_state(fuel_gauge_state_t *out, uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (s_err != ESP_OK) {
        return s_err;
    }
    *out = s_state;
    return ESP_OK;
}
