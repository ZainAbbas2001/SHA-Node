/**
 * @file test_fsm.c
 * @brief Unity tests for core/power_fsm.c — init, EventGroup handles, public API.
 *
 * power_fsm_run() never returns and is therefore not invoked here.  Tests
 * focus on the observable, call-safe portion of the FSM public interface.
 *
 * NOTE: power_fsm_init() allocates a new FreeRTOS EventGroup each time
 * without releasing the previous one.  The minor heap consumption over a
 * handful of test cases is acceptable on the target's 512 KB SRAM.
 */

#include "unity.h"
#include "power_fsm.h"
#include "sha_config.h"
#include "test_stubs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

/* ============================================================
 * power_fsm_init()
 * ============================================================ */

TEST_CASE("FSM init returns ESP_OK", "[fsm][init]")
{
    TEST_ASSERT_EQUAL(ESP_OK, power_fsm_init());
}

TEST_CASE("EventGroup handle is non-NULL after init", "[fsm][init]")
{
    TEST_ASSERT_EQUAL(ESP_OK, power_fsm_init());
    TEST_ASSERT_NOT_NULL(power_fsm_get_audio_event_group());
}

TEST_CASE("Repeated init calls each succeed", "[fsm][init]")
{
    TEST_ASSERT_EQUAL(ESP_OK, power_fsm_init());
    TEST_ASSERT_EQUAL(ESP_OK, power_fsm_init());
    TEST_ASSERT_NOT_NULL(power_fsm_get_audio_event_group());
}

/* ============================================================
 * Event-bit definitions (sha_config.h contract)
 * ============================================================ */

TEST_CASE("AUDIO_START_BIT and AUDIO_DONE_BIT are non-zero", "[fsm][bits]")
{
    TEST_ASSERT_NOT_EQUAL(0u, (unsigned)AUDIO_START_BIT);
    TEST_ASSERT_NOT_EQUAL(0u, (unsigned)AUDIO_DONE_BIT);
}

TEST_CASE("AUDIO_START_BIT and AUDIO_DONE_BIT are distinct", "[fsm][bits]")
{
    TEST_ASSERT_NOT_EQUAL(AUDIO_START_BIT, AUDIO_DONE_BIT);
}

TEST_CASE("WIFI_CONNECTED_BIT and WIFI_DISCONNECTED_BIT are non-zero", "[fsm][bits]")
{
    TEST_ASSERT_NOT_EQUAL(0u, (unsigned)WIFI_CONNECTED_BIT);
    TEST_ASSERT_NOT_EQUAL(0u, (unsigned)WIFI_DISCONNECTED_BIT);
}

TEST_CASE("WIFI_CONNECTED_BIT and WIFI_DISCONNECTED_BIT are distinct", "[fsm][bits]")
{
    TEST_ASSERT_NOT_EQUAL(WIFI_CONNECTED_BIT, WIFI_DISCONNECTED_BIT);
}

/* ============================================================
 * power_fsm_set_wifi_connected() / power_fsm_notify_sync_done()
 * ============================================================ */

TEST_CASE("set_wifi_connected true does not crash", "[fsm][api]")
{
    power_fsm_init();
    power_fsm_set_wifi_connected(true);
    TEST_PASS();
}

TEST_CASE("set_wifi_connected false does not crash", "[fsm][api]")
{
    power_fsm_init();
    power_fsm_set_wifi_connected(false);
    TEST_PASS();
}

TEST_CASE("set_wifi_connected toggle does not crash", "[fsm][api]")
{
    power_fsm_init();
    for (int i = 0; i < 4; i++) {
        power_fsm_set_wifi_connected(i % 2 == 0);
    }
    TEST_PASS();
}

TEST_CASE("notify_sync_done does not crash", "[fsm][api]")
{
    power_fsm_init();
    power_fsm_notify_sync_done();
    TEST_PASS();
}

/* ============================================================
 * EventGroup usability — verify FreeRTOS primitives work
 * ============================================================ */

TEST_CASE("AUDIO_START_BIT can be set and cleared on EventGroup", "[fsm][eg]")
{
    power_fsm_init();
    EventGroupHandle_t eg = power_fsm_get_audio_event_group();
    TEST_ASSERT_NOT_NULL(eg);

    xEventGroupSetBits(eg, AUDIO_START_BIT);
    TEST_ASSERT_NOT_EQUAL(0u, xEventGroupGetBits(eg) & AUDIO_START_BIT);

    xEventGroupClearBits(eg, AUDIO_START_BIT);
    TEST_ASSERT_EQUAL(0u, xEventGroupGetBits(eg) & AUDIO_START_BIT);
}

TEST_CASE("AUDIO_DONE_BIT can be set and cleared independently", "[fsm][eg]")
{
    power_fsm_init();
    EventGroupHandle_t eg = power_fsm_get_audio_event_group();
    TEST_ASSERT_NOT_NULL(eg);

    xEventGroupSetBits(eg, AUDIO_START_BIT | AUDIO_DONE_BIT);
    xEventGroupClearBits(eg, AUDIO_START_BIT);

    EventBits_t bits = xEventGroupGetBits(eg);
    TEST_ASSERT_EQUAL(0u,  bits & AUDIO_START_BIT);
    TEST_ASSERT_NOT_EQUAL(0u, bits & AUDIO_DONE_BIT);
}

TEST_CASE("EventGroup is cleared after init", "[fsm][eg]")
{
    power_fsm_init();
    EventGroupHandle_t eg = power_fsm_get_audio_event_group();
    TEST_ASSERT_NOT_NULL(eg);

    /* Newly created EventGroups have all bits 0 */
    TEST_ASSERT_EQUAL(0u, xEventGroupGetBits(eg));
}
