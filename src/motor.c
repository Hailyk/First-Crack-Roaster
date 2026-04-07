#include <stdbool.h>
#include <stdint.h>
#include <math.h>

#include "freertos/FreeRTOS.h"

#include "driver/i2c.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "motor.h"

#define AS5600_I2C_ADDR          0x36
#define AS5600_REG_RAW_ANGLE     0x0C
#define AS5600_COUNTS_PER_REV    4096
#define MOTOR_RPM_FILTER_ALPHA   0.20f

#define MOTOR_PWM_FWD_GPIO       GPIO_NUM_3
#define MOTOR_PWM_REV_GPIO       GPIO_NUM_4
#define MOTOR_PWM_FREQ_HZ        20000
#define MOTOR_PWM_LEDC_MODE      LEDC_LOW_SPEED_MODE
#define MOTOR_PWM_LEDC_TIMER     LEDC_TIMER_0
#define MOTOR_PWM_FWD_CHANNEL    LEDC_CHANNEL_0
#define MOTOR_PWM_REV_CHANNEL    LEDC_CHANNEL_1
#define MOTOR_PWM_DUTY_RES       LEDC_TIMER_10_BIT
#define MOTOR_PWM_DUTY_MAX       ((1U << 10) - 1U)

#define MOTOR_RPM_KP             0.10f

#ifndef MOTOR_I2C_PORT
#define MOTOR_I2C_PORT I2C_NUM_0
#endif

typedef struct {
	bool initialized;
	bool pwm_initialized;
	uint16_t prev_raw_angle;
	int64_t prev_sample_us;
	float filtered_rpm;
	float pwm_percent;
	float target_rpm;
} motor_state_t;

static const char *TAG = "MOTOR";
static motor_state_t s_motor = {0};

static esp_err_t motor_pwm_init(void)
{
	ledc_timer_config_t timer_cfg = {
		.speed_mode = MOTOR_PWM_LEDC_MODE,
		.timer_num = MOTOR_PWM_LEDC_TIMER,
		.duty_resolution = MOTOR_PWM_DUTY_RES,
		.freq_hz = MOTOR_PWM_FREQ_HZ,
		.clk_cfg = LEDC_AUTO_CLK,
	};

	esp_err_t ret = ledc_timer_config(&timer_cfg);
	if (ret != ESP_OK) {
		return ret;
	}

	ledc_channel_config_t fwd_channel_cfg = {
		.gpio_num = MOTOR_PWM_FWD_GPIO,
		.speed_mode = MOTOR_PWM_LEDC_MODE,
		.channel = MOTOR_PWM_FWD_CHANNEL,
		.intr_type = LEDC_INTR_DISABLE,
		.timer_sel = MOTOR_PWM_LEDC_TIMER,
		.duty = 0,
		.hpoint = 0,
	};

	ret = ledc_channel_config(&fwd_channel_cfg);
	if (ret != ESP_OK) {
		return ret;
	}

	ledc_channel_config_t rev_channel_cfg = {
		.gpio_num = MOTOR_PWM_REV_GPIO,
		.speed_mode = MOTOR_PWM_LEDC_MODE,
		.channel = MOTOR_PWM_REV_CHANNEL,
		.intr_type = LEDC_INTR_DISABLE,
		.timer_sel = MOTOR_PWM_LEDC_TIMER,
		.duty = 0,
		.hpoint = 0,
	};

	ret = ledc_channel_config(&rev_channel_cfg);
	if (ret != ESP_OK) {
		return ret;
	}

	s_motor.pwm_percent = 0.0f;
	s_motor.target_rpm = 0.0f;
	s_motor.pwm_initialized = true;
	return ESP_OK;
}

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
	esp_err_t ret = motor_pwm_init();
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "PWM init failed on GPIO%d/GPIO%d: %s", MOTOR_PWM_FWD_GPIO, MOTOR_PWM_REV_GPIO, esp_err_to_name(ret));
		return ret;
	}

	uint16_t first_raw_angle = 0;
	ret = motor_read_raw_angle(&first_raw_angle);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "AS5600 probe/read failed: %s", esp_err_to_name(ret));
		return ret;
	}

	s_motor.prev_raw_angle = first_raw_angle;
	s_motor.prev_sample_us = esp_timer_get_time();
	s_motor.filtered_rpm = 0.0f;
	s_motor.initialized = true;

	ESP_LOGI(TAG, "Motor initialized: AS5600=0x%02X PWM_FWD_GPIO=%d PWM_REV_GPIO=%d",
		AS5600_I2C_ADDR,
		MOTOR_PWM_FWD_GPIO,
		MOTOR_PWM_REV_GPIO);
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

esp_err_t motor_set_pwm_percent(float pwm_percent)
{
	if (!s_motor.pwm_initialized) {
		return ESP_ERR_INVALID_STATE;
	}

	if (pwm_percent > 100.0f) {
		pwm_percent = 100.0f;
	} else if (pwm_percent < -100.0f) {
		pwm_percent = -100.0f;
	}

	float duty_percent = fabsf(pwm_percent);
	uint32_t duty = (uint32_t)((duty_percent * (float)MOTOR_PWM_DUTY_MAX) / 100.0f + 0.5f);
	uint32_t fwd_duty = (pwm_percent >= 0.0f) ? duty : 0U;
	uint32_t rev_duty = (pwm_percent < 0.0f) ? duty : 0U;

	esp_err_t ret = ledc_set_duty(MOTOR_PWM_LEDC_MODE, MOTOR_PWM_FWD_CHANNEL, fwd_duty);
	if (ret != ESP_OK) {
		return ret;
	}

	ret = ledc_set_duty(MOTOR_PWM_LEDC_MODE, MOTOR_PWM_REV_CHANNEL, rev_duty);
	if (ret != ESP_OK) {
		return ret;
	}

	ret = ledc_update_duty(MOTOR_PWM_LEDC_MODE, MOTOR_PWM_FWD_CHANNEL);
	if (ret != ESP_OK) {
		return ret;
	}

	ret = ledc_update_duty(MOTOR_PWM_LEDC_MODE, MOTOR_PWM_REV_CHANNEL);
	if (ret != ESP_OK) {
		return ret;
	}

	s_motor.pwm_percent = pwm_percent;
	return ESP_OK;
}

esp_err_t motor_set_target_rpm(float target_rpm)
{
	s_motor.target_rpm = target_rpm;
	return ESP_OK;
}

esp_err_t motor_control_step(float *measured_rpm)
{
	if (!s_motor.initialized || !s_motor.pwm_initialized) {
		return ESP_ERR_INVALID_STATE;
	}

	if (s_motor.target_rpm == 0.0f) {
		esp_err_t stop_ret = motor_set_pwm_percent(0.0f);
		if (measured_rpm != NULL) {
			*measured_rpm = 0.0f;
		}
		return stop_ret;
	}

	float current_rpm = 0.0f;
	esp_err_t ret = motor_read_rpm(&current_rpm);
	if (ret != ESP_OK) {
		return ret;
	}

	float rpm_error = s_motor.target_rpm - current_rpm;
	float next_pwm = s_motor.pwm_percent + (MOTOR_RPM_KP * rpm_error);
	ret = motor_set_pwm_percent(next_pwm);
	if (ret != ESP_OK) {
		return ret;
	}

	if (measured_rpm != NULL) {
		*measured_rpm = current_rpm;
	}

	return ESP_OK;
}

esp_err_t motor_set_rpm(float target_rpm)
{
	esp_err_t ret = motor_set_target_rpm(target_rpm);
	if (ret != ESP_OK) {
		return ret;
	}

	return motor_control_step(NULL);
}
