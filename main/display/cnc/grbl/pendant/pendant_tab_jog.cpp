// Jog tab. Step mode: a tap moves one preset distance. Live mode: while a stick, strip or A/B/C
// button is held the machine moves continuously, and it stops when the finger lifts.
// The held jog itself (chunking, look-ahead limit, cancel, time limit) lives in grbl::Session::jogHold.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include "esp3d_gcode_parser_service.h"
#include "pendant_settings.h"
#include "pendant_ui.h"

namespace pendant {
namespace ui {
namespace {
constexpr int kCell = 56, kGap = 4, kExtra = 3, kChipX = 64;
constexpr int kModeY = 0, kModeH = 30, kStepY = 34, kFeedY = 72, kChipH = 34, kPadTop = 110;
constexpr int kPadSize = 3 * kCell + 2 * kGap;  // stick and strip share the footprint of the button pad
constexpr int kKnob = 56, kTravel = (kPadSize - kKnob) / 2 - 2;
constexpr int kStripX = 3 * (kCell + kGap) + 4, kStripW = kInnerW - kStripX;
constexpr int kExtraY = kPadTop + kPadSize + 4;
constexpr double kDeadZone = 0.12, kAxisSnap = 0.2;

std::vector<lv_obj_t*> stepGroup, liveGroup, moveButtons;  // moveButtons: enabled only when the machine can take a move
lv_obj_t *extraRow[kExtra] = {}, *extraButton[kExtra * 2] = {};
lv_obj_t *feedCaption, *readout, *stick, *stickKnob, *strip, *stripKnob;
bool holding = false;   // a finger is on a live control
bool refused = false;   // the controller said no; ignore the press until it is lifted
bool liveReady = true;  // what the live controls currently look like

std::string compact(double value) {
  char text[24];
  snprintf(text, sizeof(text), "%.3f", value);
  std::string out = text;
  while (out.back() == '0') out.pop_back();
  if (out.back() == '.') out.pop_back();
  return out;
}
unsigned maxFeed() {
  Settings& s = pendant::current();
  return s.feeds[s.feed];
}
void move(char axis, int direction) {
  Settings& s = pendant::current();
  const std::string command = grbl::jog(axis, direction * s.steps[s.step], s.feeds[s.feed]);
  if (command.empty() || !esp3dGcodeParser.send(command.c_str())) toast("Not sent: machine must be idle", true);
}
lv_obj_t* jogButton(lv_obj_t* parent, const char* text, int x, int y, int w, int h, char axis, int direction) {
  lv_obj_t* b = actionButton(parent, text, x, y, w, h, Kind::neutral, [axis, direction]() { move(axis, direction); });
  moveButtons.push_back(b);
  stepGroup.push_back(b);
  return b;
}

// ---- live control ----
// Quadratic response: fine control near the centre, full speed at the rim.
double curve(double deflection) {
  if (deflection < kDeadZone) return 0;
  const double t = std::min(1.0, (deflection - kDeadZone) / (1 - kDeadZone));
  return t * t;
}
void centre(lv_obj_t* knob) {
  if (knob) lv_obj_align(knob, LV_ALIGN_CENTER, 0, 0);
}
// Called for every touch sample (about every 30 ms) while held; also keeps the controller's jog fed.
void hold(const std::vector<std::pair<char, double>>& weights, const char* axes) {
  if (refused) return;
  holding = true;
  double magnitude = 0;
  for (const auto& w : weights) magnitude += w.second * w.second;
  magnitude = std::min(1.0, std::sqrt(magnitude));
  if (!esp3dGcodeParser.jogHold(weights, maxFeed())) {
    refused = true;
    toast(esp3dGcodeParser.snapshot().message, true);
    setText(readout, "Stopped - lift your finger");
    return;
  }
  if (magnitude < 0.001) setText(readout, "Centre - not moving");
  else setText(readout, std::string(axes) + "  " + std::to_string(static_cast<int>(std::lround(maxFeed() * magnitude))) + " mm/min");
}
// Lifting the finger (or sliding off the screen) stops the machine.
void endHold() {
  esp3dGcodeParser.jogRelease();
  holding = refused = false;
  centre(stickKnob);
  centre(stripKnob);
  setText(readout, "Hold and drag to jog");
}
bool pointer(lv_obj_t* base, int& dx, int& dy) {
  lv_indev_t* indev = lv_indev_get_act();
  if (!indev || !base) return false;
  lv_point_t point;
  lv_indev_get_point(indev, &point);
  lv_area_t area;
  lv_obj_get_coords(base, &area);
  dx = point.x - (area.x1 + area.x2) / 2;
  dy = point.y - (area.y1 + area.y2) / 2;
  return true;
}
void stickEvent(lv_event_t* e) {
  const lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) return endHold();
  if (code == LV_EVENT_DELETE) {  // never leave a jog running behind a vanished control
    if (holding) esp3dGcodeParser.jogRelease();
    holding = refused = false;
    return;
  }
  int dx, dy;
  if ((code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING) || !pointer(stick, dx, dy)) return;
  const double distance = std::sqrt(static_cast<double>(dx * dx + dy * dy));
  const double scale = distance > kTravel ? kTravel / distance : 1;
  lv_obj_align(stickKnob, LV_ALIGN_CENTER, static_cast<int>(dx * scale), static_cast<int>(dy * scale));
  const double power = curve(std::min(1.0, distance / kTravel));
  double x = distance > 0 ? power * dx / distance : 0, y = distance > 0 ? -power * dy / distance : 0;
  // Near an axis, move along it exactly: a slightly crooked finger should not cut a diagonal.
  if (std::fabs(y) < kAxisSnap * std::fabs(x)) y = 0;
  else if (std::fabs(x) < kAxisSnap * std::fabs(y)) x = 0;
  std::vector<std::pair<char, double>> weights;
  std::string axes;
  if (x != 0) { weights.emplace_back('X', x); axes += x > 0 ? "X+ " : "X- "; }
  if (y != 0) { weights.emplace_back('Y', y); axes += y > 0 ? "Y+" : "Y-"; }
  hold(weights, axes.c_str());
}
void stripEvent(lv_event_t* e) {
  const lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) return endHold();
  if (code == LV_EVENT_DELETE) {
    if (holding) esp3dGcodeParser.jogRelease();
    holding = refused = false;
    return;
  }
  int dx, dy;
  if ((code != LV_EVENT_PRESSED && code != LV_EVENT_PRESSING) || !pointer(strip, dx, dy)) return;
  const int offset = std::max(-kTravel, std::min(kTravel, dy));
  lv_obj_align(stripKnob, LV_ALIGN_CENTER, 0, offset);
  const double z = -curve(std::fabs(offset) / static_cast<double>(kTravel)) * (offset < 0 ? -1 : 1);
  std::vector<std::pair<char, double>> weights;
  if (z != 0) weights.emplace_back('Z', z);
  hold(weights, z > 0 ? "Z+" : "Z-");
}
// A/B/C: tap in Step mode, hold in Live mode. The event code of the data pointer is axis<<1 | positive.
void extraEvent(lv_event_t* e) {
  const intptr_t id = reinterpret_cast<intptr_t>(lv_event_get_user_data(e));
  const char axis = static_cast<char>(id >> 1);
  const int direction = (id & 1) ? 1 : -1;
  const lv_event_code_t code = lv_event_get_code(e);
  if (!pendant::current().liveJog) {
    if (code == LV_EVENT_CLICKED) move(axis, direction);
    return;
  }
  if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) return endHold();
  if (code == LV_EVENT_DELETE) {
    if (holding) esp3dGcodeParser.jogRelease();
    holding = refused = false;
    return;
  }
  if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) {
    const char name[3] = {axis, direction > 0 ? '+' : '-', 0};
    hold({{axis, static_cast<double>(direction)}}, name);
  }
}
lv_obj_t* dial(lv_obj_t* parent, int x, int y, int w, int h, int radius, lv_event_cb_t callback) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_radius(o, radius, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(o, lv_color_hex(rgb::surface), 0);
  lv_obj_set_style_border_width(o, 2, 0);
  lv_obj_set_style_border_color(o, lv_color_hex(rgb::border), 0);
  lv_obj_clear_flag(o, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_CHAIN));
  lv_obj_add_flag(o, LV_OBJ_FLAG_PRESS_LOCK);  // keep feeding events when the finger slides past the rim
  lv_obj_add_event_cb(o, callback, LV_EVENT_ALL, nullptr);
  return o;
}
lv_obj_t* knob(lv_obj_t* parent, int w, int h, int radius) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_radius(o, radius, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(o, lv_color_hex(rgb::accent), 0);
  lv_obj_clear_flag(o, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  lv_obj_align(o, LV_ALIGN_CENTER, 0, 0);
  return o;
}
void edgeLabel(lv_obj_t* parent, const char* text, lv_align_t align, int dx, int dy) {
  lv_obj_align(label(parent, text, Font::tiny, rgb::muted), align, dx, dy);
}
void applyMode() {
  const bool live = pendant::current().liveJog;
  for (lv_obj_t* o : stepGroup) setHidden(o, live);
  for (lv_obj_t* o : liveGroup) setHidden(o, !live);
  setText(feedCaption, live ? "Max /min" : "Feed /min");
}
void paintLive(bool ready) {
  liveReady = ready;
  const uint32_t face = ready ? rgb::surface : rgb::bg, edge = ready ? rgb::border : rgb::surface;
  for (lv_obj_t* o : {stick, strip}) {
    lv_obj_set_style_bg_color(o, lv_color_hex(face), 0);
    lv_obj_set_style_border_color(o, lv_color_hex(edge), 0);
  }
  for (lv_obj_t* o : {stickKnob, stripKnob}) lv_obj_set_style_bg_color(o, lv_color_hex(ready ? rgb::accent : rgb::border), 0);
}

void build(lv_obj_t* parent) {
  lv_obj_set_style_pad_all(parent, kPad, 0);
  lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
  moveButtons.clear();
  stepGroup.clear();
  liveGroup.clear();
  holding = refused = false;
  liveReady = true;
  Settings& s = pendant::current();
  std::vector<std::string> steps, feeds;
  for (unsigned i = 0; i < kPresets; ++i) {
    steps.push_back(compact(s.steps[i]));
    feeds.push_back(std::to_string(s.feeds[i]));
  }
  choiceRow(parent, {"Step", "Live"}, 0, kModeY, kInnerW, kModeH, s.liveJog ? 1 : 0, [](int i) {
    pendant::current().liveJog = i == 1;
    pendant::save();
    applyMode();
  });
  stepGroup.push_back(label(parent, "Step mm", Font::tiny, rgb::muted));
  lv_obj_set_pos(stepGroup.back(), 0, kStepY + 10);
  Choice stepChoice = choiceRow(parent, steps, kChipX, kStepY, kInnerW - kChipX, kChipH, s.step,
                                [](int i) { pendant::current().step = i; pendant::save(); });
  stepGroup.push_back(lv_obj_get_parent(stepChoice.items[0]));
  feedCaption = label(parent, "Feed /min", Font::tiny, rgb::muted);
  lv_obj_set_pos(feedCaption, 0, kFeedY + 10);
  choiceRow(parent, feeds, kChipX, kFeedY, kInnerW - kChipX, kChipH, s.feed, [](int i) { pendant::current().feed = i; pendant::save(); });

  // Step mode: buttons.
  const int top = kPadTop, c = kCell, g = kGap;
  jogButton(parent, LV_SYMBOL_UP "\nY+", c + g, top, c, c, 'Y', 1);
  jogButton(parent, LV_SYMBOL_LEFT "\nX-", 0, top + c + g, c, c, 'X', -1);
  jogButton(parent, LV_SYMBOL_RIGHT "\nX+", 2 * (c + g), top + c + g, c, c, 'X', 1);
  jogButton(parent, LV_SYMBOL_DOWN "\nY-", c + g, top + 2 * (c + g), c, c, 'Y', -1);
  // Centre: cancels a jog in progress; always available.
  stepGroup.push_back(actionButton(parent, "STOP", c + g, top + c + g, c, c, Kind::danger,
                                   []() { esp3dGcodeParser.send("\x85"); }, Font::small));
  const int zh = (kPadSize - g) / 2;
  jogButton(parent, LV_SYMBOL_UP "\nZ+", kStripX, top, kStripW, zh, 'Z', 1);
  jogButton(parent, LV_SYMBOL_DOWN "\nZ-", kStripX, top + zh + g, kStripW, zh, 'Z', -1);

  // Live mode: a stick for X/Y, a strip for Z and a readout of what is being sent.
  readout = label(parent, "Hold and drag to jog", Font::small, rgb::accent);
  lv_obj_set_pos(readout, 0, kStepY + 8);
  liveGroup.push_back(readout);
  stick = dial(parent, 0, top, kPadSize, kPadSize, LV_RADIUS_CIRCLE, stickEvent);
  stickKnob = knob(stick, kKnob, kKnob, LV_RADIUS_CIRCLE);
  edgeLabel(stick, "Y+", LV_ALIGN_TOP_MID, 0, 6);
  edgeLabel(stick, "Y-", LV_ALIGN_BOTTOM_MID, 0, -6);
  edgeLabel(stick, "X-", LV_ALIGN_LEFT_MID, 8, 0);
  edgeLabel(stick, "X+", LV_ALIGN_RIGHT_MID, -8, 0);
  liveGroup.push_back(stick);
  strip = dial(parent, kStripX, top, kStripW, kPadSize, 18, stripEvent);
  stripKnob = knob(strip, kStripW - 12, kKnob, 14);
  edgeLabel(strip, "Z+", LV_ALIGN_TOP_MID, 0, 6);
  edgeLabel(strip, "Z-", LV_ALIGN_BOTTOM_MID, 0, -6);
  liveGroup.push_back(strip);

  // A/B/C rows (shown only for axes the controller reports): tap or hold depending on the mode.
  for (int n = 0; n < kExtra; ++n) {
    extraRow[n] = lv_obj_create(parent);
    lv_obj_remove_style_all(extraRow[n]);
    lv_obj_set_pos(extraRow[n], n * (kInnerW / kExtra), kExtraY);
    lv_obj_set_size(extraRow[n], kInnerW / kExtra - 4, 40);
    lv_obj_clear_flag(extraRow[n], LV_OBJ_FLAG_SCROLLABLE);
    const char axis = "ABC"[n];
    const char names[2][3] = {{axis, '-', 0}, {axis, '+', 0}};
    const int half = (kInnerW / kExtra - 4 - 4) / 2;
    for (int side = 0; side < 2; ++side) {
      lv_obj_t* b = button(extraRow[n], names[side], side * (half + 4), 0, half, 40, Kind::neutral);
      lv_obj_add_event_cb(b, extraEvent, LV_EVENT_ALL, reinterpret_cast<void*>(static_cast<intptr_t>((axis << 1) | side)));
      extraButton[n * 2 + side] = b;
      moveButtons.push_back(b);
    }
    lv_obj_add_flag(extraRow[n], LV_OBJ_FLAG_HIDDEN);
  }
  applyMode();
}
void update(const grbl::Status& s, uint64_t now) {
  const bool stepReady = s.idle(now);
  // While a live jog is fed the state alternates between Idle and Jog; that is not "not ready".
  const bool live = pendant::current().liveJog;
  const bool ready = stepReady || s.jogHeld || (holding && s.fresh(now) && (s.state == "Idle" || s.state == "Jog"));
  for (lv_obj_t* b : moveButtons) {
    const bool extra = std::find(std::begin(extraButton), std::end(extraButton), b) != std::end(extraButton);
    setEnabled(b, extra && live ? ready : stepReady);
  }
  if (live && ready != liveReady) paintLive(ready);
  const unsigned axes = std::max(s.machineAxes, s.workAxes);
  for (int n = 0; n < kExtra; ++n) setHidden(extraRow[n], static_cast<unsigned>(n) + 3 >= axes);
}
void release() {
  // The widgets are about to go; only make sure the machine is not left moving.
  if (holding) esp3dGcodeParser.jogRelease();
  holding = refused = false;
  moveButtons.clear();
  stepGroup.clear();
  liveGroup.clear();
  feedCaption = readout = stick = stickKnob = strip = stripKnob = nullptr;
  for (int n = 0; n < kExtra; ++n) extraRow[n] = nullptr;
  std::fill(std::begin(extraButton), std::end(extraButton), nullptr);
}
}
const Tab jogTab = {"Jog", LV_SYMBOL_JOG, build, update, release};
}
}
