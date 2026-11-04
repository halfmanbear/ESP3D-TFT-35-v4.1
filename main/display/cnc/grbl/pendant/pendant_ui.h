// Touch-only FluidNC pendant UI for a 320x480 portrait display.
#pragma once
#include <lvgl.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "grbl_protocol.h"

namespace pendant {
namespace ui {
// Layout of the 320x480 screen: header, content, hold/resume/reset strip, tab bar.
constexpr int kWidth = 320, kHeader = 40, kStrip = 44, kNav = 48, kPad = 8;
constexpr int kContentY = kHeader, kContentH = 480 - kHeader - kStrip - kNav;
constexpr int kInnerW = kWidth - 2 * kPad;

enum class Kind { neutral, primary, success, warning, danger, ghost };
enum class Font { tiny, small, body, title, big };

// Palette (24-bit RGB).
namespace rgb {
constexpr uint32_t bg = 0x0D1117, surface = 0x161B22, raised = 0x212A36, border = 0x2D3745;
constexpr uint32_t text = 0xE6EDF3, muted = 0x8B96A5;
constexpr uint32_t accent = 0x2F81F7, good = 0x3FB950, warn = 0xE3B341, bad = 0xF85149, info = 0x39C5CF, violet = 0xA371F7;
}

// ---- theme and widgets (pendant_widgets.cpp) ----
void initTheme();
lv_obj_t* panel(lv_obj_t* parent, int x, int y, int w, int h);
lv_obj_t* label(lv_obj_t* parent, const char* text, Font font = Font::body, uint32_t color = rgb::text);
lv_obj_t* button(lv_obj_t* parent, const char* text, int x, int y, int w, int h, Kind kind,
                 lv_event_cb_t callback = nullptr, void* user = nullptr, Font font = Font::body);
// Button whose click runs a closure; the closure is freed with the button.
lv_obj_t* actionButton(lv_obj_t* parent, const char* text, int x, int y, int w, int h, Kind kind,
                       std::function<void()> action, Font font = Font::body);
lv_obj_t* bar(lv_obj_t* parent, int x, int y, int w, int h, uint32_t color);
lv_obj_t* slider(lv_obj_t* parent, int x, int y, int w, int min, int max, int value,
                 std::function<void(int)> onChange, std::function<void(int)> onRelease);
// Full-width card that grows with its content; children stack in a column.
lv_obj_t* flexCard(lv_obj_t* parent);
// Transparent fixed-height container for hand-positioned children.
lv_obj_t* rowBox(lv_obj_t* parent, int height);
// Muted caption on the left, value on the right.
lv_obj_t* keyValue(lv_obj_t* parent, const char* caption);
// Scrolling column used as the content of a tab.
void scrollColumn(lv_obj_t* parent);
// Update helpers that only touch LVGL when the value changed.
void setText(lv_obj_t* label, const std::string& text);
void setColor(lv_obj_t* label, uint32_t color);
void setEnabled(lv_obj_t* obj, bool enabled);
void setChecked(lv_obj_t* obj, bool checked);
void setHidden(lv_obj_t* obj, bool hidden);
void setBar(lv_obj_t* bar, int value);
// Segmented row of equally wide choices; selection reported through `onSelect`.
struct Choice {
  std::vector<lv_obj_t*> items;
  void select(int index);
};
Choice choiceRow(lv_obj_t* parent, const std::vector<std::string>& labels, int x, int y, int w, int h,
                 int selected, std::function<void(int)> onSelect);
std::string number(double value, int decimals);
const char* lengthUnit(bool inches);
const char* feedUnit(bool inches);
std::string fileSize(int64_t bytes);

// ---- dialogs (pendant_dialogs.cpp) ----
// Layer-top overlays. closeDialogs() removes every open one.
void closeDialogs();
// Plain layer on the top layer that blocks input to the area it covers.
lv_obj_t* overlay(uint8_t opacity, int x = 0, int y = 0, int w = kWidth, int h = 480);
// Closes an overlay or dialog from inside its own callbacks.
void closeOverlay(lv_obj_t* layer);
bool dialogOpen();
void confirm(const std::string& title, const std::string& body, const std::string& action, Kind kind,
             std::function<void()> onConfirm);
void notice(const std::string& title, const std::string& body);
void toast(const std::string& text, bool error);
void hideToast();
// Bottom sheet with up to four stacked action buttons and a cancel.
struct SheetAction {
  std::string text;
  Kind kind;
  std::function<void()> run;
};
void sheet(const std::string& title, const std::string& subtitle, const std::vector<SheetAction>& actions);
// Three-column grid of choices, e.g. to pick a macro slot.
void gridPicker(const std::string& title, const std::vector<std::string>& cells, std::function<void(int)> onPick);
// Number pad (value is edited as text) and full keyboard editors.
void numberPad(const std::string& title, const std::string& initial, std::function<void(double)> onDone);
void textEditor(const std::string& title, const std::vector<std::pair<std::string, std::string>>& fields,
                int maxLength, std::function<void(const std::vector<std::string>&)> onDone);

// ---- tabs ----
struct Tab {
  const char* name;
  const char* icon;
  void (*build)(lv_obj_t* parent);
  void (*update)(const grbl::Status& status, uint64_t now);
  void (*release)();
};
extern const Tab statusTab, jogTab, jobTab, toolsTab, setupTab;
void openTerminal();
void openDisplayTest();
// Builds and shows the pendant screen (replaces the active screen).
void show();
// Shell hooks used by tabs.
void showTab(int index);
// Rebuilds the visible tab, e.g. after a setting that changes its layout.
void reloadTab();
void applyHardwareSettings();
// Whole-screen pointer-only wake/dim state.
bool dimmed();
}
}
