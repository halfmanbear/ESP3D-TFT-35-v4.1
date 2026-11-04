// Full-height overlays: the controller terminal and the display/touch test.
#include <cstring>
#include "esp3d_gcode_parser_service.h"
#include "pendant_settings.h"
#include "pendant_ui.h"

namespace pendant {
namespace ui {
namespace {
// ---- terminal ----
constexpr int kTermH = kContentH, kTitleH = 36;
lv_obj_t *termLayer, *termLog, *termText, *termInput, *termKeys, *termRow;
lv_timer_t* termTimer;
unsigned seenLog = ~0u;
std::vector<std::string> history;
int historyPos = 0;
char accepted[96];

void termLayout(bool keyboard) {
  const int logH = keyboard ? 92 : 252;
  lv_obj_set_height(termLog, logH);
  lv_obj_set_y(termRow, kTitleH + 4 + logH + 4);
  setHidden(termKeys, !keyboard);
  // Keyboards are created bottom-aligned; align again or the position is taken from the bottom.
  lv_obj_align(termKeys, LV_ALIGN_TOP_LEFT, 0, kTitleH + 4 + logH + 4 + 44);
  lv_obj_set_size(termKeys, kWidth, kTermH - (kTitleH + 4 + logH + 4 + 44));
}
void termRefresh(bool force) {
  const grbl::Status s = esp3dGcodeParser.snapshot();
  if (!force && s.logGeneration == seenLog) return;
  seenLog = s.logGeneration;
  const std::vector<std::string> lines = esp3dGcodeParser.terminalLog();
  std::string text;
  for (size_t i = lines.size() > 40 ? lines.size() - 40 : 0; i < lines.size(); ++i) text += lines[i] + "\n";
  lv_label_set_text(termText, text.empty() ? "Commands you send and the replies appear here." : text.c_str());
  lv_obj_update_layout(termLog);
  lv_obj_scroll_to_y(termLog, LV_COORD_MAX, LV_ANIM_OFF);
}
void termSend() {
  const std::string line = lv_textarea_get_text(termInput);
  if (line.empty()) return;
  if (esp3dGcodeParser.sendTerminal(line.c_str())) {
    history.push_back(line);
    if (history.size() > 10) history.erase(history.begin());
    historyPos = static_cast<int>(history.size());
    lv_textarea_set_text(termInput, "");
    termRefresh(true);
  } else {
    toast("Not sent: machine must be idle or in alarm", true);
  }
}
void termRecall() {
  if (history.empty()) return;
  historyPos = historyPos <= 0 ? static_cast<int>(history.size()) - 1 : historyPos - 1;
  lv_textarea_set_text(termInput, history[historyPos].c_str());
}
void termClosed(lv_event_t*) {
  if (termTimer) lv_timer_del(termTimer);
  termTimer = nullptr;
  termLayer = termLog = termText = termInput = termKeys = termRow = nullptr;
}

// ---- display test ----
lv_obj_t *dot, *readout;
void flag(lv_obj_t* parent, const char* text, int x, int y, bool Settings::*field) {
  auto self = std::make_shared<lv_obj_t*>(nullptr);
  lv_obj_t* b = actionButton(parent, text, x, y, 148, 44, Kind::neutral, [field, self]() {
    Settings& s = pendant::current();
    s.*field = !(s.*field);
    setChecked(*self, s.*field);
    pendant::save();
    applyHardwareSettings();
    // The panel keeps old pixels in the old orientation until everything is redrawn.
    lv_obj_invalidate(lv_scr_act());
    lv_obj_invalidate(lv_layer_top());
  }, Font::small);
  *self = b;
  setChecked(b, pendant::current().*field);
}
}

void openTerminal() {
  if (!pendant::current().terminal) {
    notice("Terminal is off", "Turn it on in Setup > Controller first.");
    return;
  }
  if (termLayer) return;
  termLayer = overlay(LV_OPA_COVER, 0, kContentY, kWidth, kTermH);
  lv_obj_set_style_bg_color(termLayer, lv_color_hex(rgb::bg), 0);
  lv_obj_add_event_cb(termLayer, termClosed, LV_EVENT_DELETE, nullptr);
  lv_obj_set_pos(label(termLayer, "Terminal", Font::title), 12, 6);
  actionButton(termLayer, "Close", kWidth - 76, 3, 68, 30, Kind::neutral, []() { closeOverlay(termLayer); }, Font::small);

  termLog = panel(termLayer, kPad, kTitleH + 4, kInnerW, 252);
  lv_obj_set_scroll_dir(termLog, LV_DIR_VER);
  lv_obj_add_flag(termLog, LV_OBJ_FLAG_SCROLLABLE);
  termText = label(termLog, "", Font::tiny, rgb::text);
  lv_obj_set_width(termText, kInnerW - 14);
  lv_label_set_long_mode(termText, LV_LABEL_LONG_WRAP);
  lv_obj_set_pos(termText, 6, 4);

  termRow = lv_obj_create(termLayer);
  lv_obj_remove_style_all(termRow);
  lv_obj_set_size(termRow, kWidth, 44);
  lv_obj_clear_flag(termRow, LV_OBJ_FLAG_SCROLLABLE);
  termInput = lv_textarea_create(termRow);
  lv_textarea_set_one_line(termInput, true);
  lv_textarea_set_max_length(termInput, 120);
  lv_textarea_set_placeholder_text(termInput, "Command");
  size_t n = 0;
  for (int c = 32; c < 127; ++c) if (c != '?' && c != '!' && c != '~') accepted[n++] = static_cast<char>(c);
  accepted[n] = 0;
  lv_textarea_set_accepted_chars(termInput, accepted);
  lv_obj_set_pos(termInput, kPad, 0);
  lv_obj_set_size(termInput, 196, 40);
  actionButton(termRow, LV_SYMBOL_UP, kPad + 200, 0, 40, 40, Kind::neutral, termRecall);
  actionButton(termRow, "Send", kPad + 244, 0, 60, 40, Kind::primary, termSend, Font::small);

  termKeys = lv_keyboard_create(termLayer);
  lv_keyboard_set_textarea(termKeys, termInput);
  lv_obj_add_event_cb(termKeys, [](lv_event_t* e) {
    if (lv_event_get_code(e) == LV_EVENT_READY) termSend();
    else if (lv_event_get_code(e) == LV_EVENT_CANCEL) termLayout(false);
  }, LV_EVENT_ALL, nullptr);
  lv_obj_add_event_cb(termInput, [](lv_event_t*) { termLayout(true); }, LV_EVENT_CLICKED, nullptr);
  termLayout(false);
  seenLog = ~0u;
  historyPos = static_cast<int>(history.size());
  termRefresh(true);
  termTimer = lv_timer_create([](lv_timer_t*) { termRefresh(false); }, 200, nullptr);
}

void openDisplayTest() {
  lv_obj_t* layer = overlay(LV_OPA_COVER);
  lv_obj_set_style_bg_color(layer, lv_color_hex(rgb::bg), 0);
  const struct { const char* name; int x, y; } corners[] = {{"TL", 0, 0}, {"TR", kWidth - 40, 0}, {"BL", 0, 440}, {"BR", kWidth - 40, 440}};
  for (const auto& corner : corners) {
    lv_obj_t* mark = panel(layer, corner.x, corner.y, 40, 40);
    lv_obj_set_style_border_color(mark, lv_color_hex(rgb::accent), 0);
    lv_obj_set_style_border_width(mark, 2, 0);
    lv_obj_center(label(mark, corner.name, Font::small, rgb::accent));
  }
  lv_obj_t* title = label(layer, "Display and touch test", Font::title);
  lv_obj_set_pos(title, 52, 50);
  lv_obj_t* help = label(layer, "Touch the corner squares. The dot must appear under your finger and the letters must read correctly. "
                                "If not, switch the matching option below.", Font::small, rgb::muted);
  lv_obj_set_width(help, kInnerW - 20);
  lv_obj_set_pos(help, 20, 84);
  dot = lv_obj_create(layer);
  lv_obj_remove_style_all(dot);
  lv_obj_set_size(dot, 22, 22);
  lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(dot, lv_color_hex(rgb::warn), 0);
  lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
  lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
  readout = label(layer, "x --  y --", Font::body, rgb::text);
  lv_obj_set_pos(readout, 100, 170);
  lv_obj_add_event_cb(layer, [](lv_event_t* e) {
    lv_indev_t* input = lv_indev_get_act();
    if (!input) return;
    lv_point_t p;
    lv_indev_get_point(input, &p);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(dot, p.x - 11, p.y - 11);
    setText(readout, "x " + std::to_string(p.x) + "  y " + std::to_string(p.y));
    (void)e;
  }, LV_EVENT_PRESSING, nullptr);
  flag(layer, "Mirror display X", 8, 250, &Settings::displayFlipX);
  flag(layer, "Mirror display Y", 164, 250, &Settings::displayFlipY);
  flag(layer, "Invert touch X", 8, 302, &Settings::touchFlipX);
  flag(layer, "Invert touch Y", 164, 302, &Settings::touchFlipY);
  actionButton(layer, "Done", 60, 380, 200, 44, Kind::primary, [layer]() { closeOverlay(layer); });
}
}
}
