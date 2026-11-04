// Drives the real pendant UI (real LVGL, 320x480) against a simulated controller.
#include <lvgl.h>
#include "esp3d_gcode_parser_service.h"
#include "pendant_settings.h"
#include "screens/main_screen.h"
#include "bsp.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>
#if defined(__has_include)
#if __has_include(<sanitizer/allocator_interface.h>)
#include <sanitizer/allocator_interface.h>
#define HAVE_ALLOC_STATS 1
#endif
#endif

static void failure(const char* expression, int line);
#define check(condition) do { if (!(condition)) failure(#condition, __LINE__); } while (0)

namespace pendant {
static std::string stored;
static bool present = false;
bool storageRead(std::string& text) { text = stored; return present; }
bool storageWrite(const std::string& text) { stored = text; present = true; return true; }
}

static const int W = 320, H = 480;
static std::vector<unsigned char> pixels(W * H * 3);
static std::string outDir, statusLine;
static auto& parser = esp3dGcodeParser;

static void flush(lv_disp_drv_t* driver, const lv_area_t* a, lv_color_t* colors) {
  for (int y = a->y1; y <= a->y2; ++y) for (int x = a->x1; x <= a->x2; ++x) {
    auto c = lv_color_to32(*colors++);
    if (x >= 0 && y >= 0 && x < W && y < H) {
      size_t p = (y * W + x) * 3;
      pixels[p] = (c >> 16) & 255; pixels[p + 1] = (c >> 8) & 255; pixels[p + 2] = c & 255;
    }
  }
  lv_disp_flush_ready(driver);
}
// Advances time; the simulated controller repeats its status report every 250 ms.
static void pump(unsigned ms = 250) {
  for (unsigned t = 0; t < ms; t += 50) {
    lv_tick_inc(50);
    if (!statusLine.empty() && lv_tick_get() % 250 < 50) parser.session.ingest(statusLine.c_str(), lv_tick_get());
    lv_timer_handler();
  }
  lv_refr_now(nullptr);
}
// A simulated finger, read by a real LVGL pointer device.
static lv_indev_state_t fingerState = LV_INDEV_STATE_RELEASED;
static lv_point_t fingerAt = {0, 0};
static void readFinger(lv_indev_drv_t*, lv_indev_data_t* data) { data->state = fingerState; data->point = fingerAt; }
static void finger(int x, int y) { fingerAt = {static_cast<lv_coord_t>(x), static_cast<lv_coord_t>(y)}; fingerState = LV_INDEV_STATE_PRESSED; }
static void lift() { fingerState = LV_INDEV_STATE_RELEASED; }
static void reply(const std::string& line) { parser.session.ingest(line.c_str(), lv_tick_get()); pump(50); }
static void status(const std::string& line) { statusLine = line; reply(line); pump(); }

