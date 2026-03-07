#ifndef WIFI_H
#define WIFI_H

#include "esp_err.h"

typedef struct {
    float bean_temp;
    float env_temp;
    float exhaust_humidity;
    int air;
    int burner;
    int drum;
} roaster_state_t;

esp_err_t wifi_start(roaster_state_t *state);

#endif