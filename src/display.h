#ifndef DISPLAY_H
#define DISPLAY_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

// Initializes the CFAH2004AC-TFH-EW display on I2C address 0x3C.
esp_err_t display_init(void);

// Sends one display command byte.
esp_err_t display_send_command(uint8_t command);

// Sends one or more display data bytes.
esp_err_t display_send_data(const uint8_t *data, size_t len);

// Clears the display and resets the cursor to the home position.
esp_err_t clear_display(void);

// Sets the cursor position.
esp_err_t set_cursor(uint8_t row, uint8_t col);

#endif
