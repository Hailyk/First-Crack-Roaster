#ifndef MOTOR_H
#define MOTOR_H

#include <stdint.h>

#include "esp_err.h"

// Initializes AS5600 sampling state. Call after I2C bus initialization.
esp_err_t motor_init(void);

// Reads AS5600 raw angle in counts (0-4095).
esp_err_t motor_read_raw_angle(uint16_t *raw_angle);

// Reads AS5600 angle in degrees (0.0-360.0).
esp_err_t motor_read_angle_deg(float *angle_deg);

// Reads AS5600 and returns filtered RPM estimate.
esp_err_t motor_read_rpm(float *rpm);

// Sets DRV8871 PWM duty on GPIO2 in percent (0.0 to 100.0).
esp_err_t motor_set_pwm_percent(float pwm_percent);

// Sets desired motor RPM setpoint for closed-loop control.
esp_err_t motor_set_target_rpm(float target_rpm);

// Convenience API to set target RPM and run one control update step.
esp_err_t motor_set_rpm(float target_rpm);

// Runs one closed-loop control update and adjusts PWM on GPIO2.
// Call this periodically (for example every 20-100 ms).
esp_err_t motor_control_step(float *measured_rpm);

#endif
