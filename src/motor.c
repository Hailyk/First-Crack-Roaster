#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"

#include "motor.h"

#define MOTOR_PWM_FWD_GPIO       GPIO_NUM_2
#define MOTOR_PWM_REV_GPIO       GPIO_NUM_3
#define MOTOR_PWM_FREQ_HZ        20000
#define MOTOR_PWM_LEDC_MODE      LEDC_LOW_SPEED_MODE
#define MOTOR_PWM_LEDC_TIMER     LEDC_TIMER_0
#define MOTOR_PWM_FWD_CHANNEL    LEDC_CHANNEL_0
#define MOTOR_PWM_REV_CHANNEL    LEDC_CHANNEL_1
#define MOTOR_PWM_DUTY_RES       LEDC_TIMER_10_BIT
#define MOTOR_PWM_DUTY_MAX       ((1U << 10) - 1U)

typedef struct {
	bool pwm_initialized;
	float pwm_percent;
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
	s_motor.pwm_initialized = true;
	return ESP_OK;
}

esp_err_t motor_init(void)
{
	esp_err_t ret = motor_pwm_init();
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "PWM init failed on GPIO%d/GPIO%d: %s", MOTOR_PWM_FWD_GPIO, MOTOR_PWM_REV_GPIO, esp_err_to_name(ret));
		return ret;
	}

	ESP_LOGI(TAG, "Motor initialized: PWM_FWD_GPIO=%d PWM_REV_GPIO=%d",
		MOTOR_PWM_FWD_GPIO,
		MOTOR_PWM_REV_GPIO);
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
