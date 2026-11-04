// Frame of the pendant: header, tab content, hold/resume/reset strip and tab bar.
#include <cstring>
#include "esp3d_gcode_parser_service.h"
#include "esp3d_hal.h"
#include "pendant_settings.h"
#include "pendant_ui.h"
#if PENDANT_BSP_CONTROLS
#include "bsp.h"
#endif

namespace pendant {
namespace ui {
namespace {
const Tab* const tabs[] = {&statusTab, &jogTab, &jobTab, &toolsTab, &setupTab};
constexpr int kTabs = 5;
constexpr int kDimPercent = 3;

lv_obj_t *screen = nullptr, *content = nullptr, *dot = nullptr, *stateLabel = nullptr, *infoLabel = nullptr;
lv_obj_t *resume = nullptr, *wake = nullptr;
lv_obj_t* navItem[kTabs] = {};
lv_obj_t* navIcon[kTabs] = {};
lv_obj_t* navName[kTabs] = {};
lv_obj_t* navLine[kTabs] = {};
lv_timer_t* refreshTimer = nullptr;
int current = -1;
std::string lastMessage;
bool isDimmed = false;

uint32_t stateColor(const std::string& state) {
  if (state == "Idle") return rgb::good;
  if (state == "Run") return rgb::accent;
  if (state == "Jog" || state == "Check") return rgb::info;
  if (state == "Home") return rgb::violet;
  if (state.compare(0, 4, "Hold") == 0 || state.compare(0, 4, "Door") == 0) return rgb::warn;
  if (state == "Alarm" || state == "ConfigAlarm" || state == "Critical") return rgb::bad;
  return rgb::muted;
}
std::string stateText(const std::string& state) {
  if (state == "Hold:0" || state == "Hold:1") return "Hold";
  if (state.compare(0, 4, "Door") == 0) return "Door";
  return state;
}
void applyBrightness(int percent) {
#if PENDANT_BSP_CONTROLS
  bsp_set_brightness(percent);
#else
  (void)percent;
#endif
}
void wakeUp() {
  if (wake) { lv_obj_del(wake); wake = nullptr; }
  if (isDimmed) applyBrightness(pendant::current().brightness);
  isDimmed = false;
  lv_disp_trig_activity(nullptr);
}
void dim() {
  isDimmed = true;
  applyBrightness(kDimPercent);
  // A transparent layer swallows the waking touch so it cannot press a button.
  wake = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(wake);
  lv_obj_set_size(wake, kWidth, 480);
  lv_obj_add_flag(wake, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(wake, [](lv_event_t*) { wakeUp(); }, LV_EVENT_PRESSED, nullptr);
}
void updateHeader(const grbl::Status& s, uint64_t now) {
  const bool fresh = s.fresh(now);
  const std::string state = fresh ? s.state : "Disconnected";
  const bool latched = s.uncertain && !s.resetting;
  setText(stateLabel, s.resetting ? "Resetting..." : (latched ? "Reset required" : stateText(state)));
  const uint32_t color = s.resetting ? rgb::warn : (latched ? rgb::bad : stateColor(state));
  lv_obj_set_style_bg_color(dot, lv_color_hex(color), 0);
  setColor(stateLabel, latched ? rgb::bad : rgb::text);
  std::string info;
  if (s.resetting) info = "Please wait";
  else if (s.uncertain) info = "Press Reset";
  else if (fresh && s.sdRunning) info = number(s.progress, 0) + "%";
  else if (fresh && !s.wcs.empty()) info = s.wcs;
  else if (!fresh) info = "No status";
  if (s.pending && !s.uncertain) info = "Waiting...";
  setText(infoLabel, info);
}
void updateToast(const grbl::Status& s) {
  if (s.message == lastMessage) return;
  lastMessage = s.message;
  if (s.message.empty() || s.message == "Waiting for reply" || s.message == "Command accepted") return;
  toast(s.message, s.messageError);
}
void updateDim(const grbl::Status& s, uint64_t now) {
  const bool quiet = !s.fresh(now) || s.state == "Idle" || s.state == "Sleep";
  const unsigned long timeout = static_cast<unsigned long>(pendant::current().sleepMinutes) * 60000UL;
  if (!isDimmed && timeout && quiet && !dialogOpen() && lv_disp_get_inactive_time(nullptr) > timeout) dim();
  else if (isDimmed && !quiet) wakeUp();
}
void refresh(lv_timer_t*) {
  const grbl::Status s = esp3dGcodeParser.snapshot();
  const uint64_t now = esp3d_hal::millis();
  updateHeader(s, now);
  setEnabled(resume, s.held(now));
  updateToast(s);
  updateDim(s, now);
  if (current >= 0) tabs[current]->update(s, now);
}
// A new container per tab: flex flow, padding and scroll settings must not leak between tabs.
void resetContent() {
  if (content) lv_obj_del(content);
  content = lv_obj_create(screen);
  lv_obj_remove_style_all(content);
  lv_obj_set_pos(content, 0, kContentY);
  lv_obj_set_size(content, kWidth, kContentH);
}
void teardown() {
  if (current >= 0) tabs[current]->release();
  current = -1;
  if (refreshTimer) { lv_timer_del(refreshTimer); refreshTimer = nullptr; }
  closeDialogs();
  if (wake) lv_obj_del(wake);
  wake = nullptr;
  isDimmed = false;
  screen = content = dot = stateLabel = infoLabel = resume = nullptr;
  for (int i = 0; i < kTabs; ++i) navItem[i] = navIcon[i] = navName[i] = navLine[i] = nullptr;
}
void sendRealtime(const char* command) { esp3dGcodeParser.send(command); }
void buildHeader() {
  lv_obj_t* header = panel(screen, 0, 0, kWidth, kHeader);
  lv_obj_set_style_radius(header, 0, 0);
  lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
  dot = lv_obj_create(header);
  lv_obj_remove_style_all(dot);
  lv_obj_set_size(dot, 12, 12);
  lv_obj_set_pos(dot, 14, 14);
  lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
  stateLabel = label(header, "Disconnected", Font::title);
  lv_obj_set_pos(stateLabel, 36, 8);
  infoLabel = label(header, "", Font::small, rgb::muted);
  lv_obj_set_width(infoLabel, 130);
  lv_obj_set_style_text_align(infoLabel, LV_TEXT_ALIGN_RIGHT, 0);
  lv_label_set_long_mode(infoLabel, LV_LABEL_LONG_DOT);
  lv_obj_set_pos(infoLabel, kWidth - 130 - 12, 12);
}
void buildStrip() {
  const int y = kContentY + kContentH, gap = 6, w = (kWidth - 2 * kPad - 2 * gap) / 3, h = kStrip - 6;
  button(screen, LV_SYMBOL_PAUSE " Hold", kPad, y + 3, w, h, Kind::warning,
         [](lv_event_t*) { sendRealtime("!"); }, nullptr, Font::body);
  resume = button(screen, LV_SYMBOL_PLAY " Resume", kPad + w + gap, y + 3, w, h, Kind::success,
                  [](lv_event_t*) { confirm("Resume job?", "Motion and laser output may restart.", "Resume", Kind::success, []() { sendRealtime("~"); }); },
                  nullptr, Font::body);
  button(screen, LV_SYMBOL_RESET " Reset", kPad + 2 * (w + gap), y + 3, w, h, Kind::danger,
         [](lv_event_t*) {
           confirm("Reset controller?", "Aborts the current job and clears motion. You may need to home again. "
                   "This is not an emergency stop.", "Reset", Kind::danger, []() { sendRealtime("\x18"); });
         }, nullptr, Font::body);
}
void buildNav() {
  lv_obj_t* bar = panel(screen, 0, 480 - kNav, kWidth, kNav);
  lv_obj_set_style_radius(bar, 0, 0);
  lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_TOP, 0);
  const int w = kWidth / kTabs;
  for (int i = 0; i < kTabs; ++i) {
    navItem[i] = button(bar, "", i * w, 0, w, kNav - 1, Kind::ghost, [](lv_event_t* e) {
      showTab(static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e))));
    }, reinterpret_cast<void*>(static_cast<intptr_t>(i)));
    lv_obj_set_style_border_width(navItem[i], 0, 0);
    lv_obj_set_style_radius(navItem[i], 0, 0);
    lv_obj_set_style_bg_opa(navItem[i], LV_OPA_TRANSP, 0);
    navIcon[i] = label(navItem[i], tabs[i]->icon, Font::title, rgb::muted);
    lv_obj_align(navIcon[i], LV_ALIGN_TOP_MID, 0, 5);
    navName[i] = label(navItem[i], tabs[i]->name, Font::tiny, rgb::muted);
    lv_obj_align(navName[i], LV_ALIGN_BOTTOM_MID, 0, -3);
    navLine[i] = lv_obj_create(navItem[i]);
    lv_obj_remove_style_all(navLine[i]);
    lv_obj_set_size(navLine[i], w - 20, 3);
    lv_obj_align(navLine[i], LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(navLine[i], lv_color_hex(rgb::accent), 0);
    lv_obj_set_style_bg_opa(navLine[i], LV_OPA_COVER, 0);
    lv_obj_set_style_radius(navLine[i], 2, 0);
    lv_obj_add_flag(navLine[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(navLine[i], LV_OBJ_FLAG_CLICKABLE);
  }
}
}

void applyHardwareSettings() {
#if PENDANT_BSP_CONTROLS
  const Settings& s = pendant::current();
  bsp_touch_set_invert(s.touchFlipX, s.touchFlipY);
  bsp_display_set_flip(s.displayFlipX, s.displayFlipY);
  bsp_set_brightness(s.brightness);
#endif
}
bool dimmed() { return isDimmed; }

void showTab(int index) {
  if (!screen || index < 0 || index >= kTabs || index == current) return;
  closeDialogs();
  if (current >= 0) tabs[current]->release();
  resetContent();
  current = index;
  for (int i = 0; i < kTabs; ++i) {
    const bool active = i == index;
    setColor(navIcon[i], active ? rgb::accent : rgb::muted);
    setColor(navName[i], active ? rgb::text : rgb::muted);
    setHidden(navLine[i], !active);
  }
  tabs[index]->build(content);
  refresh(nullptr);
}

void reloadTab() {
  if (!screen || current < 0) return;
  tabs[current]->release();
  resetContent();
  tabs[current]->build(content);
  refresh(nullptr);
}

void show() {
  teardown();
  initTheme();
  lv_obj_t* old = lv_scr_act();
  screen = lv_obj_create(nullptr);
  lv_obj_remove_style_all(screen);
  lv_obj_set_size(screen, kWidth, 480);
  lv_obj_set_style_bg_color(screen, lv_color_hex(rgb::bg), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(screen, [](lv_event_t* e) {
    if (lv_event_get_target(e) == screen) teardown();
  }, LV_EVENT_DELETE, nullptr);
  buildHeader();
  resetContent();
  buildStrip();
  buildNav();
  applyHardwareSettings();
  lastMessage = esp3dGcodeParser.snapshot().message;  // only announce messages that arrive from now on
  lv_scr_load(screen);
  if (old && old != screen) lv_obj_del(old);
  showTab(0);
  refreshTimer = lv_timer_create(refresh, 200, nullptr);
}
}
}
