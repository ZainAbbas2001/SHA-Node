idf_component_register(
    SRCS
        "test_safety.c"
        "test_fsm.c"
        # Real implementations compiled directly — stubs override driver symbols
        # so we do NOT list (or REQUIRE) the drivers component.
        "${CMAKE_CURRENT_SOURCE_DIR}/../core/safety.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../core/power_fsm.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/../core/rtc_state.c"
        # Hardware stubs — provide all symbols referenced by safety.c / power_fsm.c
        # that would normally come from the drivers component.
        "stubs/fuel_gauge_stub.c"
        "stubs/gpio_stub.c"
        "stubs/haptic_stub.c"
        "stubs/sd_storage_stub.c"
        "stubs/power_rails_stub.c"
    INCLUDE_DIRS
        "."                                            # test_stubs.h
        "${CMAKE_CURRENT_SOURCE_DIR}/../include"       # sha_config.h
        "${CMAKE_CURRENT_SOURCE_DIR}/../core"          # safety.h, power_fsm.h, rtc_state.h
        "${CMAKE_CURRENT_SOURCE_DIR}/../drivers"       # fuel_gauge.h, haptic.h, etc.
    REQUIRES
        unity
        freertos
        log
        driver         # driver/gpio.h used by safety.c
        esp_hw_support # esp_sleep_enable_timer_wakeup, esp_deep_sleep_start
        esp_rom        # esp_rom_crc32_le used by rtc_state.c
        esp_timer
        soc
        hal
)
