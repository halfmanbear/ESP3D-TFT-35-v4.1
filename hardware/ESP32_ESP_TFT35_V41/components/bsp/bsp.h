/*
  esp3d_tft project

  Copyright (c) 2022 Luc Lebosse. All rights reserved.

  This code is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This code is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#include <stdbool.h>

#include "esp_err.h"

/**********************
 * GLOBAL PROTOTYPES
 **********************/
/**
 * @brief Initializes the Board Support Package (BSP).
 *
 * This function initializes the necessary hardware and peripherals required by
 * the BSP.
 *
 * @return esp_err_t Returns ESP_OK if the initialization is successful,
 * otherwise an error code.
 */
esp_err_t bsp_init(void);

/** Set the backlight level, 0-100 percent. */
esp_err_t bsp_set_brightness(int percent);

/** Mirror the display horizontally and/or vertically from its default. */
esp_err_t bsp_display_set_flip(bool flip_x, bool flip_y);

/** Invert the touch axes; applied to every subsequent touch read. */
void bsp_touch_set_invert(bool invert_x, bool invert_y);

#ifdef __cplusplus
} /* extern "C" */
#endif
