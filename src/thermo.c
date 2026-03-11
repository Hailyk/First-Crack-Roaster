#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_err.h"

#include "thermo.h"

#define MCP9600_BEAN_I2C_ADDR       0x60
#define MCP9600_ENV_I2C_ADDR        0x67
#define MCP9600_REG_HOT_JUNCTION    0x00
#define MCP9600_REG_SENSOR_CONFIG   0x05
// Sensor config register bits [6:4]: 000 = K-type, bits [3:0]: filter coefficient (0 = off)
#define MCP9600_SENSOR_CONFIG_K_TYPE 0x00

#ifndef THERMO_I2C_PORT
#define THERMO_I2C_PORT I2C_NUM_0
#endif

static const char *TAG = "THERMO";

// Configures both MCP9600 sensors for K-type thermocouple.
// Call once after the I2C bus has been initialized.
esp_err_t thermo_init(void)
{
    uint8_t sensor_cfg_write[2] = {MCP9600_REG_SENSOR_CONFIG, MCP9600_SENSOR_CONFIG_K_TYPE};
    uint8_t addrs[2] = {MCP9600_BEAN_I2C_ADDR, MCP9600_ENV_I2C_ADDR};
    for (int i = 0; i < 2; i++) {
        esp_err_t ret = i2c_master_write_to_device(THERMO_I2C_PORT, addrs[i], sensor_cfg_write,
                                                    sizeof(sensor_cfg_write), pdMS_TO_TICKS(100));
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "MCP9600 (0x%02X) sensor config write failed: %s", addrs[i], esp_err_to_name(ret));
            return ret;
        }
        ESP_LOGI(TAG, "MCP9600 (0x%02X) configured: K-type thermocouple", addrs[i]);
    }
    return ESP_OK;
}

static esp_err_t thermo_read_mcp9600_hot_junction_c(uint8_t i2c_addr, float *temperature_c)
{
    if (temperature_c == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint8_t register_addr = MCP9600_REG_HOT_JUNCTION;
    uint8_t raw_temp_bytes[2] = {0};

    esp_err_t ret = i2c_master_write_read_device(
        THERMO_I2C_PORT,
        i2c_addr,
        &register_addr,
        1,
        raw_temp_bytes,
        sizeof(raw_temp_bytes),
        pdMS_TO_TICKS(100));

    if (ret != ESP_OK) {
        return ret;
    }

    // MCP9600 hot-junction register is a signed 12.4 fixed-point temperature.
    int16_t fixed_temp = (int16_t)(((uint16_t)raw_temp_bytes[0] << 8) | raw_temp_bytes[1]);
    *temperature_c = (float)fixed_temp / 16.0f;

    return ESP_OK;
}

esp_err_t thermo_read_bean_temperature_c(float *bean_temperature_c)
{
    esp_err_t ret = thermo_read_mcp9600_hot_junction_c(MCP9600_BEAN_I2C_ADDR, bean_temperature_c);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bean MCP9600 (0x%02X) read failed: %s", MCP9600_BEAN_I2C_ADDR, esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

esp_err_t thermo_read_env_temperature_c(float *env_temperature_c)
{
    esp_err_t ret = thermo_read_mcp9600_hot_junction_c(MCP9600_ENV_I2C_ADDR, env_temperature_c);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Env MCP9600 (0x%02X) read failed: %s", MCP9600_ENV_I2C_ADDR, esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}
