// Status tab: position readout, motion, job progress and controller details.
#include <algorithm>
#include "pendant_settings.h"
#include "pendant_ui.h"

namespace pendant {
namespace ui {
namespace {
constexpr int kAxes = 6, kRowH = 38;
const char kAxisName[] = "XYZABC";
const uint32_t kAxisColor[kAxes] = {rgb::bad, rgb::good, 0x58A6FF, rgb::warn, rgb::violet, rgb::info};

lv_obj_t* axisRow[kAxes] = {};
lv_obj_t *workLabel[kAxes] = {}, *machineLabel[kAxes] = {}, *columnWork = nullptr, *columnMachine = nullptr;
lv_obj_t *feedValue, *spindleValue, *overrideValue, *jobFile, *jobPercent, *jobBar, *modalValue, *pinsValue, *firmwareValue;

void build(lv_obj_t* parent) {
  scrollColumn(parent);

  lv_obj_t* dro = flexCard(parent);
  lv_obj_t* head = rowBox(dro, 18);
  columnWork = label(head, "WORK", Font::tiny, rgb::muted);
  lv_obj_align(columnWork, LV_ALIGN_TOP_RIGHT, -112, 2);
  columnMachine = label(head, "MACHINE", Font::tiny, rgb::muted);
  lv_obj_align(columnMachine, LV_ALIGN_TOP_RIGHT, 0, 2);
  for (int i = 0; i < kAxes; ++i) {
    axisRow[i] = rowBox(dro, kRowH);
    char name[2] = {kAxisName[i], 0};
    label(axisRow[i], name, Font::title, kAxisColor[i]);
    workLabel[i] = label(axisRow[i], "---", Font::big);
    lv_obj_set_width(workLabel[i], 170);
    lv_obj_set_style_text_align(workLabel[i], LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(workLabel[i], LV_ALIGN_RIGHT_MID, -100, 0);
    machineLabel[i] = label(axisRow[i], "---", Font::body, rgb::muted);
    lv_obj_set_width(machineLabel[i], 96);
    lv_obj_set_style_text_align(machineLabel[i], LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(machineLabel[i], LV_ALIGN_RIGHT_MID, 0, 4);
    if (i >= 3) lv_obj_add_flag(axisRow[i], LV_OBJ_FLAG_HIDDEN);
  }

  lv_obj_t* motion = flexCard(parent);
  feedValue = keyValue(motion, "Feed");
  spindleValue = keyValue(motion, "Spindle / laser S");
  overrideValue = keyValue(motion, "Override F / R / S");

  lv_obj_t* job = flexCard(parent);
  jobFile = keyValue(job, "Job");
  lv_obj_t* progress = rowBox(job, 22);
  jobBar = bar(progress, 0, 7, 230, 8, rgb::accent);
  jobPercent = label(progress, "", Font::body);
  lv_obj_align(jobPercent, LV_ALIGN_RIGHT_MID, 0, 0);

  lv_obj_t* machine = flexCard(parent);
  modalValue = keyValue(machine, "Modal");
  pinsValue = keyValue(machine, "Active pins");
  firmwareValue = keyValue(machine, "Controller");
}
void update(const grbl::Status& s, uint64_t now) {
  const bool fresh = s.fresh(now), inches = pendant::current().reportInches;
  const int decimals = inches ? 4 : 3;
  unsigned shown = std::max(s.machineAxes, s.workAxes);
  if (!fresh || shown < 3) shown = 3;
  setText(columnWork, std::string("WORK (") + lengthUnit(inches) + ")");
  for (int i = 0; i < kAxes; ++i) {
    setHidden(axisRow[i], static_cast<unsigned>(i) >= shown);
    setText(workLabel[i], fresh && static_cast<unsigned>(i) < s.workAxes ? number(s.work[i], decimals) : "---");
    setText(machineLabel[i], fresh && static_cast<unsigned>(i) < s.machineAxes ? number(s.machine[i], decimals) : "---");
  }
  setText(feedValue, fresh && s.hasFeed ? number(s.feed, 0) + " " + feedUnit(inches) : "---");
  setText(spindleValue, fresh && s.hasFeed ? number(s.spindle, 0) : "---");
  setText(overrideValue, fresh && s.hasOverrides
                             ? number(s.overrides[0], 0) + " / " + number(s.overrides[1], 0) + " / " + number(s.overrides[2], 0) + " %"
                             : "---");
  const bool job = fresh && s.sdRunning;
  setText(jobFile, job ? s.file : "No job running");
  setText(jobPercent, job ? number(s.progress, 0) + "%" : "");
  setHidden(jobBar, !job);
  setBar(jobBar, job ? static_cast<int>(s.progress) : 0);
  setText(modalValue, s.modal.empty() ? "waiting for $G" : s.modal);
  setText(pinsValue, fresh && !s.pins.empty() ? s.pins : "none");
  setText(firmwareValue, s.firmware.empty() ? "not seen since start" : s.firmware);
}
void release() {
  columnWork = columnMachine = nullptr;
  for (int i = 0; i < kAxes; ++i) axisRow[i] = workLabel[i] = machineLabel[i] = nullptr;
  feedValue = spindleValue = overrideValue = jobFile = jobPercent = jobBar = modalValue = pinsValue = firmwareValue = nullptr;
}
}
const Tab statusTab = {"Status", LV_SYMBOL_GAUGE, build, update, release};
}
}
