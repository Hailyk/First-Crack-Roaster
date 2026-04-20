#ifndef MOTOR_H
#define MOTOR_H

#include "esp_err.h"

// Initializes bidirectional motor PWM outputs.
esp_err_t motor_init(void);

// Sets bidirectional motor PWM in percent (-100.0 to 100.0).
// Positive drives forward pin, negative drives reverse pin.
esp_err_t motor_set_pwm_percent(float pwm_percent);

#endif
