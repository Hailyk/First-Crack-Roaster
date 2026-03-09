#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"

#include "wifi.h"
//#include "display.h"

static const char *TAG = "First_Crack_ROASTER";

void app_main(void) {
    roaster_state_t roaster_state = {
        .bean_temp = 25.0f,
        .env_temp = 20.0f,
        .exhaust_humidity = 0.05f,
        .air = 0,
        .burner = 0,
        .drum = 0,
    };

    // Initialize display
    //display_init();

    // Create display task for LVGL updates
    //xTaskCreate(display_task, "display_task", 4096, NULL, 5, NULL);

    esp_err_t ret = wifi_start(&roaster_state, "Hidden Network", "KnownSignal");
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Wi-Fi/WebSocket services: %s", esp_err_to_name(ret));
        return;
    }

    while (1) {
        roaster_state.bean_temp += 0.1f;
        roaster_state.env_temp += 0.05f;
        roaster_state.exhaust_humidity += 0.001f;

        if (roaster_state.bean_temp > 250.0f) {
            roaster_state.bean_temp = 25.0f;
        }
        if (roaster_state.env_temp > 250.0f) {
            roaster_state.env_temp = 25.0f;
        }
        if (roaster_state.exhaust_humidity > 1.0f) {
            roaster_state.exhaust_humidity = 1.0f;
        }

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}