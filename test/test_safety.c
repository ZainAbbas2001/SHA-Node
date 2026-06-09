/**
 * @file test_safety.c
 * @brief Unity tests for core/safety.c — OTA gate, haptic gate, emergency flush.
 *
 * All hardware is replaced by stubs (see test/stubs/).  Tests run on target
 * via `idf.py -C test flash monitor` with the Unity test runner.
 */

#include "unity.h"
#include "safety.h"
#include "sha_config.h"
#include "test_stubs.h"

/* Reset every stub to a known neutral state before each test case. */
static void reset_all_stubs(void)
{
    stub_fuel_gauge_set_state(50, 4000, false);  /* healthy battery, no error */
    stub_fuel_gauge_set_err(ESP_OK);
    stub_gpio_set_level(GPIO_VBUS, 0);           /* USB not connected */
    stub_haptic_reset();
    stub_sd_reset();
    stub_power_rails_reset();
}

/* ============================================================
 * safety_ota_allowed()
 * ============================================================ */

TEST_CASE("OTA allowed when SOC is at threshold", "[safety][ota]")
{
    reset_all_stubs();
    stub_fuel_gauge_set_state(SAFETY_OTA_MIN_SOC, 3900, false);
    TEST_ASSERT_TRUE(safety_ota_allowed());
}

TEST_CASE("OTA allowed when SOC is well above threshold", "[safety][ota]")
{
    reset_all_stubs();
    stub_fuel_gauge_set_state(90, 4100, true);
    TEST_ASSERT_TRUE(safety_ota_allowed());
}

TEST_CASE("OTA blocked when SOC below threshold and USB absent", "[safety][ota]")
{
    reset_all_stubs();
    stub_fuel_gauge_set_state(SAFETY_OTA_MIN_SOC - 1, 3600, false);
    stub_gpio_set_level(GPIO_VBUS, 0);
    TEST_ASSERT_FALSE(safety_ota_allowed());
}

TEST_CASE("OTA allowed when SOC below threshold but USB present", "[safety][ota]")
{
    reset_all_stubs();
    stub_fuel_gauge_set_state(SAFETY_OTA_MIN_SOC - 1, 3600, false);
    stub_gpio_set_level(GPIO_VBUS, 1);
    TEST_ASSERT_TRUE(safety_ota_allowed());
}

TEST_CASE("OTA blocked when fuel gauge returns error and USB absent", "[safety][ota]")
{
    reset_all_stubs();
    /* On read failure safety.c defaults SOC to 0 (conservative). */
    stub_fuel_gauge_set_err(ESP_ERR_TIMEOUT);
    stub_gpio_set_level(GPIO_VBUS, 0);
    TEST_ASSERT_FALSE(safety_ota_allowed());
}

TEST_CASE("OTA allowed when fuel gauge error but USB present", "[safety][ota]")
{
    reset_all_stubs();
    stub_fuel_gauge_set_err(ESP_ERR_TIMEOUT);
    stub_gpio_set_level(GPIO_VBUS, 1);
    TEST_ASSERT_TRUE(safety_ota_allowed());
}

/* ============================================================
 * safety_haptic_allowed()
 * ============================================================ */

TEST_CASE("Haptic allowed when SOC is at threshold", "[safety][haptic]")
{
    reset_all_stubs();
    stub_fuel_gauge_set_state(SAFETY_HAPTIC_MIN_SOC, 3700, false);
    TEST_ASSERT_TRUE(safety_haptic_allowed());
}

TEST_CASE("Haptic allowed when SOC is well above threshold", "[safety][haptic]")
{
    reset_all_stubs();
    stub_fuel_gauge_set_state(80, 4000, true);
    TEST_ASSERT_TRUE(safety_haptic_allowed());
}

TEST_CASE("Haptic blocked when SOC is below threshold", "[safety][haptic]")
{
    reset_all_stubs();
    stub_fuel_gauge_set_state(SAFETY_HAPTIC_MIN_SOC - 1, 3500, false);
    TEST_ASSERT_FALSE(safety_haptic_allowed());
}

TEST_CASE("Haptic blocked when fuel gauge returns error", "[safety][haptic]")
{
    reset_all_stubs();
    /* On read failure safety.c defaults SOC to 0 — below 15%. */
    stub_fuel_gauge_set_err(ESP_FAIL);
    TEST_ASSERT_FALSE(safety_haptic_allowed());
}

/* ============================================================
 * safety_emergency_flush()
 * ============================================================ */

TEST_CASE("Emergency flush calls haptic_disable", "[safety][flush]")
{
    reset_all_stubs();
    safety_emergency_flush();
    TEST_ASSERT_EQUAL_INT(1, stub_haptic_disable_call_count());
}

TEST_CASE("Emergency flush calls power_rails_disable", "[safety][flush]")
{
    reset_all_stubs();
    safety_emergency_flush();
    TEST_ASSERT_EQUAL_INT(1, stub_power_rails_disable_call_count());
}

TEST_CASE("Emergency flush calls sd_flush_queue", "[safety][flush]")
{
    reset_all_stubs();
    safety_emergency_flush();
    TEST_ASSERT_EQUAL_INT(1, stub_sd_flush_queue_call_count());
}

TEST_CASE("Emergency flush returns ESP_OK when all steps succeed", "[safety][flush]")
{
    reset_all_stubs();
    TEST_ASSERT_EQUAL(ESP_OK, safety_emergency_flush());
}

TEST_CASE("Emergency flush still calls all steps when sd_flush fails", "[safety][flush]")
{
    reset_all_stubs();
    stub_sd_flush_queue_set_result(ESP_ERR_INVALID_STATE);

    esp_err_t ret = safety_emergency_flush();

    /* Returns the first error encountered */
    TEST_ASSERT_NOT_EQUAL(ESP_OK, ret);

    /* All three steps must still execute despite the failure */
    TEST_ASSERT_EQUAL_INT(1, stub_haptic_disable_call_count());
    TEST_ASSERT_EQUAL_INT(1, stub_power_rails_disable_call_count());
    TEST_ASSERT_EQUAL_INT(1, stub_sd_flush_queue_call_count());
}

TEST_CASE("Emergency flush is idempotent on repeated calls", "[safety][flush]")
{
    reset_all_stubs();
    safety_emergency_flush();
    safety_emergency_flush();

    /* Each call must exercise all three steps independently */
    TEST_ASSERT_EQUAL_INT(2, stub_haptic_disable_call_count());
    TEST_ASSERT_EQUAL_INT(2, stub_power_rails_disable_call_count());
    TEST_ASSERT_EQUAL_INT(2, stub_sd_flush_queue_call_count());
}
