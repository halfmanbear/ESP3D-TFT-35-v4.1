// Panel-specific sequence from embeddedboys/rpi_dm_yt350s006_software,
// rpi-dm-cl35bc219-40a.txt. ST7796-compatible, hardware validation pending.
#include "panel_init.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

esp_err_t panel_init(esp_lcd_panel_io_handle_t io) {
  static const struct { uint8_t cmd, len, data[14]; } init[] = {
      {0xF0, 1, {0xC3}}, {0xF0, 1, {0x96}},
      {0x36, 1, {PANEL_MADCTL_PORTRAIT}}, {0x3A, 1, {0x55}},
      {0xB4, 1, {0x01}}, {0xB7, 1, {0xC6}},
      {0xE8, 8, {0x40,0x8A,0x00,0x00,0x29,0x19,0xA5,0x33}},
      {0xC1, 1, {0x06}}, {0xC2, 1, {0xA7}}, {0xC5, 1, {0x18}},
      {0xE0,14,{0xF0,0x09,0x0B,0x06,0x04,0x15,0x2F,0x54,0x42,0x3C,0x17,0x14,0x18,0x1B}},
      {0xE1,14,{0xF0,0x09,0x0B,0x06,0x04,0x03,0x2D,0x43,0x42,0x3B,0x16,0x14,0x17,0x1B}},
      {0xF0, 1, {0x3C}}, {0xF0, 1, {0x69}},
      {0x21, 0, {0}}, {0x29, 0, {0}},
  };
  esp_err_t err = esp_lcd_panel_io_tx_param(io, 0x11, NULL, 0);
  if (err != ESP_OK) return err;
  vTaskDelay(pdMS_TO_TICKS(120));
  for (size_t i = 0; i < sizeof(init) / sizeof(init[0]); ++i) {
    err = esp_lcd_panel_io_tx_param(io, init[i].cmd, init[i].data, init[i].len);
    if (err != ESP_OK) return err;
  }
  return ESP_OK;
}

esp_err_t panel_set_flip(esp_lcd_panel_io_handle_t io, bool flip_x, bool flip_y) {
  uint8_t madctl = PANEL_MADCTL_PORTRAIT ^ (flip_x ? PANEL_MADCTL_FLIP_X : 0) ^
                   (flip_y ? PANEL_MADCTL_FLIP_Y : 0);
  return esp_lcd_panel_io_tx_param(io, 0x36, &madctl, 1);
}
