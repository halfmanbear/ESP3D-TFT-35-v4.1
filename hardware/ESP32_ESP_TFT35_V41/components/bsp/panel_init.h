#pragma once
#include <stdbool.h>
#include "esp_lcd_panel_io.h"

// ST7796 MADCTL for portrait (MX + BGR, the standard rotation 0). Flip bits are
// XORed in at runtime from the saved display setting.
#define PANEL_MADCTL_PORTRAIT 0x48
#define PANEL_MADCTL_FLIP_X 0x40
#define PANEL_MADCTL_FLIP_Y 0x80

esp_err_t panel_init(esp_lcd_panel_io_handle_t io);
esp_err_t panel_set_flip(esp_lcd_panel_io_handle_t io, bool flip_x, bool flip_y);
