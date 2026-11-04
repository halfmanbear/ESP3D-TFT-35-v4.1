// Persistent pendant preferences. Pure C++; storage is a pair of hooks so the
// same code runs on the host (tests) and on the ESP32 (NVS).
#pragma once
#include <string>

namespace pendant {
constexpr unsigned kPresets = 4, kMacros = 9;
struct Macro {
  std::string name, path;  // path is a controller path such as /sd/fluidtouch/macros/a.nc
  bool used() const { return !path.empty(); }
};
struct Settings {
  int brightness = 100;    // percent, 10-100
  int sleepMinutes = 5;    // dim after this much inactivity, 0 = never
  bool terminal = false;   // free-form terminal is opt-in
  bool reportInches = false;  // controller $13; only changes unit labels
  bool touchFlipX = false, touchFlipY = false;
  bool displayFlipX = false, displayFlipY = false;
  double steps[kPresets] = {0.1, 1, 10, 50};          // mm
  unsigned feeds[kPresets] = {100, 500, 1000, 3000};  // mm/min
  int step = 1, feed = 1;  // selected presets
  bool liveJog = false;    // Jog tab shows the held-control sticks instead of step buttons
  Macro macros[kMacros];
};
// Clamp every field into its valid range.
void sanitize(Settings& settings);
bool validMacroPath(const std::string& path);
std::string serialize(const Settings& settings);
// Lenient: unknown or invalid fields keep their defaults.
Settings parse(const std::string& text);
// Active settings, loaded on first use.
Settings& current();
void load();
bool save();
// Storage hooks, defined by pendant_settings_nvs.cpp on the device.
bool storageRead(std::string& text);
bool storageWrite(const std::string& text);
}
