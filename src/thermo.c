#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_err.h"

#include "thermo.h"

#define MCP9600_BEAN_I2C_ADDR    0x60
#define MCP9600_ENV_I2C_ADDR     0x67
#define MCP9600_REG_HOT_JUNCTION 0x00

#ifndef THERMO_I2C_PORT
#define THERMO_I2C_PORT I2C_NUM_0
#endif

#ifndef THERMO_I2C_SDA_PIN
#define THERMO_I2C_SDA_PIN GPIO_NUM_6
#endif

#ifndef THERMO_I2C_SCL_PIN
#define THERMO_I2C_SCL_PIN GPIO_NUM_7
#endif

#ifndef THERMO_I2C_CLOCK_HZ
#define THERMO_I2C_CLOCK_HZ 100000
#endif

static const char *TAG = "THERMO";
static bool s_i2c_ready = false;

static esp_err_t thermo_i2c_init_once(void);

static esp_err_t thermo_read_mcp9600_hot_junction_c(uint8_t i2c_addr, float *temperature_c)
{
    if (temperature_c == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = thermo_i2c_init_once();
    if (ret != ESP_OK) {
        return ret;
    }

    const uint8_t register_addr = MCP9600_REG_HOT_JUNCTION;
    uint8_t raw_temp_bytes[2] = {0};

    ret = i2c_master_write_read_device(
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

static esp_err_t thermo_i2c_init_once(void)
{
    if (s_i2c_ready) {
        return ESP_OK;
    }

    i2c_config_t cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = THERMO_I2C_SDA_PIN,
        .scl_io_num = THERMO_I2C_SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = THERMO_I2C_CLOCK_HZ,
        .clk_flags = 0,
    };

    esp_err_t ret = i2c_param_config(THERMO_I2C_PORT, &cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_param_config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = i2c_driver_install(THERMO_I2C_PORT, cfg.mode, 0, 0, 0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "i2c_driver_install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_i2c_ready = true;
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
