#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"

#include "wifi.h"
#include "thermo.h"

static const char *TAG = "First_Crack_Roaster";

void app_main(void) {
    roaster_state_t roaster_state = {
        .bean_temp = 25.0f,
        .env_temp = 20.0f,
        .exhaust_humidity = 0.05f,
        .air = 0,
        .burner = 0,
        .drum = 0,
    };

    vTaskDelay(pdMS_TO_TICKS(3000));

    // Start AP mode and wait for WiFi credentials
    char ssid[33] = {0};
    char password[64] = {0};
    
    ESP_LOGI(TAG, "Starting WiFi configuration portal...");
    ESP_LOGI(TAG, "Connect to WiFi network: First-Crack (password: 12345678)");
    ESP_LOGI(TAG, "Then open browser and go to: http://192.168.1.1");
    
    esp_err_t ret = wifi_start_ap_with_config_portal(ssid, sizeof(ssid), password, sizeof(password));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start configuration portal: %s", esp_err_to_name(ret));
        return;
    }
    
    ESP_LOGI(TAG, "Connecting to WiFi network: %s", ssid);
    
    // Connect to WiFi and start WebSocket server
    ret = wifi_start(&roaster_state, ssid, password);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Wi-Fi/WebSocket services: %s", esp_err_to_name(ret));
        return;
    }

    while (1) {
        roaster_state.exhaust_humidity += 0.001f;

        if (roaster_state.exhaust_humidity > 1.0f) {
            roaster_state.exhaust_humidity = 1.0f;
        }

        if (thermo_read_bean_temperature_c(&roaster_state.bean_temp) != ESP_OK) {
            ESP_LOGW(TAG, "Failed to read bean temperature");
        }

        if (thermo_read_env_temperature_c(&roaster_state.env_temp) != ESP_OK) {
            ESP_LOGW(TAG, "Failed to read environmental temperature");
        }

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}