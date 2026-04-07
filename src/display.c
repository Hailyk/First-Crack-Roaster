#include <stddef.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/i2c.h"
#include "esp_err.h"
#include "esp_log.h"

#include "display.h"

#define DISPLAY_I2C_ADDR          0x3C
#define DISPLAY_I2C_PORT          I2C_NUM_0
#define DISPLAY_CONTROL_COMMAND   0x80
#define DISPLAY_CONTROL_DATA      0x40
#define DISPLAY_MAX_PAYLOAD_BYTES 64
#define DISPLAY_CMD_SETTLE_MS     1

static const char *TAG = "DISPLAY";

static esp_err_t display_write_with_control(uint8_t control, const uint8_t *payload, size_t payload_len)
{
    if (payload == NULL || payload_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t tx_buffer[DISPLAY_MAX_PAYLOAD_BYTES + 1U] = {0};
    if (payload_len > (sizeof(tx_buffer) - 1U)) {
        return ESP_ERR_INVALID_SIZE;
    }

    tx_buffer[0] = control;
    for (size_t i = 0; i < payload_len; i++) {
        tx_buffer[i + 1U] = payload[i];
    }

    return i2c_master_write_to_device(
        DISPLAY_I2C_PORT,
        DISPLAY_I2C_ADDR,
        tx_buffer,
        payload_len + 1U,
        pdMS_TO_TICKS(100));
}

esp_err_t display_send_command(uint8_t command)
{
    esp_err_t ret = display_write_with_control(DISPLAY_CONTROL_COMMAND, &command, 1);
    if (ret != ESP_OK) {
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(DISPLAY_CMD_SETTLE_MS));
    return ESP_OK;
}

esp_err_t display_send_data(const uint8_t *data, size_t len)
{
    return display_write_with_control(DISPLAY_CONTROL_DATA, data, len);
}

esp_err_t clear_display(void)
{
    return display_send_command(0x01);
}

esp_err_t set_cursor(uint8_t row, uint8_t col)
{
    if (col > 19 || row > 3) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t address = 0;
    if (row == 0) {
        address = col;
    } else if (row == 1) {
        address = 0x40 + col;
    } else if (row == 2) {
        address = 0x14 + col;
    } else if (row == 3) {
        address = 0x54 + col;
    }
    return display_send_command(0x80 | address);
}

esp_err_t display_init(void)
{
    ESP_LOGI(TAG, "Initializing display...");

    vTaskDelay(pdMS_TO_TICKS(500));

    esp_err_t ret = display_send_command(0x38);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Function set failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = display_send_command(0x0C);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Display control failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = display_send_command(0x01);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Clear display failed: %s", esp_err_to_name(ret));
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(15));

    ret = display_send_command(0x06);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Entry mode set failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "CFAH2004AC-TFH-EW initialized at 0x%02X", DISPLAY_I2C_ADDR);
    return ESP_OK;
}
