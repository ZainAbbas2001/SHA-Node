/**
 * @file mqtt_client.c
 * @brief MQTT client — non-blocking publish queue, QoS 1, TLS, PUBACK semaphore.
 */

/* Our wrapper header uses #include_next to pull in the ESP-IDF mqtt_client.h
 * automatically, so a single include gives us both sets of declarations. */
#include "mqtt_client.h"
#include "sha_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include <string.h>
#include <stdbool.h>

static const char *TAG = "mqtt_client";

/* --------------------------------------------------------------------------
 * Internal publish message — copied by value into the queue so the caller's
 * buffer can be reused immediately after mqtt_client_publish() returns.
 * -------------------------------------------------------------------------- */

typedef struct {
    char topic[MQTT_TOPIC_MAX_LEN];
    char payload[SD_RECORD_MAX_LEN];
} mqtt_msg_t;

/* --------------------------------------------------------------------------
 * Module state
 * -------------------------------------------------------------------------- */

static esp_mqtt_client_handle_t s_client;
static QueueHandle_t             s_publish_queue;
static SemaphoreHandle_t         s_puback_sem;
static volatile bool             s_connected   = false;
static bool                      s_initialised = false;

/* --------------------------------------------------------------------------
 * MQTT event handler — runs in esp_event_loop task, not ISR context
 * -------------------------------------------------------------------------- */

static void mqtt_event_handler(void *arg, esp_event_base_t base,
                                int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t ev = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            s_connected = true;
            ESP_LOGI(TAG, "connected to broker");
            break;

        case MQTT_EVENT_DISCONNECTED:
            s_connected = false;
            ESP_LOGW(TAG, "disconnected from broker");
            break;

        case MQTT_EVENT_PUBLISHED:
            /* QoS 1 PUBACK received — unblock any waiter in store_forward */
            xSemaphoreGive(s_puback_sem);
            ESP_LOGD(TAG, "PUBACK msg_id=%d", ev->msg_id);
            break;

        case MQTT_EVENT_ERROR:
            if (ev->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                ESP_LOGE(TAG, "transport error 0x%x",
                         ev->error_handle->esp_tls_last_esp_err);
            }
            break;

        default:
            break;
    }
}

/* --------------------------------------------------------------------------
 * Internal publish task — drains s_publish_queue at TASK_PRIORITY_MQTT
 * -------------------------------------------------------------------------- */

static void mqtt_publish_task_fn(void *arg)
{
    (void)arg;
    mqtt_msg_t msg;

    ESP_LOGI(TAG, "publish task running");

    while (true) {
        if (xQueueReceive(s_publish_queue, &msg, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (!s_connected) {
            ESP_LOGW(TAG, "not connected — dropping queued message on topic %s",
                     msg.topic);
            continue;
        }

        int msg_id = esp_mqtt_client_publish(s_client,
                                              msg.topic,
                                              msg.payload,
                                              0,    /* len=0 uses strlen */
                                              1,    /* QoS 1 */
                                              0);   /* retain=0 */
        if (msg_id < 0) {
            ESP_LOGW(TAG, "esp_mqtt_client_publish failed (not connected?)");
        } else {
            ESP_LOGD(TAG, "published msg_id=%d topic=%s", msg_id, msg.topic);
        }
    }
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

esp_err_t mqtt_client_init(void)
{
    s_publish_queue = xQueueCreate(MQTT_PUBLISH_QUEUE_DEPTH, sizeof(mqtt_msg_t));
    if (!s_publish_queue) {
        return ESP_ERR_NO_MEM;
    }

    s_puback_sem = xSemaphoreCreateBinary();
    if (!s_puback_sem) {
        vQueueDelete(s_publish_queue);
        return ESP_ERR_NO_MEM;
    }

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {
            .address.uri               = MQTT_BROKER_URI,
            .verification.certificate  = MQTT_CA_CERT_PEM,
        },
        .session = {
            .keepalive    = MQTT_KEEPALIVE_S,
            .protocol_ver = MQTT_PROTOCOL_V_3_1_1,
        },
    };

    s_client = esp_mqtt_client_init(&mqtt_cfg);
    if (!s_client) {
        ESP_LOGE(TAG, "esp_mqtt_client_init failed");
        return ESP_FAIL;
    }

    esp_err_t ret = esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID,
                                                    mqtt_event_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "register_event: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_mqtt_client_start(s_client);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_mqtt_client_start: %s", esp_err_to_name(ret));
        return ret;
    }

    BaseType_t created = xTaskCreatePinnedToCore(
        mqtt_publish_task_fn, "mqtt_pub",
        TASK_STACK_MQTT, NULL,
        TASK_PRIORITY_MQTT, NULL,
        TASK_CORE_MQTT
    );
    if (created != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    s_initialised = true;
    ESP_LOGI(TAG, "MQTT client started — broker: %s", MQTT_BROKER_URI);
    return ESP_OK;
}

esp_err_t mqtt_client_publish(const char *topic, const char *payload)
{
    if (!topic || !payload) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialised || !s_connected) {
        return ESP_FAIL;
    }

    mqtt_msg_t msg;
    strlcpy(msg.topic,   topic,   sizeof(msg.topic));
    strlcpy(msg.payload, payload, sizeof(msg.payload));

    /* Non-blocking enqueue — return ESP_FAIL immediately if queue is full */
    if (xQueueSend(s_publish_queue, &msg, 0) != pdTRUE) {
        ESP_LOGW(TAG, "publish queue full — dropping message");
        return ESP_FAIL;
    }
    return ESP_OK;
}

SemaphoreHandle_t mqtt_client_get_puback_sem(void)
{
    return s_puback_sem;
}
