// Tools tab: homing, unlock, work offsets, zeroing and realtime overrides.
#include <algorithm>
#include "esp3d_gcode_parser_service.h"
#include "pendant_settings.h"
#include "pendant_ui.h"

namespace pendant {
namespace ui {
namespace {
constexpr int kAxes = 6;
const char kAxisName[] = "XYZABC";

struct Override {
  const char* name;
  const char* codes[3];  // realtime bytes for the three buttons
  const char* captions[3];
};
const Override kOverrides[3] = {
    {"Feed", {"\x92", "\x90", "\x91"}, {"-10%", "100%", "+10%"}},
    {"Rapid", {"\x97", "\x96", "\x95"}, {"25%", "50%", "100%"}},
    {"Spindle", {"\x9b", "\x99", "\x9a"}, {"-10%", "100%", "+10%"}},
};

lv_obj_t *homeButton, *unlockButton, *laserOff, *zeroAxis[kAxes], *zeroAll;
lv_obj_t* overrideLabel[3];
std::vector<lv_obj_t*> needIdle, needRealtime;
Choice wcsChoice;

void send(const char* command) {
  if (!esp3dGcodeParser.send(command)) toast("Not sent: check machine state", true);
}
lv_obj_t* sectionTitle(lv_obj_t* parent, const char* text) { return label(parent, text, Font::tiny, rgb::muted); }
lv_obj_t* wrapRow(lv_obj_t* parent) {
  lv_obj_t* row = lv_obj_create(parent);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_column(row, 6, 0);
  lv_obj_set_style_pad_row(row, 6, 0);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  return row;
}
lv_obj_t* flexButton(lv_obj_t* row, const char* text, int width, Kind kind, std::function<void()> action, Font font = Font::body) {
  return actionButton(row, text, 0, 0, width, 44, kind, std::move(action), font);
}
void zero(const std::string& words, const std::string& what) {
  confirm("Set " + what + " to zero?", "Changes the active work offset to the current position.", "Set zero", Kind::warning,
          [words]() { send(("G10 L20 P0 " + words).c_str()); });
}
void build(lv_obj_t* parent) {
  scrollColumn(parent);
  needIdle.clear();
  needRealtime.clear();

  lv_obj_t* machine = flexCard(parent);
  sectionTitle(machine, "MACHINE");
  lv_obj_t* row = wrapRow(machine);
  homeButton = flexButton(row, LV_SYMBOL_HOME " Home all", 134, Kind::primary, []() {
    confirm("Home all axes?", "The machine will move towards its limit switches.", "Home", Kind::primary, []() { send("$H"); });
  });
  unlockButton = flexButton(row, LV_SYMBOL_UNLOCK " Unlock", 134, Kind::neutral, []() {
    confirm("Unlock the controller?", "Clears the alarm lock without homing. Position may be wrong: home before running a job.",
            "Unlock", Kind::warning, []() { send("$X"); });
  });
  laserOff = flexButton(row, "Laser / spindle off (M5)", 276, Kind::neutral, []() { send("M5"); });
  if (pendant::current().terminal) flexButton(row, LV_SYMBOL_KEYBOARD " Terminal", 276, Kind::ghost, []() { openTerminal(); });

  lv_obj_t* offsets = flexCard(parent);
  sectionTitle(offsets, "WORK OFFSET");
  const char* names[] = {"G54", "G55", "G56", "G57", "G58", "G59"};
  wcsChoice = choiceRow(offsets, {names, names + 6}, 0, 0, 276, 40, -1, [](int i) {
    static const char* codes[] = {"G54", "G55", "G56", "G57", "G58", "G59"};
    send(codes[i]);
  });
  for (lv_obj_t* item : wcsChoice.items) needIdle.push_back(item);
  lv_obj_t* zeros = wrapRow(offsets);
  for (int i = 0; i < kAxes; ++i) {
    const std::string name(1, kAxisName[i]);
    zeroAxis[i] = flexButton(zeros, ("Zero " + name).c_str(), 60, Kind::neutral, [name]() { zero(name + "0", "axis " + name); }, Font::small);
    needIdle.push_back(zeroAxis[i]);
  }
  zeroAll = flexButton(zeros, "Zero XYZ", 78, Kind::warning, []() { zero("X0 Y0 Z0", "X, Y and Z"); }, Font::small);
  needIdle.push_back(zeroAll);
  needIdle.push_back(laserOff);

  lv_obj_t* overrides = flexCard(parent);
  sectionTitle(overrides, "OVERRIDES");
  for (int i = 0; i < 3; ++i) {
    lv_obj_t* line = rowBox(overrides, 44);
    overrideLabel[i] = label(line, kOverrides[i].name, Font::body);
    lv_obj_align(overrideLabel[i], LV_ALIGN_LEFT_MID, 0, 0);
    for (int n = 0; n < 3; ++n) {
      const char* code = kOverrides[i].codes[n];
      lv_obj_t* b = actionButton(line, kOverrides[i].captions[n], 98 + n * 62, 0, 58, 44, Kind::neutral,
                                 [code]() { send(code); }, Font::small);
      needRealtime.push_back(b);
    }
  }
}
void update(const grbl::Status& s, uint64_t now) {
  const bool idle = s.idle(now);
  for (lv_obj_t* b : needIdle) setEnabled(b, idle);
  setEnabled(homeButton, idle || s.alarm(now));
  setEnabled(unlockButton, idle || s.alarm(now));
  const bool realtime = s.fresh(now) && !s.uncertain;
  for (lv_obj_t* b : needRealtime) setEnabled(b, realtime);
  const unsigned axes = std::max(s.machineAxes, s.workAxes);
  for (int i = 0; i < kAxes; ++i) setHidden(zeroAxis[i], static_cast<unsigned>(i) >= std::max(3u, axes));
  int active = -1;
  for (int i = 0; i < 6; ++i)
    if (s.fresh(now) && s.wcs.size() == 3 && s.wcs[2] == static_cast<char>('4' + i)) active = i;
  wcsChoice.select(active);
  for (int i = 0; i < 3; ++i) {
    const double value = s.overrides[i];
    setText(overrideLabel[i], std::string(kOverrides[i].name) + (realtime && s.hasOverrides ? " " + number(value, 0) + "%" : ""));
  }
}
void release() {
  needIdle.clear();
  needRealtime.clear();
  wcsChoice = Choice{};
  homeButton = unlockButton = laserOff = zeroAll = nullptr;
  for (int i = 0; i < kAxes; ++i) zeroAxis[i] = nullptr;
  for (int i = 0; i < 3; ++i) overrideLabel[i] = nullptr;
}
}
const Tab toolsTab = {"Tools", LV_SYMBOL_MILLING, build, update, release};
}
}
