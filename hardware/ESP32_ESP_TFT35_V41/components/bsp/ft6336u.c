// FT6336U single-point polling using the FT6x36 register map.
// Keep INT as an input; it is an output of the touch controller.
#include "touch_def.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static i2c_bus_device_handle_t touch;
static volatile bool invert_x, invert_y;

void ft6336u_set_invert(bool inv_x, bool inv_y) {
  invert_x = inv_x;
  invert_y = inv_y;
}

esp_err_t ft6336u_init(i2c_bus_handle_t bus) {
  gpio_config_t irq = {.pin_bit_mask = 1ULL << TOUCH_INT_PIN,
                       .mode = GPIO_MODE_INPUT};
  gpio_config_t rst = {.pin_bit_mask = 1ULL << TOUCH_RST_PIN,
                       .mode = GPIO_MODE_OUTPUT};
  esp_err_t err = gpio_config(&irq);
  if (err != ESP_OK) return err;
  err = gpio_config(&rst);
  if (err != ESP_OK) return err;
  gpio_set_level(TOUCH_RST_PIN, 0);
  vTaskDelay(pdMS_TO_TICKS(10));
  gpio_set_level(TOUCH_RST_PIN, 1);
  vTaskDelay(pdMS_TO_TICKS(200));
  touch = i2c_bus_device_create(bus, 0x38, 100000);
  if (!touch) return ESP_FAIL;
  uint8_t chip;
  err = i2c_bus_read_byte(touch, 0xA3, &chip);
  // Normal operating mode; leave factory tuning and calibration untouched.
  if (err == ESP_OK) err = i2c_bus_write_byte(touch, 0x00, 0);
  if (err != ESP_OK) i2c_bus_device_delete(&touch);
  return err;
}

ft6336u_data_t ft6336u_read(void) {
  ft6336u_data_t result = {0};
  uint8_t data[5];
  if (!touch || i2c_bus_read_bytes(touch, 0x02, sizeof(data), data) != ESP_OK)
    return result;
  uint8_t count = data[0] & 0x0F;
  uint8_t event = data[1] >> 6;
  if (count == 0 || count > 2 || (event != 0 && event != 2)) return result;
  int x = ((data[1] & 0x0F) << 8) | data[2];
  int y = ((data[3] & 0x0F) << 8) | data[4];
  if (x >= TOUCH_MAX_X || y >= TOUCH_MAX_Y) return result;
  if (invert_x) x = TOUCH_MAX_X - 1 - x;
  if (invert_y) y = TOUCH_MAX_Y - 1 - y;
  result.x = x;
  result.y = y;
  result.is_pressed = true;
  return result;
}
