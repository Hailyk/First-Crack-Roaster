#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"

#include "driver/i2c.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "motor.h"

#define AS5600_I2C_ADDR          0x36
#define AS5600_REG_RAW_ANGLE     0x0C
#define AS5600_COUNTS_PER_REV    4096
#define MOTOR_RPM_FILTER_ALPHA   0.20f

#ifndef MOTOR_I2C_PORT
#define MOTOR_I2C_PORT I2C_NUM_0
#endif

typedef struct {
	bool initialized;
	uint16_t prev_raw_angle;
	int64_t prev_sample_us;
	float filtered_rpm;
} motor_state_t;

static const char *TAG = "MOTOR";
static motor_state_t s_motor = {0};

static esp_err_t as5600_read_u12(uint8_t register_addr, uint16_t *value_u12)
{
	if (value_u12 == NULL) {
		return ESP_ERR_INVALID_ARG;
	}

	uint8_t raw_data[2] = {0};
	esp_err_t ret = i2c_master_write_read_device(
		MOTOR_I2C_PORT,
		AS5600_I2C_ADDR,
		&register_addr,
		1,
		raw_data,
		sizeof(raw_data),
		pdMS_TO_TICKS(100));
	if (ret != ESP_OK) {
		return ret;
	}

	*value_u12 = (((uint16_t)raw_data[0] << 8) | raw_data[1]) & 0x0FFF;
	return ESP_OK;
}

esp_err_t motor_init(void)
{
	uint16_t first_raw_angle = 0;
	esp_err_t ret = motor_read_raw_angle(&first_raw_angle);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "AS5600 probe/read failed: %s", esp_err_to_name(ret));
		return ret;
	}

	s_motor.prev_raw_angle = first_raw_angle;
	s_motor.prev_sample_us = esp_timer_get_time();
	s_motor.filtered_rpm = 0.0f;
	s_motor.initialized = true;

	ESP_LOGI(TAG, "AS5600 initialized at 0x%02X (raw=%u)", AS5600_I2C_ADDR, first_raw_angle);
	return ESP_OK;
}

esp_err_t motor_read_raw_angle(uint16_t *raw_angle)
{
	return as5600_read_u12(AS5600_REG_RAW_ANGLE, raw_angle);
}

esp_err_t motor_read_angle_deg(float *angle_deg)
{
	if (angle_deg == NULL) {
		return ESP_ERR_INVALID_ARG;
	}

	uint16_t raw_angle = 0;
	esp_err_t ret = motor_read_raw_angle(&raw_angle);
	if (ret != ESP_OK) {
		return ret;
	}

	*angle_deg = ((float)raw_angle * 360.0f) / (float)AS5600_COUNTS_PER_REV;
	return ESP_OK;
}

esp_err_t motor_read_rpm(float *rpm)
{
	if (rpm == NULL) {
		return ESP_ERR_INVALID_ARG;
	}

	uint16_t raw_angle = 0;
	esp_err_t ret = motor_read_raw_angle(&raw_angle);
	if (ret != ESP_OK) {
		return ret;
	}

	int64_t now_us = esp_timer_get_time();
	if (!s_motor.initialized) {
		s_motor.prev_raw_angle = raw_angle;
		s_motor.prev_sample_us = now_us;
		s_motor.filtered_rpm = 0.0f;
		s_motor.initialized = true;
		*rpm = 0.0f;
		return ESP_OK;
	}

	int64_t dt_us = now_us - s_motor.prev_sample_us;
	if (dt_us <= 0) {
		*rpm = s_motor.filtered_rpm;
		return ESP_OK;
	}

	int32_t delta_counts = (int32_t)raw_angle - (int32_t)s_motor.prev_raw_angle;
	if (delta_counts > (AS5600_COUNTS_PER_REV / 2)) {
		delta_counts -= AS5600_COUNTS_PER_REV;
	} else if (delta_counts < -(AS5600_COUNTS_PER_REV / 2)) {
		delta_counts += AS5600_COUNTS_PER_REV;
	}

	float instantaneous_rpm =
		((float)delta_counts * 60.0f * 1000000.0f) /
		((float)AS5600_COUNTS_PER_REV * (float)dt_us);

	// Low-pass filter smooths jitter from quantization and sampling noise.
	s_motor.filtered_rpm =
		(MOTOR_RPM_FILTER_ALPHA * instantaneous_rpm) +
		((1.0f - MOTOR_RPM_FILTER_ALPHA) * s_motor.filtered_rpm);

	s_motor.prev_raw_angle = raw_angle;
	s_motor.prev_sample_us = now_us;
	*rpm = s_motor.filtered_rpm;

	return ESP_OK;
}
