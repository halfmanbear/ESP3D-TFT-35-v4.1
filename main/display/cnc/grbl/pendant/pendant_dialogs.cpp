#include <algorithm>
#include <cmath>
#include <cstdlib>
#include "pendant_ui.h"

namespace pendant {
namespace ui {
namespace {
std::vector<lv_obj_t*> open;
lv_obj_t* toastBox = nullptr;
lv_timer_t* toastTimer = nullptr;

void forget(lv_event_t* e) {
  open.erase(std::remove(open.begin(), open.end(), lv_event_get_target(e)), open.end());
}
// Dark, input-blocking layer covering the whole screen.
lv_obj_t* scrim(uint8_t opacity = LV_OPA_60, int x = 0, int y = 0, int w = kWidth, int h = 480) {
  lv_obj_t* obj = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(obj);
  lv_obj_set_pos(obj, x, y);
  lv_obj_set_size(obj, w, h);
  lv_obj_set_style_bg_color(obj, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(obj, opacity, 0);
  lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(obj, forget, LV_EVENT_DELETE, nullptr);
  open.push_back(obj);
  return obj;
}
// Removes the layer from the open list at once and deletes it on the next timer tick, so a
// button may close its own dialog and closeDialogs() can never delete it a second time.
void dismiss(lv_obj_t* layer) {
  open.erase(std::remove(open.begin(), open.end(), layer), open.end());
  lv_obj_del_async(layer);
}
// Column card that sizes itself to its content.
lv_obj_t* card(lv_obj_t* parent, int width) {
  lv_obj_t* obj = panel(parent, 0, 0, width, LV_SIZE_CONTENT);
  lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);  // taps on the card must not reach the scrim behind it
  lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(obj, 14, 0);
  lv_obj_set_style_pad_row(obj, 10, 0);
  lv_obj_set_style_bg_color(obj, lv_color_hex(rgb::surface), 0);
  return obj;
}
lv_obj_t* wrapped(lv_obj_t* parent, const std::string& text, Font font, uint32_t color) {
  lv_obj_t* obj = label(parent, text.c_str(), font, color);
  lv_obj_set_width(obj, lv_pct(100));
  lv_label_set_long_mode(obj, LV_LABEL_LONG_WRAP);
  return obj;
}
lv_obj_t* row(lv_obj_t* parent, int height) {
  lv_obj_t* obj = lv_obj_create(parent);
  lv_obj_remove_style_all(obj);
  lv_obj_set_size(obj, lv_pct(100), height);
  lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(obj, 10, 0);
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
  return obj;
}
lv_obj_t* flexButton(lv_obj_t* parent, const std::string& text, Kind kind, std::function<void()> action, int width = -1) {
  lv_obj_t* obj = actionButton(parent, text.c_str(), 0, 0, 100, 48, kind, std::move(action));
  if (width < 0) lv_obj_set_flex_grow(obj, 1);
  else lv_obj_set_width(obj, width);
  return obj;
}
void hideToastCb(lv_timer_t*) { hideToast(); }
void onToastDelete(lv_event_t*) { toastBox = nullptr; }

struct EditorState {
  std::vector<lv_obj_t*> fields;
  lv_obj_t* keyboard = nullptr;
  std::function<void()> save;
};
void focus(EditorState* state, lv_obj_t* field) {
  for (lv_obj_t* other : state->fields) {
    if (other == field) lv_obj_add_state(other, LV_STATE_FOCUSED);
    else lv_obj_clear_state(other, LV_STATE_FOCUSED);
  }
  lv_keyboard_set_textarea(state->keyboard, field);
}
lv_obj_t* textField(lv_obj_t* parent, const std::string& initial, int maxLength) {
  lv_obj_t* field = lv_textarea_create(parent);
  lv_textarea_set_one_line(field, true);
  lv_textarea_set_max_length(field, maxLength);
  lv_textarea_set_text(field, initial.c_str());
  lv_obj_set_width(field, lv_pct(100));
  lv_obj_set_height(field, 42);
  lv_obj_set_style_text_font(field, &lv_font_montserrat_16, 0);
  return field;
}
}

lv_obj_t* overlay(uint8_t opacity, int x, int y, int w, int h) { return scrim(opacity, x, y, w, h); }
void closeOverlay(lv_obj_t* layer) { dismiss(layer); }

void closeDialogs() {
  hideToast();
  auto layers = open;
  for (lv_obj_t* layer : layers) lv_obj_del(layer);
  open.clear();
}
bool dialogOpen() { return !open.empty(); }

void confirm(const std::string& title, const std::string& body, const std::string& action, Kind kind,
             std::function<void()> onConfirm) {
  lv_obj_t* layer = scrim();
  lv_obj_t* box = card(layer, 292);
  lv_obj_center(box);
  wrapped(box, title, Font::title, rgb::text);
  wrapped(box, body, Font::body, rgb::muted);
  lv_obj_t* buttons = row(box, 48);
  flexButton(buttons, "Cancel", Kind::neutral, [layer]() { dismiss(layer); });
  flexButton(buttons, action, kind, [layer, onConfirm]() {
    dismiss(layer);
    if (onConfirm) onConfirm();
  });
}
void notice(const std::string& title, const std::string& body) {
  lv_obj_t* layer = scrim();
  lv_obj_t* box = card(layer, 292);
  lv_obj_center(box);
  wrapped(box, title, Font::title, rgb::text);
  wrapped(box, body, Font::body, rgb::muted);
  flexButton(row(box, 48), "OK", Kind::primary, [layer]() { dismiss(layer); });
}
void sheet(const std::string& title, const std::string& subtitle, const std::vector<SheetAction>& actions) {
  lv_obj_t* layer = scrim();
  lv_obj_add_event_cb(layer, [](lv_event_t* e) {
    if (lv_event_get_target(e) == lv_event_get_current_target(e)) dismiss(lv_event_get_target(e));
  }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* box = card(layer, kWidth);
  lv_obj_align(box, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_radius(box, 16, 0);
  wrapped(box, title, Font::title, rgb::text);
  if (!subtitle.empty()) wrapped(box, subtitle, Font::small, rgb::muted);
  for (const SheetAction& item : actions) {
    lv_obj_t* b = actionButton(box, item.text.c_str(), 0, 0, 100, 48, item.kind, [layer, run = item.run]() {
      dismiss(layer);
      if (run) run();
    });
    lv_obj_set_width(b, lv_pct(100));
  }
  lv_obj_t* cancel = actionButton(box, "Cancel", 0, 0, 100, 44, Kind::ghost, [layer]() { dismiss(layer); });
  lv_obj_set_width(cancel, lv_pct(100));
}
void gridPicker(const std::string& title, const std::vector<std::string>& cells, std::function<void(int)> onPick) {
  lv_obj_t* layer = scrim();
  lv_obj_t* box = card(layer, 292);
  lv_obj_center(box);
  wrapped(box, title, Font::title, rgb::text);
  lv_obj_t* grid = lv_obj_create(box);
  lv_obj_remove_style_all(grid);
  lv_obj_set_size(grid, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_style_pad_column(grid, 6, 0);
  lv_obj_set_style_pad_row(grid, 6, 0);
  lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
  for (size_t i = 0; i < cells.size(); ++i) {
    actionButton(grid, cells[i].c_str(), 0, 0, 84, 48, Kind::neutral, [layer, onPick, i]() {
      dismiss(layer);
      if (onPick) onPick(static_cast<int>(i));
    }, Font::small);
  }
  lv_obj_t* cancel = actionButton(box, "Cancel", 0, 0, 100, 44, Kind::ghost, [layer]() { dismiss(layer); });
  lv_obj_set_width(cancel, lv_pct(100));
}
void hideToast() {
  if (toastTimer) { lv_timer_del(toastTimer); toastTimer = nullptr; }
  if (toastBox) { lv_obj_del(toastBox); toastBox = nullptr; }
}
void toast(const std::string& text, bool error) {
  hideToast();
  toastBox = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(toastBox);
  lv_obj_add_event_cb(toastBox, onToastDelete, LV_EVENT_DELETE, nullptr);
  lv_obj_set_size(toastBox, kInnerW, 32);
  lv_obj_set_pos(toastBox, kPad, kContentY + kContentH - 36);
  lv_obj_set_style_bg_color(toastBox, lv_color_hex(error ? 0x7A1F1C : 0x1B3A5C), 0);
  lv_obj_set_style_bg_opa(toastBox, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(toastBox, lv_color_hex(error ? rgb::bad : rgb::accent), 0);
  lv_obj_set_style_border_width(toastBox, 1, 0);
  lv_obj_set_style_radius(toastBox, 16, 0);
  lv_obj_clear_flag(toastBox, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  lv_obj_t* caption = label(toastBox, text.c_str(), Font::small, rgb::text);
  lv_obj_set_width(caption, kInnerW - 16);
  lv_label_set_long_mode(caption, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_center(caption);
  toastTimer = lv_timer_create(hideToastCb, 3500, nullptr);
  lv_timer_set_repeat_count(toastTimer, 1);
}

void numberPad(const std::string& title, const std::string& initial, std::function<void(double)> onDone) {
  lv_obj_t* layer = scrim(LV_OPA_80);
  lv_obj_t* box = card(layer, 304);
  lv_obj_align(box, LV_ALIGN_TOP_MID, 0, 12);
  wrapped(box, title, Font::title, rgb::text);
  lv_obj_t* field = textField(box, initial, 8);
  lv_textarea_set_accepted_chars(field, "0123456789.");
  lv_obj_add_state(field, LV_STATE_FOCUSED);
  lv_obj_t* keys = lv_keyboard_create(layer);
  lv_keyboard_set_mode(keys, LV_KEYBOARD_MODE_NUMBER);
  lv_keyboard_set_textarea(keys, field);
  lv_obj_set_size(keys, kWidth, 220);
  lv_obj_align(keys, LV_ALIGN_BOTTOM_MID, 0, 0);
  struct Context { lv_obj_t* layer; lv_obj_t* field; std::function<void(double)> done; };
  auto* context = new Context{layer, field, std::move(onDone)};
  lv_obj_add_event_cb(keys, [](lv_event_t* e) {
    auto* c = static_cast<Context*>(lv_event_get_user_data(e));
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CANCEL) { dismiss(c->layer); return; }
    if (code != LV_EVENT_READY) return;  // not DELETE etc: the field may already be gone
    char* end;
    const char* text = lv_textarea_get_text(c->field);
    double value = strtod(text, &end);
    if (*text && !*end && std::isfinite(value)) {
      auto done = c->done;
      dismiss(c->layer);
      if (done) done(value);
    }
  }, LV_EVENT_ALL, context);
  lv_obj_add_event_cb(keys, [](lv_event_t* e) {
    if (lv_event_get_code(e) == LV_EVENT_DELETE) delete static_cast<Context*>(lv_event_get_user_data(e));
  }, LV_EVENT_DELETE, context);
}

void textEditor(const std::string& title, const std::vector<std::pair<std::string, std::string>>& fields,
                int maxLength, std::function<void(const std::vector<std::string>&)> onDone) {
  lv_obj_t* layer = scrim(LV_OPA_80);
  lv_obj_t* box = card(layer, 304);
  lv_obj_align(box, LV_ALIGN_TOP_MID, 0, 8);
  wrapped(box, title, Font::title, rgb::text);
  auto* state = new EditorState;
  lv_obj_add_event_cb(layer, [](lv_event_t* e) { delete static_cast<EditorState*>(lv_event_get_user_data(e)); },
                      LV_EVENT_DELETE, state);
  for (const auto& item : fields) {
    wrapped(box, item.first, Font::small, rgb::muted);
    lv_obj_t* field = textField(box, item.second, maxLength);
    state->fields.push_back(field);
  }
  state->keyboard = lv_keyboard_create(layer);
  lv_obj_set_size(state->keyboard, kWidth, 200);
  lv_obj_align(state->keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
  for (lv_obj_t* field : state->fields) {
    lv_obj_add_event_cb(field, [](lv_event_t* e) {
      focus(static_cast<EditorState*>(lv_event_get_user_data(e)), lv_event_get_target(e));
    }, LV_EVENT_CLICKED, state);
  }
  focus(state, state->fields.front());
  state->save = [layer, state, onDone]() {
    std::vector<std::string> values;
    for (lv_obj_t* field : state->fields) values.push_back(lv_textarea_get_text(field));
    dismiss(layer);
    if (onDone) onDone(values);
  };
  lv_obj_t* buttons = row(box, 48);
  flexButton(buttons, "Cancel", Kind::neutral, [layer]() { dismiss(layer); });
  flexButton(buttons, "Save", Kind::primary, [state]() { state->save(); });
  // The keyboard's tick key saves and its hide key cancels.
  lv_obj_add_event_cb(state->keyboard, [](lv_event_t* e) {
    auto* state = static_cast<EditorState*>(lv_event_get_user_data(e));
    if (lv_event_get_code(e) == LV_EVENT_READY) state->save();
    else if (lv_event_get_code(e) == LV_EVENT_CANCEL) dismiss(lv_obj_get_parent(lv_event_get_target(e)));
  }, LV_EVENT_ALL, state);
}
}
}
