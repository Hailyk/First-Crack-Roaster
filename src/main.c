#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "cJSON.h"

static const char *TAG = "First_Crack_ROASTER";

float bean_temp = 25.0;
float env_temp = 20.0;
float exhaust_humidity = 0.05;
int air = 0;            // 0-100
int burner = 0;         // 0-100
int drum = 0;           // 0-100

httpd_handle_t server = NULL;

static esp_err_t ws_handler(httpd_req_t *req) {
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "Handshake done, new client connected");
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt;
    uint8_t buf[128] = {0};
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.payload = buf;
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 128);
    if (ret != ESP_OK) return ret;

    ws_pkt.payload[ws_pkt.len < 127 ? ws_pkt.len : 127] = '\0'; 
    ESP_LOGI(TAG, "Artisan asked: %s", (char*)ws_pkt.payload);

    int request_id = 0; 

    // Parse incoming JSON commands
    cJSON *json = cJSON_Parse((char*)ws_pkt.payload);
    if (json) {
        // Look for the "MessageID" that Artisan is sending
        cJSON *req_id_json = cJSON_GetObjectItemCaseSensitive(json, "MessageID");
        if (cJSON_IsNumber(req_id_json)) {
            request_id = req_id_json->valueint;
        }

        // Check for slider updates
        cJSON *air_json = cJSON_GetObjectItemCaseSensitive(json, "air");
        if (cJSON_IsNumber(air_json)) {
            air = air_json->valueint;
            ESP_LOGI(TAG, "Updated air to %d", air);
        }

        cJSON *burner_json = cJSON_GetObjectItemCaseSensitive(json, "burner");
        if (cJSON_IsNumber(burner_json)) {
            burner = burner_json->valueint;
            ESP_LOGI(TAG, "Updated burner to %d", burner);
        }

        cJSON *drum_json = cJSON_GetObjectItemCaseSensitive(json, "drum");
        if (cJSON_IsNumber(drum_json)) {
            drum = drum_json->valueint;
            ESP_LOGI(TAG, "Updated drum to %d", drum);
        }

        cJSON_Delete(json);
    }

    char reply_str[256];
    
    snprintf(reply_str, sizeof(reply_str), 
             "{\"MessageID\": %d, \"Machine ID\": 0, \"Data\": {\"BT\": %.1f, \"ET\": %.1f, \"exhaust_humidity\": %.3f, \"air\": %d, \"burner\": %d, \"drum\": %d}}", 
             request_id, bean_temp, env_temp, exhaust_humidity * 100, air, burner, drum);

    httpd_ws_frame_t reply_pkt;
    memset(&reply_pkt, 0, sizeof(httpd_ws_frame_t));
    reply_pkt.payload = (uint8_t*)reply_str;
    reply_pkt.len = strlen(reply_str);
    reply_pkt.type = HTTPD_WS_TYPE_TEXT;

    httpd_ws_send_frame(req, &reply_pkt);
    ESP_LOGI(TAG, "Replied with: %s", reply_str);

    return ESP_OK;
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "Connecting to Wi-Fi...");
    } 
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "Disconnected. Retrying connection...");
    } 
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        
        // log the ip
        ESP_LOGI(TAG, "SUCCESS! Connected. IP Address: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

static void wifi_init_sta(void) {
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    // Register our event handler for Wi-Fi and IP events
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = "Hidden Network",
            .password = "KnownSignal",
        },
    };
    
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start(); // This triggers WIFI_EVENT_STA_START in the handler
}

void app_main(void) {
    // Initialize NVS (Required for Wi-Fi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    wifi_init_sta();
    vTaskDelay(pdMS_TO_TICKS(5000)); // Wait for Wi-Fi to connect

    // Start HTTP Server with WebSockets
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t ws_uri = {
            .uri        = "/ws",
            .method     = HTTP_GET,
            .handler    = ws_handler,
            .user_ctx   = NULL,
            .is_websocket = true
        };
        httpd_register_uri_handler(server, &ws_uri);
        ESP_LOGI(TAG, "WebSocket Server Started!");
    }

    while(1) {
        // Simulate sensor updates
        bean_temp += 0.1;
        env_temp += 0.05;
        exhaust_humidity += 0.001;
        if (bean_temp > 250.0) bean_temp = 25.0;
        if (env_temp > 250.0) env_temp = 25.0;
        if (exhaust_humidity > 1.0) exhaust_humidity = 1.0;
        vTaskDelay(pdMS_TO_TICKS(2000)); 
    }
}