static bool hidden(lv_obj_t* obj) {
  for (; obj; obj = lv_obj_get_parent(obj)) if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) return true;
  return false;
}
static int minY = 0;  // ignore matches above this screen row (e.g. the header)
static lv_obj_t* search(lv_obj_t* obj, const char* text, bool exact) {
  if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) return nullptr;
  if (lv_obj_check_type(obj, &lv_label_class)) {
    const char* t = lv_label_get_text(obj);
    lv_area_t area; lv_obj_get_coords(obj, &area);
    if (area.y1 >= minY && (exact ? !strcmp(t, text) : strstr(t, text) != nullptr)) return obj;
  }
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(obj); ++i)
    if (auto found = search(lv_obj_get_child(obj, i), text, exact)) return found;
  return nullptr;
}
// First visible label with this text (top layer first, as that is what a finger hits).
static lv_obj_t* label(const char* text, bool exact = true) {
  if (auto l = search(lv_layer_top(), text, exact)) return l;
  return search(lv_scr_act(), text, exact);
}
static lv_obj_t* widget(const char* text, bool exact = true) {
  lv_obj_t* l = label(text, exact);
  while (l && !lv_obj_has_flag(l, LV_OBJ_FLAG_CLICKABLE)) l = lv_obj_get_parent(l);
  return l;
}
static bool exists(const char* text, bool exact = true) { return label(text, exact) != nullptr; }
static bool enabled(const char* text, bool exact = true) { auto w = widget(text, exact); check(w); return !lv_obj_has_state(w, LV_STATE_DISABLED); }
// Like a finger: ignored if the control is disabled or not there.
static bool tap(const char* text, bool exact = true) {
  lv_obj_t* w = widget(text, exact);
  if (!w || lv_obj_has_state(w, LV_STATE_DISABLED) || hidden(w)) return false;
  lv_event_send(w, LV_EVENT_CLICKED, nullptr);
  pump(300);
  return true;
}
// Taps a tab in the bottom bar.
static bool nav(const char* text) { minY = 432; bool r = tap(text); minY = 0; return r; }
static void shot(const char* name) {
  pump(50);
  std::string path = outDir + "/" + name + ".ppm";
  FILE* f = fopen(path.c_str(), "wb"); check(f);
  fprintf(f, "P6\n%d %d\n255\n", W, H);
  fwrite(pixels.data(), 1, pixels.size(), f); fclose(f);
}
static void failure(const char* expression, int line) {
  fprintf(stderr, "FAILED preview.cpp:%d: %s\n", line, expression);
  for (const std::string& w : parser.sent) { fprintf(stderr, "  sent:"); for (unsigned char c : w) fprintf(stderr, c < 32 || c > 126 ? " 0x%02X" : "%c", c); fprintf(stderr, "\n"); }
  fprintf(stderr, "  state=%s pending=%d jogHeld=%d message=%s\n", parser.session.status.state.c_str(), parser.session.status.pending,
          parser.session.status.jogHeld, parser.session.status.message.c_str());
  shot("failure");
  abort();
}
// Lets time pass in 10 ms steps like a real controller would: every status report arrives on time and
// every line gets its ok straight away. Returns how many chunks the pendant wrote (a chunk starts with $J).
static unsigned live(unsigned ms) {
  unsigned chunks = 0;
  for (unsigned t = 0; t < ms; t += 10) {
    const size_t before = parser.sent.size();
    lv_tick_inc(10);
    if (!statusLine.empty() && lv_tick_get() % 250 < 10) parser.session.ingest(statusLine.c_str(), lv_tick_get());
    lv_timer_handler();
    for (size_t i = before; i < parser.sent.size(); ++i) if (parser.sent[i].find("$J") != std::string::npos) ++chunks;
    check(!(parser.session.status.pending && parser.sent.size() > before + 1 && parser.sent.back().find("$J") != std::string::npos
            && parser.sent[parser.sent.size() - 2].find("$J") != std::string::npos));  // never two chunks in one tick
    if (parser.session.status.pending) parser.session.ingest("ok", lv_tick_get());
  }
  lv_refr_now(nullptr);
  return chunks;
}
static std::string last() { return parser.sent.empty() ? "" : parser.sent.back(); }
static void ok() { reply("ok"); pump(); }
static const char* IDLE = "<Idle|MPos:10.5,20,-30.25|WCO:1,2,3|FS:0,0|Ov:100,100,100>";

static void files(const std::string& path, const std::string& json) {
  check(last() == "$Files/ListGcode=" + path + "\n");
  reply("[JSON:" + json + "]");
  ok();
}

