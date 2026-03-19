#ifndef HUMIDITY_H
#define HUMIDITY_H

#include "esp_err.h"

// Probes SHT85 sensor connectivity by doing one measurement.
esp_err_t humidity_init(void);

// Reads relative humidity from SHT85 in percent (0.0 to 100.0).
esp_err_t humidity_read_relative_humidity_percent(float *humidity_percent);

// Reads SHT85 and updates normalized exhaust humidity (0.0 to 1.0).
esp_err_t humidity_update_exhaust_humidity(float *exhaust_humidity);

#endif