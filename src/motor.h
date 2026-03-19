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

#endif
