#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"

#include "driver/i2c.h"

#include "main.h"
#include "wifi.h"
#include "thermo.h"
#include "motor.h"
#include "humidity.h"
#include "display.h"

static const char *TAG = "First_Crack_Roaster";

#define I2C_MASTER_PORT    I2C_NUM_0
#define I2C_MASTER_SDA_PIN GPIO_NUM_6
#define I2C_MASTER_SCL_PIN GPIO_NUM_7
#define I2C_MASTER_FREQ_HZ 100000

static esp_err_t i2c_master_bus_init(void)
{
    i2c_config_t cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_PIN,
        .scl_io_num = I2C_MASTER_SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_DISABLE,
        .scl_pullup_en = GPIO_PULLUP_DISABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
        .clk_flags = 0,
    };

    esp_err_t ret = i2c_param_config(I2C_MASTER_PORT, &cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_param_config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = i2c_driver_install(I2C_MASTER_PORT, cfg.mode, 0, 0, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_driver_install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "I2C bus initialized: SDA=%d SCL=%d @ %dHz",
             I2C_MASTER_SDA_PIN, I2C_MASTER_SCL_PIN, I2C_MASTER_FREQ_HZ);
    return ESP_OK;
}

void app_main(void) {
    roaster_state_t roaster_state = {
        .bean_temp = 25.0f,
        .env_temp = 20.0f,
        .exhaust_humidity = 0.05f,
        .air = 0,
        .burner = 0,
        .drum = 0,
        .drum_rpm = 0.0f,
        .ip_address = {0},
    };

    vTaskDelay(pdMS_TO_TICKS(3000));

    esp_err_t ret = i2c_master_bus_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2C bus: %s", esp_err_to_name(ret));
        return;
    }

    ret = display_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize display: %s", esp_err_to_name(ret));
        return;
    }

    ret = set_cursor(0, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set display cursor: %s", esp_err_to_name(ret));
        return;
    }

    uint8_t display_text[] = "First Crack Roaster";
    ret = display_send_data(display_text, sizeof(display_text) - 1U);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write startup message to display: %s", esp_err_to_name(ret));
        return;
    }

    ret = thermo_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize thermocouples: %s", esp_err_to_name(ret));
        return;
    }

    ret = motor_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize motor sensor: %s", esp_err_to_name(ret));
        return;
    }

    ret = humidity_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize humidity sensor: %s", esp_err_to_name(ret));
        return;
    }

    motor_set_pwm_percent(0.0f);
    ESP_LOGI(TAG, "Motor set to 0%% PWM");

    // Start AP mode and wait for WiFi credentials
    char ssid[33] = {0};
    char password[64] = {0};
    
    ESP_LOGI(TAG, "Starting WiFi configuration portal...");
    ESP_LOGI(TAG, "Connect to WiFi network: First-Crack (password: 12345678)");
    ESP_LOGI(TAG, "Then open browser and go to: http://192.168.1.1");

    strcpy((char *)display_text, "WiFi: First-Crack");
    ret = set_cursor(1, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set display cursor: %s", esp_err_to_name(ret));
        return;
    }
    ret = display_send_data(display_text, strlen((char *)display_text));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write WiFi instructions to display: %s", esp_err_to_name(ret));
        return;
    }

    strcpy((char *)display_text, "Password: 12345678");
    ret = set_cursor(2, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set display cursor: %s", esp_err_to_name(ret));
        return;
    }
    ret = display_send_data(display_text, strlen((char *)display_text));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write WiFi instructions to display: %s", esp_err_to_name(ret));
        return;
    }

    strcpy((char *)display_text, "http://192.168.1.1");
    ret = set_cursor(3, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set display cursor: %s", esp_err_to_name(ret));
        return;
    }
    ret = display_send_data(display_text, strlen((char *)display_text));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write WiFi instructions to display: %s", esp_err_to_name(ret));
        return;
    }
    
    ret = wifi_start_ap_with_config_portal(ssid, sizeof(ssid), password, sizeof(password));
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

    // clear and reset display after WiFi setup
    display_send_command(0x01); // Clear display
    vTaskDelay(pdMS_TO_TICKS(100));
    display_send_command(0x02); // Return home
    vTaskDelay(pdMS_TO_TICKS(100));

    // setup temp and motor template display
    strcpy((char *)display_text, "Bean:      C");
    ret = set_cursor(0, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set display cursor: %s", esp_err_to_name(ret));
        return;
    }
    ret = display_send_data(display_text, strlen((char *)display_text));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write to display: %s", esp_err_to_name(ret));
        return;
    }

    strcpy((char *)display_text, "Env:       C");
    ret = set_cursor(1, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set display cursor: %s", esp_err_to_name(ret));
        return;
    }
    ret = display_send_data(display_text, strlen((char *)display_text));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write to display: %s", esp_err_to_name(ret));
        return;
    }

    strcpy((char *)display_text, "Motor: 00.0RPM");
    ret = set_cursor(2, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set display cursor: %s", esp_err_to_name(ret));
        return;
    }
    ret = display_send_data(display_text, strlen((char *)display_text));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write to display: %s", esp_err_to_name(ret));
        return;
    }

    char ip_str[20] = {0};
    snprintf(ip_str, sizeof(ip_str), "IP: %u.%u.%u.%u", 
        roaster_state.ip_address[0], 
        roaster_state.ip_address[1], 
        roaster_state.ip_address[2], 
        roaster_state.ip_address[3]);
    strcpy((char *)display_text, ip_str);
    ret = set_cursor(3, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set display cursor: %s", esp_err_to_name(ret));
        return;
    }
    ret = display_send_data(display_text, strlen((char *)display_text));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write to display: %s", esp_err_to_name(ret));
        return;
    }


    bool forward = false;

    while (1) {
        esp_err_t wifi_ret = wifi_process_recovery(&roaster_state);
        if (wifi_ret != ESP_OK) {
            ESP_LOGW(TAG, "Wi-Fi recovery attempt failed: %s", esp_err_to_name(wifi_ret));
        }

        if (thermo_read_bean_temperature_c(&roaster_state.bean_temp) != ESP_OK) {
            ESP_LOGW(TAG, "Failed to read bean temperature");
        }

        if (thermo_read_env_temperature_c(&roaster_state.env_temp) != ESP_OK) {
            ESP_LOGW(TAG, "Failed to read environmental temperature");
        }

        if (humidity_update_exhaust_humidity(&roaster_state.exhaust_humidity) != ESP_OK) {
            ESP_LOGW(TAG, "Failed to read exhaust humidity");
        }

        if (motor_read_rpm(&roaster_state.drum_rpm) != ESP_OK) {
            ESP_LOGW(TAG, "Failed to read drum RPM");
        }

        float test_pwm = forward ? 55.0f : -55.0f;
        if (motor_set_pwm_percent(test_pwm) != ESP_OK) {
            ESP_LOGW(TAG, "Failed to set motor PWM to %.1f%%", test_pwm);
        }
        forward = !forward;

        snprintf((char *)display_text, sizeof(display_text), "%05.1f", roaster_state.bean_temp);
        ret = set_cursor(0, 6);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set display cursor: %s", esp_err_to_name(ret));
            return;
        }
        ret = display_send_data(display_text, strlen((char *)display_text));
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to write bean temp to display: %s", esp_err_to_name(ret));
            return;
        }

        snprintf((char *)display_text, sizeof(display_text), "%05.1f", roaster_state.env_temp);
        ret = set_cursor(1, 6);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set display cursor: %s", esp_err_to_name(ret));
            return;
        }
        ret = display_send_data(display_text, strlen((char *)display_text));
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to write env temp to display: %s", esp_err_to_name(ret));
            return;
        }

        if (roaster_state.drum_rpm < 0.0f) {
            ret = set_cursor(2, 6);
            snprintf((char *)display_text, sizeof(display_text), "%05.1f", roaster_state.drum_rpm);
        }
        else {
            ret = set_cursor(2, 7);
            snprintf((char *)display_text, sizeof(display_text), "%04.1f", roaster_state.drum_rpm);
        }
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set display cursor: %s", esp_err_to_name(ret));
            return;
        }
        ret = display_send_data(display_text, strlen((char *)display_text));
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to write drum RPM to display: %s", esp_err_to_name(ret));
            return;
        }

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}