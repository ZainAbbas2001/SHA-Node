/**
 * @file sha_config.h
 * @brief Single source of truth for all SHA-Node compile-time configuration.
 *
 * Every GPIO pin, peripheral parameter, RTOS stack size, priority, and
 * threshold lives here.  No magic numbers anywhere else in the firmware.
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * GPIO assignments
 * ========================================================================= */

/** I2C bus 0 — shared by fuel gauge (MAX17048) */
#define GPIO_I2C_SDA            8
#define GPIO_I2C_SCL            9

/** PDM microphone — I2S0 */
#define GPIO_MIC_DATA           4
#define GPIO_MIC_CLK            5

/** SD card — SPI mode */
#define GPIO_SD_MOSI            11
#define GPIO_SD_MISO            13
#define GPIO_SD_CLK             12
#define GPIO_SD_CS              10

/** Haptic motor — LEDC channel 0 */
#define GPIO_HAPTIC             6

/** Power rail switches — active HIGH */
#define GPIO_RAIL_MIC           15
#define GPIO_RAIL_SD            16
#define GPIO_RAIL_HAPTIC        17
#define GPIO_RAIL_MODEM         18

/** USB VBUS sense — HIGH = USB power present */
#define GPIO_VBUS               21

/* =========================================================================
 * I2C / peripheral bus configuration
 * ========================================================================= */

/** I2C master clock speed for bus 0 (Hz) */
#define I2C_MASTER_FREQ_HZ      400000

/** I2C master bus 0 port number */
#define I2C_MASTER_PORT         0

/** MAX17048 7-bit I2C address */
#define MAX17048_I2C_ADDR       0x36

/* =========================================================================
 * Audio / DMA configuration
 * ========================================================================= */

/** PDM audio sample rate (Hz) */
#define AUDIO_SAMPLE_RATE_HZ    16000

/** DMA buffer size per descriptor (bytes) */
#define AUDIO_DMA_BUF_SIZE      4096

/** Number of DMA descriptors (ping-pong) */
#define AUDIO_DMA_DESC_COUNT    2

/** Depth of the audio chunk queue (number of pointers) */
#define AUDIO_CHUNK_QUEUE_DEPTH 4

/** Recording duration triggered per audio task cycle (milliseconds) */
#define AUDIO_RECORD_DURATION_MS 10000

/* =========================================================================
 * SD / storage configuration
 * ========================================================================= */

/** Maximum telemetry records buffered in queue.jsonl */
#define SD_QUEUE_MAX_RECORDS    512

/** Maximum length of a single JSON telemetry record (bytes, including '\0') */
#define SD_RECORD_MAX_LEN       256

/** SPI host used for SD card */
#define SD_SPI_HOST             SPI2_HOST

/* =========================================================================
 * Haptic motor configuration
 * ========================================================================= */

/** LEDC channel assigned to haptic motor */
#define HAPTIC_LEDC_CHANNEL     0

/** LEDC timer assigned to haptic motor */
#define HAPTIC_LEDC_TIMER       0

/** PWM frequency for haptic motor (Hz) */
#define HAPTIC_PWM_FREQ_HZ      200

/** PWM duty cycle resolution (bits) — matches LEDC_TIMER_10_BIT enum value */
#define HAPTIC_LEDC_DUTY_RES    10

/** Full-on duty value (100 % of 2^10 = 1024) */
#define HAPTIC_DUTY_ON          512   /* ~50 % duty */

/* =========================================================================
 * Networking configuration
 * ========================================================================= */

/** MQTT broker URI — replace before flashing production devices */
#define MQTT_BROKER_URI         "mqtts://broker.example.com:8883"

/** MQTT TLS keep-alive interval (seconds) */
#define MQTT_KEEPALIVE_S        60

/** MQTT publish queue depth (number of messages) */
#define MQTT_PUBLISH_QUEUE_DEPTH 16

/** Wi-Fi reconnect initial backoff (ms) */
#define WIFI_RECONNECT_BASE_MS  1000

/** Wi-Fi reconnect maximum backoff (ms) — 5 minutes */
#define WIFI_RECONNECT_MAX_MS   300000

/* =========================================================================
 * Power / sleep configuration
 * ========================================================================= */

/** Deep sleep duration (microseconds) = 30 seconds */
#define DEEP_SLEEP_DURATION_US  (30ULL * 1000000ULL)

/** SOC threshold below which OTA is blocked (%) when USB absent */
#define SAFETY_OTA_MIN_SOC      30

/** SOC threshold below which haptic output is disabled (%) */
#define SAFETY_HAPTIC_MIN_SOC   15

/** SOC threshold below which LOW_POWER_FLUSH state is entered (%) */
#define SAFETY_LOW_BATTERY_SOC  15

/** Maximum mutex wait before returning ESP_ERR_TIMEOUT (ms) */
#define MUTEX_TIMEOUT_MS        10

/** Maximum mutex wait in safety functions (ms) */
#define SAFETY_MUTEX_TIMEOUT_MS 5

/* =========================================================================
 * FreeRTOS task priorities
 * Higher number = higher priority.  Priority 0 is idle; configMAX_PRIORITIES-1
 * is the maximum.  Reserve the top two for the scheduler internals.
 * ========================================================================= */

#define TASK_PRIORITY_AUDIO         5
#define TASK_PRIORITY_MQTT          4
#define TASK_PRIORITY_STORE_FWD     4
#define TASK_PRIORITY_TELEMETRY     3
#define TASK_PRIORITY_FUEL_GAUGE    2

/* =========================================================================
 * FreeRTOS task stack sizes (bytes)
 * ========================================================================= */

#define TASK_STACK_AUDIO            8192
#define TASK_STACK_MQTT             6144
#define TASK_STACK_STORE_FWD        4096
#define TASK_STACK_TELEMETRY        4096
#define TASK_STACK_FUEL_GAUGE       2048

/* =========================================================================
 * FreeRTOS core affinity
 * ========================================================================= */

#define TASK_CORE_AUDIO             1   /* PRO core — time-critical DMA drain */
#define TASK_CORE_TELEMETRY         0   /* APP core */
#define TASK_CORE_MQTT              0
#define TASK_CORE_STORE_FWD         0

/* =========================================================================
 * EventGroup bit definitions (audio task)
 * ========================================================================= */

#define AUDIO_START_BIT             (1 << 0)
#define AUDIO_DONE_BIT              (1 << 1)

/* =========================================================================
 * EventGroup bit definitions (Wi-Fi)
 * ========================================================================= */

#define WIFI_CONNECTED_BIT          (1 << 0)
#define WIFI_DISCONNECTED_BIT       (1 << 1)

/* =========================================================================
 * Wi-Fi credentials — replace before flashing production devices.
 * Production builds should provision these via NVS at first boot.
 * ========================================================================= */

#define WIFI_DEFAULT_SSID           "your-ssid-here"
#define WIFI_DEFAULT_PASS           "your-password-here"

/* =========================================================================
 * MQTT message constraints
 * ========================================================================= */

/** Maximum MQTT topic string length (bytes, including '\0') */
#define MQTT_TOPIC_MAX_LEN          128

/* =========================================================================
 * Store-and-forward
 * ========================================================================= */

/** Time to wait for a PUBACK before re-queuing and aborting burst (ms) */
#define STORE_FWD_PUBACK_TIMEOUT_MS 5000

/* =========================================================================
 * Fuel gauge polling interval
 * ========================================================================= */

/** How often the fuel gauge task refreshes its readings (ms) */
#define FUEL_GAUGE_POLL_MS          10000

#ifdef __cplusplus
}
#endif
