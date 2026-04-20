#ifndef MAIN_H
#define MAIN_H

#include <stdint.h>

typedef struct {
    float bean_temp;
    float env_temp;
    int air;
    int burner;
    int drum;
    uint8_t ip_address[4];
} roaster_state_t;

#endif