int main(int argc, char** argv) {
  check(argc == 2); outDir = argv[1];
  lv_init();
  static lv_color_t buffer[W * 40]; static lv_disp_draw_buf_t draw;
  lv_disp_draw_buf_init(&draw, buffer, nullptr, W * 40);
  static lv_disp_drv_t driver; lv_disp_drv_init(&driver);
  driver.hor_res = W; driver.ver_res = H; driver.draw_buf = &draw; driver.flush_cb = flush;
  lv_disp_drv_register(&driver);
  static lv_indev_drv_t pointerDriver; lv_indev_drv_init(&pointerDriver);
  pointerDriver.type = LV_INDEV_TYPE_POINTER; pointerDriver.read_cb = readFinger;
  lv_indev_drv_register(&pointerDriver);

#ifdef HAVE_ALLOC_STATS
  const size_t baseline = __sanitizer_get_current_allocated_bytes();
#endif
  // 1. Disconnected boot: nothing can be sent except the always-safe stop/hold.
  mainScreen::create(); pump();
  check(bspState.brightness == 100 && !bspState.touchX);
  check(exists("Disconnected") && exists("---", true));
  check(!enabled("Resume", false));
  shot("01-disconnected");

  // 2. Connected and idle.
  reply("Grbl 3.9 [FluidNC v3.9.5 (wifi) '$' for help]");
  status(IDLE);
  reply("[GC:G0 G54 G17 G21 G90 M5]"); pump();
  check(exists("Idle") && exists("G54") && exists("9.500") && exists("-30.250") && exists("FluidNC v3.9", false));
  shot("02-status");

  // 3. Jog.
  check(nav("Jog"));
  check(enabled("\nX+", false));
  parser.sent.clear();
  check(tap("\nX+", false) && last() == "$J=G91 G21 X1.000 F500\n");
  check(!tap("\nY+", false)); // one outstanding command at a time
  ok();
  status(IDLE);
  check(tap("10") && tap("\nY-", false) && last() == "$J=G91 G21 Y-10.000 F500\n");
  check(pendant::parse(pendant::stored).step == 2);
  ok(); status(IDLE);
  check(tap("1000") && tap("\nZ+", false) && last() == "$J=G91 G21 Z10.000 F1000\n");
  ok(); status(IDLE);
  shot("03-jog");
  status("<Run|MPos:10.5,20,-30.25|WCO:1,2,3|FS:500,0>");
  check(!enabled("\nX+", false) && enabled("STOP"));
  check(tap("STOP") && last() == "\x85");
  // An axis beyond Z shows its own buttons.
  status("<Idle|MPos:1,2,3,4|WCO:0,0,0,0>");
  check(exists("A-") && exists("A+") && !exists("B-"));
  check(tap("A+") && last() == "$J=G91 G21 A10.000 F1000\n");
  ok(); status(IDLE);

  // 3b. Live jog: a held stick / strip / button moves the machine until the finger lifts.
  check(tap("Live"));
  check(pendant::parse(pendant::stored).liveJog);
  check(exists("Hold and drag to jog") && !exists("STOP") && !exists("\nX+", false) && exists("Max /min"));
  lv_area_t area;
  lv_obj_get_coords(lv_obj_get_parent(label("Y+")), &area);
  const int cx = (area.x1 + area.x2) / 2, cy = (area.y1 + area.y2) / 2;
  lv_obj_get_coords(lv_obj_get_parent(label("Z+")), &area);
  const int zx = (area.x1 + area.x2) / 2, zy = (area.y1 + area.y2) / 2;
  check(cx < 160 && zx > 200 && cy > 200 && cy < 300);
  const char* JOGGING = "<Jog|MPos:15,20,-30.25|WCO:1,2,3|FS:1000,0>";
  statusLine = JOGGING; parser.sent.clear();
  finger(cx + 70, cy);                                  // full deflection to the right
  live(30);
  check(parser.sent.size() >= 1 && parser.sent.size() <= 2 && parser.sent[0] == "$J=G91 G21 X1.667 F1000\n");
  check(parser.session.status.jogHeld && exists("X+", false) && exists("1000 mm/min", false));
  const unsigned chunks = 1 + live(1000);               // a second of holding: ten 100 ms chunks, one at a time
  check(chunks >= 9 && chunks <= 12);
  check(exists("Jog") && exists("X+", false));
  shot("03b-live-jog");
  finger(cx + 300, cy - 400);                           // sliding far off the screen keeps full speed along the nearest axis
  check(live(300) >= 2);
  lift(); live(60);
  check(last() == "\x85" && !parser.session.status.jogHeld && exists("Hold and drag to jog"));
  const size_t sentSoFar = parser.sent.size();
  live(500);
  check(parser.sent.size() == sentSoFar);               // nothing more once lifted
  // Dead zone: a touch at the centre does not move anything. Half deflection is quarter speed or less.
  statusLine = IDLE; live(300);
  finger(cx + 3, cy + 2); live(300);
  check(parser.sent.size() == sentSoFar && exists("Centre", false));
  finger(cx + 30, cy + 2); live(150);                   // half way, nearly on the X axis: X only, well below full speed
  check(parser.sent.size() > sentSoFar && parser.sent[sentSoFar].find("$J=G91 G21 X") == 0 &&
        parser.sent[sentSoFar].find("Y") == std::string::npos && parser.sent[sentSoFar] != "$J=G91 G21 X1.667 F1000\n");
  lift(); live(60); check(last() == "\x85");
  statusLine = IDLE; live(300);
  finger(cx + 40, cy - 40); live(150);                  // diagonal: both axes, positive
  check(parser.sent[parser.sent.size() - 1].find("X") != std::string::npos || parser.sent[parser.sent.size() - 2].find("X") != std::string::npos);
  { bool both = false; for (const auto& w : parser.sent) both = both || (w.find(" X") != std::string::npos && w.find(" Y") != std::string::npos && w.find("-") == std::string::npos); check(both); }
  lift(); live(60); statusLine = IDLE; live(300);
  // Z strip: up is Z+, and the strip releases like the stick.
  parser.sent.clear();
  finger(zx, zy - 70); live(30);
  check(parser.sent.size() >= 1 && parser.sent.size() <= 2 && parser.sent[0] == "$J=G91 G21 Z1.667 F1000\n");
  lift(); live(60); check(last() == "\x85");
  statusLine = IDLE; live(300);
  parser.sent.clear();
  finger(zx, zy + 70); live(30);
  check(parser.sent.size() >= 1 && parser.sent.size() <= 2 && parser.sent[0] == "$J=G91 G21 Z-1.667 F1000\n");
  lift(); live(60); check(last() == "\x85");
  // A halt (alarm, limit...) while the finger is still down stops the feeding and cancels once.
  statusLine = IDLE; live(300);
  parser.sent.clear();
  finger(cx + 70, cy); live(150);
  statusLine = "<Alarm|MPos:15,20,-30.25|WCO:1,2,3>"; reply("ALARM:1"); live(60);
  const size_t atAlarm = parser.sent.size();
  check(parser.sent.back() == "\x85" && !parser.session.status.jogHeld);
  live(500);
  check(parser.sent.size() == atAlarm && exists("Hard limit", false));
  lift(); live(60);
  check(parser.sent.size() == atAlarm);                 // the cancel was already sent
  // Not idle: the press is refused with a reason and nothing is sent.
  statusLine = "<Run|MPos:15,20,-30.25|WCO:1,2,3|FS:500,0>"; live(300);
  parser.sent.clear();
  finger(cx + 70, cy); live(100);
  check(parser.sent.empty() && exists("Jog needs an idle machine"));
  lift(); live(60);
  statusLine = IDLE; live(300);
  // Leaving the tab while a finger is down stops the machine.
  parser.sent.clear();
  finger(cx + 70, cy); live(150);
  check(parser.session.status.jogHeld);
  check(nav("Status"));
  check(!parser.session.status.jogHeld && last() == "\x85");
  lift(); live(60);
  check(nav("Jog") && exists("Hold and drag to jog"));   // the mode is remembered
  // A/B/C buttons hold too.
  statusLine = "<Idle|MPos:1,2,3,4|WCO:0,0,0,0>"; live(300);
  parser.sent.clear();
  lv_obj_get_coords(widget("A+"), &area);
  finger((area.x1 + area.x2) / 2, (area.y1 + area.y2) / 2); live(30);
  check(parser.sent.size() >= 1 && parser.sent.size() <= 2 && parser.sent[0] == "$J=G91 G21 A1.667 F1000\n");
  lift(); live(60); check(last() == "\x85");
  // Back to step mode: the buttons return and tap once as before.
  statusLine = IDLE; live(300);
  check(tap("Step"));
  check(!pendant::parse(pendant::stored).liveJog && exists("STOP") && !exists("Hold and drag to jog"));
  parser.sent.clear();
  check(tap("\nX+", false) && last() == "$J=G91 G21 X10.000 F1000\n");
  reply("ok"); status(IDLE);

  // 4. Hold / resume / reset are reachable from every tab.
  check(tap("Hold", false) && last() == "!");
  status("<Hold:0|MPos:10.5,20,-30.25|WCO:1,2,3>");
  check(exists("Hold") && enabled("Resume", false));
  check(tap("Resume", false) && exists("Resume job?"));
  check(last() == "!");                      // nothing sent before the confirmation
  check(tap("Resume") && last() == "~");
  status(IDLE);
  check(!enabled("Resume", false));
  check(tap("Reset", false) && exists("Reset controller?"));
  shot("04-confirm");
  check(tap("Cancel") && last() == "~");
  check(tap("Reset", false) && tap("Reset") && last() == "\x18");
  check(exists("Resetting...") && !exists("Reset required"));  // uncertain until the controller restarts
  reply("Grbl 3.9 [FluidNC v3.9.5 (wifi) '$' for help]");
  status(IDLE); reply("[GC:G0 G54 G17 G21 G90 M5]");

  // 5. Tools.
  check(nav("Tools"));
  shot("05-tools");
  check(tap("Home all", false) && exists("Home all axes?"));
  check(last() == "\x18");
  check(tap("Home") && last() == "$H\n");
  status("<Home|MPos:0,0,0>"); ok(); status(IDLE);
  minY = 44;  // below the header, which also shows the active G54..G59
  check(tap("G55") && last() == "G55\n");
  ok(); status(IDLE); reply("[GC:G0 G55 G17 G21 G90 M5]"); pump();
  minY = 44;
  check(lv_obj_has_state(widget("G55"), LV_STATE_CHECKED) && !lv_obj_has_state(widget("G54"), LV_STATE_CHECKED));
  minY = 0;
  check(tap("Zero X", false) && exists("Set axis X to zero?"));
  check(tap("Set zero") && last() == "G10 L20 P0 X0\n");
  ok(); status(IDLE);
  check(tap("Zero XYZ", false) && tap("Set zero") && last() == "G10 L20 P0 X0 Y0 Z0\n");
  ok(); status(IDLE);
  check(tap("(M5)", false) && last() == "M5\n");
  ok(); status(IDLE);
  check(!exists("Terminal", false));
  check(tap("+10%") && last() == "\x91");   // feed override works while a job runs, too
  status("<Run|MPos:1,2,3|Ov:110,100,100>");
  check(exists("Feed 110%") && !enabled("Home all", false) && enabled("+10%"));
  status(IDLE);

  // 6. Job: files on the controller.
  check(nav("Job"));
  check(exists("Loading...") || exists("No G-code", false) == false);
  files("/sd/", "{\"files\":[{\"name\":\"zebra.nc\",\"size\":\"20480\"},{\"name\":\"fluidtouch\",\"size\":\"-1\"},"
                "{\"name\":\"Alpha part with a very long file name.gcode\",\"size\":\"1536\"},{\"name\":\"b.nc\",\"size\":\"7\"},"
                "{\"name\":\"c.nc\",\"size\":\"7\"},{\"name\":\"d.nc\",\"size\":\"2097152\"}],\"path\":\"/sd\"}");
  status(IDLE);
  check(exists("fluidtouch") && exists("zebra.nc") == false && exists("1/2"));
  shot("06-files");
  check(tap("fluidtouch"));
  files("/sd/fluidtouch/", "{\"files\":[{\"name\":\"probe.nc\",\"size\":\"88\"}]}");
  status(IDLE);
  check(exists("/sd/fluidtouch/") && exists("probe.nc"));
  check(tap(LV_SYMBOL_UP) && last() == "$Files/ListGcode=/sd/\n");
  files("/sd/", "{\"files\":[{\"name\":\"zebra.nc\",\"size\":\"20480\"},{\"name\":\"fluidtouch\",\"size\":\"-1\"}]}");
  status(IDLE);
  check(tap("zebra.nc") && exists("Run job"));
  shot("07-file-sheet");
  check(tap("Save as macro", false) && exists("Save as macro"));
  check(tap("1: empty") && pendant::current().macros[0].path == "/sd/zebra.nc" && pendant::current().macros[0].name == "zebra.nc");
  check(tap("zebra.nc") && tap("Run job") && last() == "$SD/Run=/sd/zebra.nc\n");
  check(exists("Starting zebra.nc"));
  ok();
  status("<Run|MPos:5,5,5|SD:42.5,/sd/zebra.nc>");
  check(exists("42%") || exists("43%"));
  status(IDLE);
  // Back to the Job tab: the folder list is requested again and the folder is remembered.
  check(nav("Job") && last() == "$Files/ListGcode=/sd/\n");
  files("/sd/", "{\"files\":[{\"name\":\"zebra.nc\",\"size\":\"20480\"}]}");
  status(IDLE);
  // Browse flash, and a controller error.
  check(tap("Flash", false) && last() == "$Files/ListGcode=/localfs/\n");
  reply("error:63"); pump(); status(IDLE);
  check(exists("error:63 SD directory not found") );
  // Macros.
  check(tap("Macros"));
  check(exists("zebra.nc") && exists("Add", false));
  shot("08-macros");
  check(tap("zebra.nc") && exists("Run macro?"));
  check(tap("Run") && last() == "$SD/Run=/sd/zebra.nc\n");
  ok(); status(IDLE);
  // Running a macro jumps to Status; come back (the Macros page is remembered) and edit a slot.
  check(nav("Job"));
  status(IDLE);
  check(widget("Add", false));
  lv_event_send(widget("Add", false), LV_EVENT_CLICKED, nullptr); pump(300);
  check(exists("Macro 2"));
  shot("09-macro-editor");
  {
    std::vector<lv_obj_t*> areas;
    std::function<void(lv_obj_t*)> collect = [&](lv_obj_t* o) {
      if (lv_obj_check_type(o, &lv_textarea_class)) areas.push_back(o);
      for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); ++i) collect(lv_obj_get_child(o, i));
    };
    collect(lv_layer_top());
    check(areas.size() == 2);
    lv_textarea_set_text(areas[0], "Park");
    lv_textarea_set_text(areas[1], "/sd/fluidtouch/macros/park.nc");
  }
  check(tap("Save"));
  check(pendant::current().macros[1].name == "Park" && pendant::parse(pendant::stored).macros[1].path == "/sd/fluidtouch/macros/park.nc");
  check(exists("Park"));

  // 7. Setup.
  check(nav("Setup"));
  shot("10-setup");
  {
    lv_obj_t* slider = nullptr;
    std::function<void(lv_obj_t*)> find = [&](lv_obj_t* o) {
      if (lv_obj_check_type(o, &lv_slider_class)) slider = o;
      for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); ++i) find(lv_obj_get_child(o, i));
    };
    find(lv_scr_act());
    check(slider);
    lv_slider_set_value(slider, 40, LV_ANIM_OFF);
    lv_event_send(slider, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_event_send(slider, LV_EVENT_RELEASED, nullptr);
  }
  check(bspState.brightness == 40 && pendant::parse(pendant::stored).brightness == 40 && exists("40%"));
  check(tap("1m") && pendant::current().sleepMinutes == 1);
  check(tap("Inches") && pendant::current().reportInches);
  check(tap("Millimetres") && !pendant::current().reportInches);
  // Number pad: change the first step preset.
  check(tap("0.1") && exists("Step preset (mm)"));
  shot("11-numpad");
  {
    lv_obj_t* field = nullptr;
    std::function<void(lv_obj_t*)> find = [&](lv_obj_t* o) {
      if (lv_obj_check_type(o, &lv_textarea_class)) field = o;
      for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); ++i) find(lv_obj_get_child(o, i));
    };
    find(lv_layer_top());
    check(field);
    lv_textarea_set_text(field, "0.05");
    lv_obj_t* keys = nullptr;
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(lv_obj_get_parent(field)); ++i) (void)i;
    std::function<void(lv_obj_t*)> findKeys = [&](lv_obj_t* o) {
      if (lv_obj_check_type(o, &lv_keyboard_class)) keys = o;
      for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); ++i) findKeys(lv_obj_get_child(o, i));
    };
    findKeys(lv_layer_top());
    check(keys);
    lv_event_send(keys, LV_EVENT_READY, nullptr);
    pump(300);
  }
  check(pendant::current().steps[0] == 0.05 && exists("0.05"));
  // Terminal is opt-in and warns first.
  check(tap("On") && exists("Enable terminal?"));
  check(!pendant::current().terminal);
  check(tap("Cancel") && !pendant::current().terminal);
  check(tap("On") && tap("Enable") && pendant::current().terminal);
  check(tap("Display and touch test"));
  check(tap("Invert touch X") && bspState.touchX && pendant::current().touchFlipX);
  check(tap("Mirror display Y") && bspState.flipY);
  shot("12-display-test");
  check(tap("Done"));
  check(nav("Tools") && tap("Terminal", false));
  shot("13-terminal-empty");
  {
    lv_obj_t* input = nullptr;
    std::function<void(lv_obj_t*)> find = [&](lv_obj_t* o) {
      if (lv_obj_check_type(o, &lv_textarea_class)) input = o;
      for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); ++i) find(lv_obj_get_child(o, i));
    };
    find(lv_layer_top());
    check(input);
    lv_event_send(input, LV_EVENT_CLICKED, nullptr); pump(100);
    lv_textarea_set_text(input, "$I");
    check(tap("Send"));
    check(!parser.terminalSent.empty() && parser.terminalSent.back() == "$I\n");
    reply("[VER:3.9 FluidNC v3.9.5:]"); reply("[OPT:PHRV,35,1024]"); ok(); status(IDLE);
    pump(300);
    shot("14-terminal");
  }
  check(exists("> $I", false) && exists("[VER:3.9 FluidNC v3.9.5:]", false));
  check(tap("Close"));

  // 8. Idle dimming and wake-up.
  status(IDLE);
  check(bspState.brightness == 40);
  for (int i = 0; i < 70; ++i) pump(1000);
  check(bspState.brightness == 3);
  status("<Run|MPos:1,2,3>");   // the machine starts moving: wake immediately, never hide Hold behind a wake tap
  check(bspState.brightness == 40);
  status(IDLE);
  for (int i = 0; i < 70; ++i) pump(1000);
  check(bspState.brightness == 3);
  lv_obj_t* wake = lv_obj_get_child(lv_layer_top(), lv_obj_get_child_cnt(lv_layer_top()) - 1);
  parser.sent.clear();
  lv_event_send(wake, LV_EVENT_PRESSED, nullptr); pump(100);
  check(bspState.brightness == 40 && parser.sent.empty());

  // 9. Messages and errors raise a toast.
  reply("error:9"); pump(300);   // the UI refreshes every 200 ms
  check(exists("error:9 Locked", false));
  shot("15-toast");

  // 10. Rebuilding the screen repeatedly leaves no dialogs or timers behind.
  for (int i = 0; i < 25; ++i) { mainScreen::create(); pump(); }
  check(lv_obj_get_child_cnt(lv_layer_top()) == 0);
  // 11. Memory: each tab's footprint, and no growth when tabs and dialogs are cycled.
