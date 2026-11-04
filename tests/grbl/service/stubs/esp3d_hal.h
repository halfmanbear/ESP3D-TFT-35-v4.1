#pragma once
#include <cstdint>
inline uint64_t testClock = 100;
namespace esp3d_hal { inline uint64_t millis() { return testClock; } }
