// Setup tab: display, jog presets, units, terminal opt-in and maintenance.
#include <cstdio>
#include "esp3d_gcode_parser_service.h"
#include "pendant_settings.h"
#include "pendant_ui.h"
#if defined(ESP_PLATFORM)
#include "esp_system.h"
#endif

namespace pendant {
namespace ui {
namespace {
const int kSleepMinutes[] = {0, 1, 2, 5, 10, 30};
lv_obj_t *brightnessValue, *firmwareValue, *heapValue;
Choice terminalChoice;

void saved() {
  if (!pendant::save()) toast("Could not save settings", true);
}
int sleepIndex(int minutes) {
  for (int i = 0; i < 6; ++i) if (kSleepMinutes[i] == minutes) return i;
  return 3;
}
std::string compact(double value) {
  char text[24];
  snprintf(text, sizeof(text), "%.3f", value);
  std::string out = text;
  while (out.back() == '0') out.pop_back();
  if (out.back() == '.') out.pop_back();
  return out;
}
lv_obj_t* title(lv_obj_t* parent, const char* text) { return label(parent, text, Font::tiny, rgb::muted); }
// Four tappable value buttons that open a number pad.
void presetRow(lv_obj_t* card, const char* caption, bool feeds) {
  Settings& s = pendant::current();
  lv_obj_t* line = rowBox(card, 40);
  lv_obj_set_pos(label(line, caption, Font::small, rgb::muted), 0, 10);
  const int each = 46;
  for (unsigned i = 0; i < kPresets; ++i) {
    const std::string text = feeds ? std::to_string(s.feeds[i]) : compact(s.steps[i]);
    actionButton(line, text.c_str(), 74 + i * (each + 4), 0, each, 40, Kind::neutral, [i, feeds, text]() {
      numberPad(feeds ? "Feed preset (mm/min)" : "Step preset (mm)", text, [i, feeds](double value) {
        Settings& s = pendant::current();
        if (feeds) s.feeds[i] = static_cast<unsigned>(value);
        else s.steps[i] = value;
        pendant::sanitize(s);
        saved();
        reloadTab();
      });
    }, Font::small);
  }
}
void setTerminal(int index) {
  if (index == 0) {
    pendant::current().terminal = false;
    saved();
    return;
  }
  // Keep showing the saved state until the warning is accepted.
  terminalChoice.select(0);
  confirm("Enable terminal?",
          "The terminal sends any text you type to the controller, including commands that start the spindle or fire the laser.",
          "Enable", Kind::warning, []() {
            pendant::current().terminal = true;
            saved();
            reloadTab();
          });
}
void build(lv_obj_t* parent) {
  scrollColumn(parent);
  Settings& s = pendant::current();

  lv_obj_t* display = flexCard(parent);
  title(display, "DISPLAY");
  lv_obj_t* line = rowBox(display, 30);
  lv_obj_set_pos(label(line, "Brightness", Font::small, rgb::muted), 0, 5);
  brightnessValue = label(line, (std::to_string(s.brightness) + "%").c_str(), Font::small);
  lv_obj_align(brightnessValue, LV_ALIGN_RIGHT_MID, 0, 0);
  slider(line, 84, 10, 140, 10, 100, s.brightness, [](int v) {
    pendant::current().brightness = v;
    setText(brightnessValue, std::to_string(v) + "%");
    applyHardwareSettings();
  }, [](int) { saved(); });
  label(display, "Dim after", Font::small, rgb::muted);
  choiceRow(display, {"Off", "1m", "2m", "5m", "10m", "30m"}, 0, 0, 284, 36, sleepIndex(s.sleepMinutes), [](int i) {
    pendant::current().sleepMinutes = kSleepMinutes[i];
    saved();
  });
  actionButton(display, "Display and touch test", 0, 0, 284, 44, Kind::neutral, []() { openDisplayTest(); });

  lv_obj_t* jog = flexCard(parent);
  title(jog, "JOG PRESETS");
  presetRow(jog, "Step mm", false);
  presetRow(jog, "Feed /min", true);

  lv_obj_t* controller = flexCard(parent);
  title(controller, "CONTROLLER");
  label(controller, "Controller reports ($13)", Font::small, rgb::muted);
  choiceRow(controller, {"Millimetres", "Inches"}, 0, 0, 284, 36, s.reportInches ? 1 : 0, [](int i) {
    pendant::current().reportInches = i == 1;
    saved();
  });
  label(controller, "Terminal", Font::small, rgb::muted);
  terminalChoice = choiceRow(controller, {"Off", "On"}, 0, 0, 284, 36, s.terminal ? 1 : 0, setTerminal);

  lv_obj_t* about = flexCard(parent);
  title(about, "ABOUT");
  firmwareValue = keyValue(about, "Controller");
  heapValue = keyValue(about, "Free memory");
  actionButton(about, "Reset all settings", 0, 0, 284, 44, Kind::danger, []() {
    confirm("Reset all settings?", "Brightness, presets, macros and display options return to defaults.", "Reset", Kind::danger, []() {
      pendant::current() = Settings{};
      saved();
      applyHardwareSettings();
      reloadTab();
    });
  });
}
void update(const grbl::Status& s, uint64_t) {
  setText(firmwareValue, s.firmware.empty() ? "not seen since start" : s.firmware);
#if defined(ESP_PLATFORM)
  setText(heapValue, std::to_string(esp_get_free_heap_size() / 1024) + " KB");
#else
  setText(heapValue, "n/a");
#endif
}
void release() {
  brightnessValue = firmwareValue = heapValue = nullptr;
  terminalChoice = Choice{};
}
}
const Tab setupTab = {"Setup", LV_SYMBOL_SETTINGS, build, update, release};
}
}
