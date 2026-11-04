#include <cstdio>
#include <cstring>
#include <utility>
#include "pendant_ui.h"

namespace pendant {
namespace ui {
namespace {
constexpr unsigned kKinds = 6;
bool ready = false;
lv_style_t stPanel, stBar, stBarFill, stChecked;
lv_style_t stBtn[kKinds], stBtnDown[kKinds], stBtnOff[kKinds], stFont[5];

lv_color_t hex(uint32_t value) { return lv_color_hex(value); }
const lv_font_t* fontFor(Font font) {
  switch (font) {
    case Font::tiny: return &lv_font_montserrat_12;
    case Font::small: return &lv_font_montserrat_14;
    case Font::title: return &lv_font_montserrat_20;
    case Font::big: return &lv_font_montserrat_28;
    default: return &lv_font_montserrat_16;
  }
}
void buttonStyle(Kind kind, uint32_t& fill, uint32_t& line, uint32_t& text) {
  text = rgb::text;
  switch (kind) {
    case Kind::primary: fill = rgb::accent; line = rgb::accent; text = 0xFFFFFF; break;
    case Kind::success: fill = 0x238636; line = rgb::good; text = 0xFFFFFF; break;
    case Kind::warning: fill = 0x9A6700; line = rgb::warn; text = 0xFFFFFF; break;
    case Kind::danger: fill = 0xC62828; line = rgb::bad; text = 0xFFFFFF; break;
    case Kind::ghost: fill = rgb::bg; line = rgb::border; break;
    default: fill = rgb::raised; line = rgb::border; break;
  }
}
void initStyles() {
  if (ready) return;
  ready = true;
  lv_style_init(&stPanel);
  lv_style_set_bg_color(&stPanel, hex(rgb::surface));
  lv_style_set_bg_opa(&stPanel, LV_OPA_COVER);
  lv_style_set_radius(&stPanel, 12);
  lv_style_set_border_width(&stPanel, 1);
  lv_style_set_border_color(&stPanel, hex(rgb::border));
  lv_style_set_pad_all(&stPanel, 0);
  for (unsigned i = 0; i < kKinds; ++i) {
    uint32_t fill, line, text;
    buttonStyle(static_cast<Kind>(i), fill, line, text);
    lv_style_init(&stBtn[i]);
    lv_style_set_bg_color(&stBtn[i], hex(fill));
    lv_style_set_bg_opa(&stBtn[i], LV_OPA_COVER);
    lv_style_set_border_color(&stBtn[i], hex(line));
    lv_style_set_border_width(&stBtn[i], 1);
    lv_style_set_radius(&stBtn[i], 10);
    lv_style_set_text_color(&stBtn[i], hex(text));
    lv_style_set_pad_all(&stBtn[i], 0);
    lv_style_init(&stBtnDown[i]);
    lv_style_set_bg_color(&stBtnDown[i], lv_color_lighten(hex(i == static_cast<unsigned>(Kind::ghost) ? rgb::raised : fill), 60));
    // Dimmed colours rather than the opa property, which LVGL draws through an extra layer buffer.
    lv_style_init(&stBtnOff[i]);
    lv_style_set_bg_color(&stBtnOff[i], lv_color_mix(hex(fill), hex(rgb::bg), 60));
    lv_style_set_border_color(&stBtnOff[i], hex(rgb::border));
    lv_style_set_text_color(&stBtnOff[i], hex(0x667084));
  }
  lv_style_init(&stChecked);
  lv_style_set_bg_color(&stChecked, hex(rgb::accent));
  lv_style_set_border_color(&stChecked, hex(rgb::accent));
  lv_style_set_text_color(&stChecked, lv_color_white());
  lv_style_init(&stBar);
  lv_style_set_bg_color(&stBar, hex(rgb::raised));
  lv_style_set_bg_opa(&stBar, LV_OPA_COVER);
  lv_style_set_radius(&stBar, 4);
  lv_style_init(&stBarFill);
  lv_style_set_bg_opa(&stBarFill, LV_OPA_COVER);
  lv_style_set_radius(&stBarFill, 4);
  for (unsigned i = 0; i < 5; ++i) {
    lv_style_init(&stFont[i]);
    lv_style_set_text_font(&stFont[i], fontFor(static_cast<Font>(i)));
  }
}
void runAction(lv_event_t* e) {
  // Copy first: the action may delete this very button and with it the stored closure.
  auto action = *static_cast<std::function<void()>*>(lv_event_get_user_data(e));
  action();
}
void freeAction(lv_event_t* e) { delete static_cast<std::function<void()>*>(lv_event_get_user_data(e)); }
}

void initTheme() {
  initStyles();
  lv_disp_t* display = lv_disp_get_default();
  lv_theme_t* theme = lv_theme_default_init(display, hex(rgb::accent), hex(rgb::violet), true, &lv_font_montserrat_16);
  lv_disp_set_theme(display, theme);
}
lv_obj_t* panel(lv_obj_t* parent, int x, int y, int w, int h) {
  lv_obj_t* obj = lv_obj_create(parent);
  lv_obj_remove_style_all(obj);
  lv_obj_add_style(obj, &stPanel, 0);
  lv_obj_set_pos(obj, x, y);
  lv_obj_set_size(obj, w, h);
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  return obj;
}
lv_obj_t* label(lv_obj_t* parent, const char* text, Font font, uint32_t color) {
  lv_obj_t* obj = lv_label_create(parent);
  lv_obj_remove_style_all(obj);
  lv_obj_add_style(obj, &stFont[static_cast<unsigned>(font)], 0);
  lv_obj_set_style_text_color(obj, hex(color), 0);
  lv_label_set_text(obj, text);
  return obj;
}
lv_obj_t* button(lv_obj_t* parent, const char* text, int x, int y, int w, int h, Kind kind,
                 lv_event_cb_t callback, void* user, Font font) {
  lv_obj_t* obj = lv_btn_create(parent);
  lv_obj_remove_style_all(obj);
  const unsigned k = static_cast<unsigned>(kind);
  lv_obj_add_style(obj, &stBtn[k], 0);
  lv_obj_add_style(obj, &stBtnDown[k], LV_STATE_PRESSED);
  lv_obj_add_style(obj, &stChecked, LV_STATE_CHECKED);
  lv_obj_add_style(obj, &stBtnOff[k], LV_STATE_DISABLED);
  lv_obj_set_pos(obj, x, y);
  lv_obj_set_size(obj, w, h);
  if (callback) lv_obj_add_event_cb(obj, callback, LV_EVENT_CLICKED, user);
  if (text && *text) {
    lv_obj_t* caption = lv_label_create(obj);
    lv_obj_remove_style_all(caption);
    lv_obj_add_style(caption, &stFont[static_cast<unsigned>(font)], 0);
    lv_label_set_text(caption, text);
    lv_label_set_long_mode(caption, LV_LABEL_LONG_DOT);
    lv_obj_set_width(caption, lv_pct(100));
    lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(caption);
  }
  return obj;
}
lv_obj_t* actionButton(lv_obj_t* parent, const char* text, int x, int y, int w, int h, Kind kind,
                       std::function<void()> action, Font font) {
  auto* stored = new std::function<void()>(std::move(action));
  lv_obj_t* obj = button(parent, text, x, y, w, h, kind, runAction, stored, font);
  lv_obj_add_event_cb(obj, freeAction, LV_EVENT_DELETE, stored);
  return obj;
}
lv_obj_t* bar(lv_obj_t* parent, int x, int y, int w, int h, uint32_t color) {
  lv_obj_t* obj = lv_bar_create(parent);
  lv_obj_remove_style_all(obj);
  lv_obj_add_style(obj, &stBar, LV_PART_MAIN);
  lv_obj_add_style(obj, &stBarFill, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(obj, hex(color), LV_PART_INDICATOR);
  lv_obj_set_pos(obj, x, y);
  lv_obj_set_size(obj, w, h);
  lv_bar_set_range(obj, 0, 100);
  return obj;
}
namespace {
struct SliderHooks {
  std::function<void(int)> change, release;
};
}
lv_obj_t* slider(lv_obj_t* parent, int x, int y, int w, int min, int max, int value,
                 std::function<void(int)> onChange, std::function<void(int)> onRelease) {
  lv_obj_t* obj = lv_slider_create(parent);
  lv_obj_remove_style_all(obj);
  lv_obj_set_pos(obj, x, y);
  lv_obj_set_size(obj, w, 10);
  lv_obj_set_ext_click_area(obj, 14);
  lv_obj_set_style_bg_color(obj, hex(rgb::raised), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_bg_color(obj, hex(rgb::accent), LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(obj, lv_color_white(), LV_PART_KNOB);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_KNOB);
  lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, LV_PART_KNOB);
  lv_obj_set_style_pad_all(obj, 9, LV_PART_KNOB);
  lv_slider_set_range(obj, min, max);
  lv_slider_set_value(obj, value, LV_ANIM_OFF);
  auto* hooks = new SliderHooks{std::move(onChange), std::move(onRelease)};
  lv_obj_add_event_cb(obj, [](lv_event_t* e) {
    auto* h = static_cast<SliderHooks*>(lv_event_get_user_data(e));
    const int v = lv_slider_get_value(lv_event_get_target(e));
    switch (lv_event_get_code(e)) {
      case LV_EVENT_VALUE_CHANGED: if (h->change) h->change(v); break;
      case LV_EVENT_RELEASED: if (h->release) h->release(v); break;
      case LV_EVENT_DELETE: delete h; break;
      default: break;
    }
  }, LV_EVENT_ALL, hooks);
  return obj;
}
lv_obj_t* flexCard(lv_obj_t* parent) {
  lv_obj_t* obj = panel(parent, 0, 0, kInnerW, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(obj, 10, 0);
  lv_obj_set_style_pad_row(obj, 4, 0);
  return obj;
}
lv_obj_t* rowBox(lv_obj_t* parent, int height) {
  lv_obj_t* obj = lv_obj_create(parent);
  lv_obj_remove_style_all(obj);
  lv_obj_set_size(obj, lv_pct(100), height);
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  return obj;
}
lv_obj_t* keyValue(lv_obj_t* parent, const char* caption) {
  lv_obj_t* line = rowBox(parent, 24);
  label(line, caption, Font::small, rgb::muted);
  lv_obj_t* value = label(line, "", Font::body);
  lv_obj_set_width(value, 190);
  lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);
  lv_label_set_long_mode(value, LV_LABEL_LONG_DOT);
  lv_obj_align(value, LV_ALIGN_TOP_RIGHT, 0, 0);
  return value;
}
void scrollColumn(lv_obj_t* parent) {
  lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(parent, kPad, 0);
  lv_obj_set_style_pad_row(parent, kPad, 0);
  lv_obj_set_scroll_dir(parent, LV_DIR_VER);
  lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLL_ELASTIC);  // the bounce costs extra full redraws on the SPI panel
}
// Remembers the last text given to a label: ellipsised labels rewrite their own buffer,
// so lv_label_get_text() cannot be used to tell whether the value changed.
void setText(lv_obj_t* obj, const std::string& text) {
  if (!obj) return;
  auto* shown = static_cast<std::string*>(lv_obj_get_user_data(obj));
  if (!shown) {
    shown = new std::string(lv_label_get_text(obj));
    lv_obj_set_user_data(obj, shown);
    lv_obj_add_event_cb(obj, [](lv_event_t* e) { delete static_cast<std::string*>(lv_obj_get_user_data(lv_event_get_target(e))); },
                        LV_EVENT_DELETE, nullptr);
  }
  if (*shown == text) return;
  *shown = text;
  lv_label_set_text(obj, text.c_str());
}
void setColor(lv_obj_t* obj, uint32_t color) {
  if (obj && lv_obj_get_style_text_color(obj, 0).full != hex(color).full) lv_obj_set_style_text_color(obj, hex(color), 0);
}
void setEnabled(lv_obj_t* obj, bool enabled) {
  if (!obj || lv_obj_has_state(obj, LV_STATE_DISABLED) == !enabled) return;
  if (enabled) lv_obj_clear_state(obj, LV_STATE_DISABLED);
  else lv_obj_add_state(obj, LV_STATE_DISABLED);
}
void setChecked(lv_obj_t* obj, bool checked) {
  if (!obj || lv_obj_has_state(obj, LV_STATE_CHECKED) == checked) return;
  if (checked) lv_obj_add_state(obj, LV_STATE_CHECKED);
  else lv_obj_clear_state(obj, LV_STATE_CHECKED);
}
void setHidden(lv_obj_t* obj, bool hidden) {
  if (!obj || lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN) == hidden) return;
  if (hidden) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
}
void setBar(lv_obj_t* obj, int value) {
  if (obj && lv_bar_get_value(obj) != value) lv_bar_set_value(obj, value, LV_ANIM_OFF);
}
void Choice::select(int index) {
  for (size_t i = 0; i < items.size(); ++i) setChecked(items[i], static_cast<int>(i) == index);
}
Choice choiceRow(lv_obj_t* parent, const std::vector<std::string>& labels, int x, int y, int w, int h,
                 int selected, std::function<void(int)> onSelect) {
  Choice choice;
  auto group = std::make_shared<std::vector<lv_obj_t*>>();
  const int gap = 4, count = static_cast<int>(labels.size());
  const int each = (w - gap * (count - 1)) / count;
  lv_obj_t* host = rowBox(parent, h);
  lv_obj_set_pos(host, x, y);
  lv_obj_set_width(host, w);
  for (int i = 0; i < count; ++i) {
    lv_obj_t* item = actionButton(host, labels[i].c_str(), i * (each + gap), 0, each, h, Kind::neutral,
                                  [group, onSelect, i]() {
                                    for (size_t n = 0; n < group->size(); ++n) setChecked((*group)[n], static_cast<int>(n) == i);
                                    if (onSelect) onSelect(i);
                                  }, Font::small);
    group->push_back(item);
    choice.items.push_back(item);
  }
  choice.select(selected);
  return choice;
}
std::string number(double value, int decimals) {
  char text[40];
  snprintf(text, sizeof(text), "%.*f", decimals, value);
  return text;
}
const char* lengthUnit(bool inches) { return inches ? "in" : "mm"; }
const char* feedUnit(bool inches) { return inches ? "in/min" : "mm/min"; }
std::string fileSize(int64_t bytes) {
  char text[24];
  if (bytes < 1024) snprintf(text, sizeof(text), "%d B", static_cast<int>(bytes));
  else if (bytes < 1024 * 1024) snprintf(text, sizeof(text), "%.1f KB", bytes / 1024.0);
  else snprintf(text, sizeof(text), "%.1f MB", bytes / 1048576.0);
  return text;
}
}
}
