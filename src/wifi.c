#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "cJSON.h"

#include "wifi.h"

static const char *TAG = "WIFI_MODULE";

static roaster_state_t *g_roaster_state = NULL;
static httpd_handle_t server = NULL;

static esp_err_t ws_handler(httpd_req_t *req) {
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "Handshake done, new client connected");
        return ESP_OK;
    }

    if (g_roaster_state == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    httpd_ws_frame_t ws_pkt;
    uint8_t buf[128] = {0};
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.payload = buf;
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, sizeof(buf) - 1);
    if (ret != ESP_OK) {
        return ret;
    }

    ws_pkt.payload[ws_pkt.len < sizeof(buf) - 1 ? ws_pkt.len : sizeof(buf) - 1] = '\0';
    ESP_LOGI(TAG, "Artisan asked: %s", (char *)ws_pkt.payload);

    int request_id = 0;

    cJSON *json = cJSON_Parse((char *)ws_pkt.payload);
    if (json) {
        cJSON *req_id_json = cJSON_GetObjectItemCaseSensitive(json, "MessageID");
        if (cJSON_IsNumber(req_id_json)) {
            request_id = req_id_json->valueint;
        }

        cJSON *air_json = cJSON_GetObjectItemCaseSensitive(json, "air");
        if (cJSON_IsNumber(air_json)) {
            g_roaster_state->air = air_json->valueint;
            ESP_LOGI(TAG, "Updated air to %d", g_roaster_state->air);
        }

        cJSON *burner_json = cJSON_GetObjectItemCaseSensitive(json, "burner");
        if (cJSON_IsNumber(burner_json)) {
            g_roaster_state->burner = burner_json->valueint;
            ESP_LOGI(TAG, "Updated burner to %d", g_roaster_state->burner);
        }

        cJSON *drum_json = cJSON_GetObjectItemCaseSensitive(json, "drum");
        if (cJSON_IsNumber(drum_json)) {
            g_roaster_state->drum = drum_json->valueint;
            ESP_LOGI(TAG, "Updated drum to %d", g_roaster_state->drum);
        }

        cJSON_Delete(json);
    }

    char reply_str[256];
    snprintf(reply_str,
             sizeof(reply_str),
             "{\"MessageID\": %d, \"Machine ID\": 0, \"Data\": {\"BT\": %.1f, \"ET\": %.1f, \"exhaust_humidity\": %.3f, \"air\": %d, \"burner\": %d, \"drum\": %d}}",
             request_id,
             g_roaster_state->bean_temp,
             g_roaster_state->env_temp,
             g_roaster_state->exhaust_humidity * 100,
             g_roaster_state->air,
             g_roaster_state->burner,
             g_roaster_state->drum);

    httpd_ws_frame_t reply_pkt;
    memset(&reply_pkt, 0, sizeof(httpd_ws_frame_t));
    reply_pkt.payload = (uint8_t *)reply_str;
    reply_pkt.len = strlen(reply_str);
    reply_pkt.type = HTTPD_WS_TYPE_TEXT;

    ret = httpd_ws_send_frame(req, &reply_pkt);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Replied with: %s", reply_str);
    }

    return ret;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "Connecting to Wi-Fi...");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "Disconnected. Retrying connection...");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "SUCCESS! Connected. IP Address: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

static esp_err_t wifi_init_sta(void) {
    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        return ret;
    }

    esp_event_handler_instance_t wifi_any_id;
    esp_event_handler_instance_t wifi_got_ip;

    ret = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &wifi_any_id);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &wifi_got_ip);
    if (ret != ESP_OK) {
        return ret;
    }

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = "Hidden Network",
            .password = "KnownSignal",
        },
    };

    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (ret != ESP_OK) {
        return ret;
    }

    return esp_wifi_start();
}

esp_err_t wifi_start(roaster_state_t *state) {
    if (state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    g_roaster_state = state;

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ret = nvs_flash_erase();
        if (ret != ESP_OK) {
            return ret;
        }
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        return ret;
    }

    ret = wifi_init_sta();
    if (ret != ESP_OK) {
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(5000));

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    ret = httpd_start(&server, &config);
    if (ret != ESP_OK) {
        return ret;
    }

    httpd_uri_t ws_uri = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = ws_handler,
        .user_ctx = NULL,
        .is_websocket = true,
    };

    ret = httpd_register_uri_handler(server, &ws_uri);
    if (ret != ESP_OK) {
        httpd_stop(server);
        server = NULL;
        return ret;
    }

    ESP_LOGI(TAG, "WebSocket Server Started!");
    return ESP_OK;
}