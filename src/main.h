#ifndef MAIN_H
#define MAIN_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    float bean_temp;
    float env_temp;
    int air;
    int burner;
    int drum;
    int drum_cont;
    bool drum_cont_mode;
    float motor_pwm;
    uint8_t ip_address[4];
} roaster_state_t;

#endif