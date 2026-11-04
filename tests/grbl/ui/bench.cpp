// Render-cost benchmark: scrolls each tab with real LVGL and reports how much is drawn per frame.
// Host timing is only a relative guide (an ESP32 is tens of times slower); the counts are exact.
// Build in Release without sanitizers:  cmake -S tests/grbl/ui -B build/grbl-ui-bench -DCMAKE_BUILD_TYPE=Release
#include <lvgl.h>
#include "esp3d_gcode_parser_service.h"
#include "pendant_settings.h"
#include "pendant_ui.h"
#include "screens/main_screen.h"
#include "bsp.h"
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

namespace pendant {
static std::string stored;
bool storageRead(std::string& text) { text = stored; return !stored.empty(); }
bool storageWrite(const std::string& text) { stored = text; return true; }
}

static const int W = 320, H = 480;
static unsigned long flushedPx, strips, rectCalls, rectPx, bgCalls, letters, lines;
static void (*realRect)(lv_draw_ctx_t*, const lv_draw_rect_dsc_t*, const lv_area_t*);
static void (*realBg)(lv_draw_ctx_t*, const lv_draw_rect_dsc_t*, const lv_area_t*);
static void (*realLetter)(lv_draw_ctx_t*, const lv_draw_label_dsc_t*, const lv_point_t*, uint32_t);
static void (*realLine)(lv_draw_ctx_t*, const lv_draw_line_dsc_t*, const lv_point_t*, const lv_point_t*);

static void flush(lv_disp_drv_t* driver, const lv_area_t* a, lv_color_t*) {
  flushedPx += static_cast<unsigned long>(lv_area_get_width(a)) * lv_area_get_height(a);
  ++strips;
  lv_disp_flush_ready(driver);
}
static void countRect(lv_draw_ctx_t* ctx, const lv_draw_rect_dsc_t* dsc, const lv_area_t* coords) {
  lv_area_t clipped;
  if (_lv_area_intersect(&clipped, coords, ctx->clip_area))
    rectPx += static_cast<unsigned long>(lv_area_get_width(&clipped)) * lv_area_get_height(&clipped);
  ++rectCalls;
  realRect(ctx, dsc, coords);
}
static void countBg(lv_draw_ctx_t* ctx, const lv_draw_rect_dsc_t* dsc, const lv_area_t* coords) { ++bgCalls; realBg(ctx, dsc, coords); }
static void countLetter(lv_draw_ctx_t* ctx, const lv_draw_label_dsc_t* dsc, const lv_point_t* pos, uint32_t letter) {
  ++letters;
  realLetter(ctx, dsc, pos, letter);
}
static void countLine(lv_draw_ctx_t* ctx, const lv_draw_line_dsc_t* dsc, const lv_point_t* a, const lv_point_t* b) {
  ++lines;
  realLine(ctx, dsc, a, b);
}

static lv_obj_t* scroller(lv_obj_t* obj) {
  if (lv_obj_has_flag(obj, LV_OBJ_FLAG_SCROLLABLE) && lv_obj_get_scroll_bottom(obj) > 0) return obj;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(obj); ++i)
    if (lv_obj_t* found = scroller(lv_obj_get_child(obj, i))) return found;
  return nullptr;
}
static void settle() { for (int i = 0; i < 10; ++i) { lv_tick_inc(50); lv_timer_handler(); } lv_refr_now(nullptr); }

int main() {
  lv_init();
  static lv_color_t buffer[W * 20]; static lv_disp_draw_buf_t draw;
  lv_disp_draw_buf_init(&draw, buffer, nullptr, W * 20);  // same 20-line strips as the device
  static lv_disp_drv_t driver; lv_disp_drv_init(&driver);
  driver.hor_res = W; driver.ver_res = H; driver.draw_buf = &draw; driver.flush_cb = flush;
  lv_disp_drv_register(&driver);
  realRect = driver.draw_ctx->draw_rect; driver.draw_ctx->draw_rect = countRect;
  realBg = driver.draw_ctx->draw_bg; driver.draw_ctx->draw_bg = countBg;
  realLetter = driver.draw_ctx->draw_letter; driver.draw_ctx->draw_letter = countLetter;
  realLine = driver.draw_ctx->draw_line; driver.draw_ctx->draw_line = countLine;

  mainScreen::create(); settle();
  esp3dGcodeParser.session.ingest("Grbl 3.9 [FluidNC v3.9.5]", lv_tick_get());
  esp3dGcodeParser.session.ingest("<Idle|MPos:10.5,20,-30.25|WCO:1,2,3|FS:0,0|Ov:100,100,100>", lv_tick_get());
  settle();
  printf("%-8s %9s %9s %8s %8s %8s %8s %8s %9s\n", "tab", "px/frame", "overdraw", "strips", "rects", "bg", "glyphs", "lines", "host ms");
  const char* names[] = {"Status", "Jog", "Job", "Tools", "Setup"};
  for (int tab = 0; tab < 5; ++tab) {
    pendant::ui::showTab(tab); settle();
    lv_obj_t* column = scroller(lv_scr_act());
    if (!column) { printf("%-8s (does not scroll)\n", names[tab]); continue; }
    const int frames = 40;
    flushedPx = strips = rectCalls = rectPx = bgCalls = letters = lines = 0;
    double total = 0;
    for (int f = 0; f < frames; ++f) {
      lv_obj_scroll_by(column, 0, (f / 10) % 2 ? 6 : -6, LV_ANIM_OFF);
      auto t0 = std::chrono::steady_clock::now();
      lv_refr_now(nullptr);
      total += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    }
    printf("%-8s %9lu %8.1fx %8lu %8lu %8lu %8lu %8lu %9.2f\n", names[tab], flushedPx / frames,
           flushedPx ? static_cast<double>(rectPx) / flushedPx : 0.0, strips / frames, rectCalls / frames,
           bgCalls / frames, letters / frames, lines / frames, total / frames);
  }
  return 0;
}
