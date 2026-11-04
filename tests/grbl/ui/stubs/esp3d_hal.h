#pragma once
#include <lvgl.h>
#include <cstdint>
namespace esp3d_hal { inline uint64_t millis() { return lv_tick_get(); } }
