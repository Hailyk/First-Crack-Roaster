#ifndef THERMO_H
#define THERMO_H

#include "esp_err.h"


// Configures both MCP9600 sensors for K-type thermocouple. Call once after I2C bus init.
esp_err_t thermo_init(void);

// Reads bean temperature from MCP9600 at I2C address 0x60 in degrees C.
esp_err_t thermo_read_bean_temperature_c(float *bean_temperature_c);
// Reads environmental temperature from MCP9600 at I2C address 0x67 in degrees C.
esp_err_t thermo_read_env_temperature_c(float *env_temperature_c);


#endif
