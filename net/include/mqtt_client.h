/**
 * @file mqtt_client.h
 * @brief MQTT client wrapper — non-blocking publish queue with TLS.
 *
 * Implementation: net/mqtt_client.c (Phase 5).
 *
 * Header location note:
 *   This file lives at net/include/mqtt_client.h so that the compiler's
 *   #include_next search skips net/include/ and resolves to the ESP-IDF
 *   mqtt_client.h (components/mqtt/include/mqtt_client.h).  A flat placement
 *   at net/mqtt_client.h would put both files in the same -I search tier and
 *   prevent #include_next from finding the ESP-IDF header.
 */

#pragma once

/* Pull in ESP-IDF mqtt types (esp_mqtt_client_handle_t, esp_mqtt_client_config_t,
 * MQTT_EVENT_*, etc.).  #include_next skips the directory where THIS file lives
 * (net/include/) and continues searching, landing on the ESP-IDF header. */
#include_next "mqtt_client.h"

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise the MQTT client and start the internal publish task.
 *        Does not block waiting for a broker connection.
 *
 * @return ESP_OK or an esp-mqtt initialisation error.
 */
esp_err_t mqtt_client_init(void);

/**
 * @brief Enqueue a message for publish at QoS 1.  Returns immediately.
 *
 * @param topic    Null-terminated topic string.
 * @param payload  Null-terminated payload string.
 * @return ESP_OK if enqueued, ESP_FAIL if the client is disconnected or the
 *         internal queue is full.  Callers should reroute to SD on ESP_FAIL.
 */
esp_err_t mqtt_client_publish(const char *topic, const char *payload);

/**
 * @brief Return the binary semaphore that is given each time the broker sends
 *        a PUBACK (QoS 1 acknowledgement).
 *
 * Callers that need per-publish confirmation (e.g. store_forward) should
 * xSemaphoreTake() this semaphore after each call to mqtt_client_publish().
 * The semaphore is created in mqtt_client_init().
 *
 * @return Semaphore handle, or NULL if mqtt_client_init() has not been called.
 */
SemaphoreHandle_t mqtt_client_get_puback_sem(void);

/**
 * @brief Placeholder CA certificate for the MQTT broker TLS connection.
 *
 * Replace this string with the actual CA certificate PEM before flashing
 * production devices.  The PEM must include the BEGIN/END markers and newlines.
 */
#define MQTT_CA_CERT_PEM \
    "-----BEGIN CERTIFICATE-----\n" \
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n" \
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n" \
    "-----END CERTIFICATE-----\n"

#ifdef __cplusplus
}
#endif
