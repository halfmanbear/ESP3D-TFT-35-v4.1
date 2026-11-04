#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "i2c_bus.h"
#define TOUCH_CONTROLLER "FT6336U"
#define TOUCH_RST_PIN 14
#define TOUCH_INT_PIN 21
// Native portrait coordinates of the 320x480 panel. The axis directions are
// unverified; they can be inverted at runtime by bsp_touch_set_invert().
#define TOUCH_MAX_X 320
#define TOUCH_MAX_Y 480
typedef struct {
  bool is_pressed;
  int16_t x, y;
} ft6336u_data_t;
esp_err_t ft6336u_init(i2c_bus_handle_t bus);
void ft6336u_set_invert(bool invert_x, bool invert_y);
ft6336u_data_t ft6336u_read(void);
