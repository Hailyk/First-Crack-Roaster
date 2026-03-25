#ifndef MAIN_H
#define MAIN_H

typedef struct {
    float bean_temp;
    float env_temp;
    float exhaust_humidity;
    int air;
    int burner;
    int drum;
    float drum_rpm;
} roaster_state_t;

#endif