/**
 * @file wifi_manager.c
 * @brief Wi-Fi STA manager — non-blocking init, exponential-backoff reconnect.
 */

#include "wifi_manager.h"
#include "store_forward.h"
#include "power_fsm.h"
#include "sha_config.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <string.h>
#include <stdbool.h>

static const char *TAG = "wifi_mgr";

static esp_netif_t        *s_netif          = NULL;
static esp_timer_handle_t  s_reconnect_tmr;
static EventGroupHandle_t  s_wifi_eg;
static volatile bool       s_connected      = false;
static uint32_t            s_backoff_ms     = WIFI_RECONNECT_BASE_MS;
static bool                s_initialised    = false;

/* --------------------------------------------------------------------------
 * Reconnect timer callback — fires in esp_timer dispatch task (not ISR)
 * -------------------------------------------------------------------------- */

static void reconnect_timer_cb(void *arg)
{
    ESP_LOGI(TAG, "attempting reconnect...");
    esp_wifi_connect();
}

/* --------------------------------------------------------------------------
 * Event handlers — must never call vTaskDelay
 * -------------------------------------------------------------------------- */

static void on_wifi_event(void *arg, esp_event_base_t base,
                          int32_t event_id, void *event_data)
{
    if (event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        return;
    }

    if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        xEventGroupClearBits(s_wifi_eg, WIFI_CONNECTED_BIT);
        xEventGroupSetBits(s_wifi_eg, WIFI_DISCONNECTED_BIT);
        power_fsm_set_wifi_connected(false);

        /* Stop any pending reconnect before re-arming to avoid double-fire */
        esp_timer_stop(s_reconnect_tmr);
        esp_timer_start_once(s_reconnect_tmr, (uint64_t)s_backoff_ms * 1000ULL);

        ESP_LOGW(TAG, "disconnected — reconnect in %" PRIu32 " ms", s_backoff_ms);

        /* Exponential backoff, capped at WIFI_RECONNECT_MAX_MS */
        s_backoff_ms = (s_backoff_ms * 2 >= WIFI_RECONNECT_MAX_MS)
                       ? WIFI_RECONNECT_MAX_MS
                       : s_backoff_ms * 2;
    }
}

static void on_ip_event(void *arg, esp_event_base_t base,
                        int32_t event_id, void *event_data)
{
    if (event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "IP: " IPSTR, IP2STR(&ev->ip_info.ip));

        s_connected  = true;
        s_backoff_ms = WIFI_RECONNECT_BASE_MS;   /* reset backoff on success */

        xEventGroupClearBits(s_wifi_eg, WIFI_DISCONNECTED_BIT);
        xEventGroupSetBits(s_wifi_eg, WIFI_CONNECTED_BIT);
        power_fsm_set_wifi_connected(true);

        /* Drain the SD queue to MQTT in a one-shot background task */
        store_forward_on_reconnect();
    }
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

esp_err_t wifi_manager_init(void)
{
    s_wifi_eg = xEventGroupCreate();
    if (!s_wifi_eg) {
        return ESP_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    s_netif = esp_netif_create_default_wifi_sta();
    if (!s_netif) {
        return ESP_FAIL;
    }

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t ret = esp_wifi_init(&init_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                         ESP_EVENT_ANY_ID,
                                                         on_wifi_event,
                                                         NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                         IP_EVENT_STA_GOT_IP,
                                                         on_ip_event,
                                                         NULL, NULL));

    /* Reconnect timer — one-shot, re-armed by the disconnect handler */
    esp_timer_create_args_t tmr_args = {
        .callback        = reconnect_timer_cb,
        .arg             = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name            = "wifi_reconnect",
    };
    ret = esp_timer_create(&tmr_args, &s_reconnect_tmr);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_timer_create: %s", esp_err_to_name(ret));
        return ret;
    }

    wifi_config_t wifi_cfg = { 0 };
    strlcpy((char *)wifi_cfg.sta.ssid,     WIFI_DEFAULT_SSID,
            sizeof(wifi_cfg.sta.ssid));
    strlcpy((char *)wifi_cfg.sta.password, WIFI_DEFAULT_PASS,
            sizeof(wifi_cfg.sta.password));
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_cfg.sta.pmf_cfg.capable    = true;
    wifi_cfg.sta.pmf_cfg.required   = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    /* Connection is initiated by WIFI_EVENT_STA_START handler */

    s_initialised = true;
    ESP_LOGI(TAG, "STA started — SSID: %s", WIFI_DEFAULT_SSID);
    return ESP_OK;
}

bool wifi_is_connected(void)
{
    return s_connected;
}
