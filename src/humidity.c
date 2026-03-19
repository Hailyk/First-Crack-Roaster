#include <stddef.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/i2c.h"
#include "esp_err.h"
#include "esp_log.h"

#include "humidity.h"

#define SHT85_I2C_ADDR                         0x44
#define SHT85_CMD_MEASURE_HIGHREP_NO_STRETCH   0x2400
#define SHT85_MEASUREMENT_WAIT_MS              20

#ifndef HUMIDITY_I2C_PORT
#define HUMIDITY_I2C_PORT I2C_NUM_0
#endif

static const char *TAG = "HUMIDITY";

static uint8_t sht3x_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0xFF;

    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 0x80) {
                crc = (uint8_t)((crc << 1) ^ 0x31);
            } else {
                crc <<= 1;
            }
        }
    }

    return crc;
}

static esp_err_t sht85_read_raw_humidity(uint16_t *raw_humidity)
{
    if (raw_humidity == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t command[2] = {
        (uint8_t)(SHT85_CMD_MEASURE_HIGHREP_NO_STRETCH >> 8),
        (uint8_t)(SHT85_CMD_MEASURE_HIGHREP_NO_STRETCH & 0xFF),
    };

    esp_err_t ret = i2c_master_write_to_device(
        HUMIDITY_I2C_PORT,
        SHT85_I2C_ADDR,
        command,
        sizeof(command),
        pdMS_TO_TICKS(100));
    if (ret != ESP_OK) {
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(SHT85_MEASUREMENT_WAIT_MS));

    uint8_t response[6] = {0};
    ret = i2c_master_read_from_device(
        HUMIDITY_I2C_PORT,
        SHT85_I2C_ADDR,
        response,
        sizeof(response),
        pdMS_TO_TICKS(100));
    if (ret != ESP_OK) {
        return ret;
    }

    if (sht3x_crc8(&response[0], 2) != response[2]) {
        ESP_LOGE(TAG, "SHT85 temperature CRC mismatch");
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (sht3x_crc8(&response[3], 2) != response[5]) {
        ESP_LOGE(TAG, "SHT85 humidity CRC mismatch");
        return ESP_ERR_INVALID_RESPONSE;
    }

    *raw_humidity = ((uint16_t)response[3] << 8) | response[4];
    return ESP_OK;
}

esp_err_t humidity_init(void)
{
    float humidity_percent = 0.0f;
    esp_err_t ret = humidity_read_relative_humidity_percent(&humidity_percent);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SHT85 probe/read failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "SHT85 initialized at 0x%02X (RH=%.2f%%)", SHT85_I2C_ADDR, humidity_percent);
    return ESP_OK;
}

esp_err_t humidity_read_relative_humidity_percent(float *humidity_percent)
{
    if (humidity_percent == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t raw_humidity = 0;
    esp_err_t ret = sht85_read_raw_humidity(&raw_humidity);
    if (ret != ESP_OK) {
        return ret;
    }

    *humidity_percent = ((float)raw_humidity * 100.0f) / 65535.0f;
    if (*humidity_percent < 0.0f) {
        *humidity_percent = 0.0f;
    } else if (*humidity_percent > 100.0f) {
        *humidity_percent = 100.0f;
    }

    return ESP_OK;
}

esp_err_t humidity_update_exhaust_humidity(float *exhaust_humidity)
{
    if (exhaust_humidity == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    float humidity_percent = 0.0f;
    esp_err_t ret = humidity_read_relative_humidity_percent(&humidity_percent);
    if (ret != ESP_OK) {
        return ret;
    }

    *exhaust_humidity = humidity_percent / 100.0f;
    if (*exhaust_humidity < 0.0f) {
        *exhaust_humidity = 0.0f;
    } else if (*exhaust_humidity > 1.0f) {
        *exhaust_humidity = 1.0f;
    }

    return ESP_OK;
}