#ifdef HAVE_ALLOC_STATS
  {
    status(IDLE);
    mainScreen::create(); pump();
    pump(500);
    const char* tabs[] = {"Status", "Jog", "Job", "Tools", "Setup"};
    size_t worst = 0;
    for (const char* t : tabs) {
      check(nav("Status"));
      pump(300);
      const size_t before = __sanitizer_get_current_allocated_bytes();
      check(nav(t));
      pump(300);
      const size_t used = __sanitizer_get_current_allocated_bytes() - before;
      printf("heap for the %-6s tab: %6zu bytes (allocator view, before LVGL object overhead differences)\n", t, used);
      worst = used > worst ? used : worst;
    }
    check(worst < 48 * 1024);
    size_t settled = 0;
    for (int round = 0; round < 40; ++round) {
      for (const char* t : tabs) { check(nav(t)); pump(300); status(IDLE); }
      check(nav("Tools"));
      check(tap("Home all", false) && tap("Cancel"));
      pump(300);
      if (round == 4) settled = __sanitizer_get_current_allocated_bytes();
    }
    const size_t finalBytes = __sanitizer_get_current_allocated_bytes();
    printf("heap after 5 rounds: %zu, after 40 rounds: %zu\n", settled, finalBytes);
    printf("whole UI (LVGL objects, styles, theme, strings, session): about %zu KB above the pre-UI baseline\n", (finalBytes - baseline) / 1024);
    check(finalBytes <= settled + 8 * 1024);
  }
#endif
  puts("LVGL pendant UI passed");
}
