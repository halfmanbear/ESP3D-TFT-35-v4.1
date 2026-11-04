#pragma once
#include <stdbool.h>
struct MockBsp { int brightness = 100; bool flipX = false, flipY = false, touchX = false, touchY = false; int calls = 0; };
inline MockBsp bspState;
inline int bsp_set_brightness(int percent) { bspState.brightness = percent; ++bspState.calls; return 0; }
inline int bsp_display_set_flip(bool x, bool y) { bspState.flipX = x; bspState.flipY = y; return 0; }
inline void bsp_touch_set_invert(bool x, bool y) { bspState.touchX = x; bspState.touchY = y; }
