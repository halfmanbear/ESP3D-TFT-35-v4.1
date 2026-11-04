// Job tab: run files stored on the controller (SD card or flash) and one-tap macros.
#include <algorithm>
#include "esp3d_gcode_parser_service.h"
#include "pendant_settings.h"
#include "pendant_ui.h"

namespace pendant {
namespace ui {
namespace {
constexpr int kRows = 5, kRowH = 42, kRowGap = 2, kSegH = 34, kPageTop = kSegH + 4, kPageH = 288;
const char* const kRoots[] = {"/sd/", "/localfs/"};

bool macroPage = false, editing = false, wantList = true;
int storage = 0, pageIndex = 0;
std::string folder = kRoots[0];
grbl::FileList shown;
unsigned seenGeneration = 0;

lv_obj_t *filesPage, *macrosPage, *editButton, *upButton, *pathLabel, *pageLabel, *prevButton, *nextButton, *statusLabel;
lv_obj_t *rowButton[kRows], *rowIcon[kRows], *rowName[kRows], *rowSize[kRows];
lv_obj_t* macroButton[kMacros];
lv_obj_t* macroCaption[kMacros];
std::vector<lv_obj_t*> needIdle;
Choice storageChoice, modeChoice;
grbl::Status lastStatus;
uint64_t lastNow = 0;

bool isRoot() { return folder == kRoots[0] || folder == kRoots[1]; }
size_t pages() { return std::max<size_t>(1, (shown.entries.size() + kRows - 1) / kRows); }
std::string parentOf(const std::string& path) {
  std::string trimmed = path.substr(0, path.size() - 1);
  return trimmed.substr(0, trimmed.find_last_of('/') + 1);
}
std::string runCommand(const std::string& path) {
  return std::string(path.compare(0, 9, "/localfs/") == 0 ? "$LocalFS/Run=" : "$SD/Run=") + path;
}
void run(const std::string& name, const std::string& path) {
  if (esp3dGcodeParser.send(runCommand(path).c_str())) {
    showTab(0);  // closes dialogs and toasts, so announce afterwards
    toast("Starting " + name, false);
  } else {
    toast("Not started: machine must be idle", true);
  }
}
void renderList(const grbl::Status& s, uint64_t now);
void redraw() { renderList(lastStatus, lastNow); }
void load() { wantList = true; shown = grbl::FileList{}; pageIndex = 0; redraw(); }
void go(const std::string& target) { folder = target; load(); }

void saveAsMacro(const std::string& name, const std::string& path) {
  std::vector<std::string> cells;
  Settings& s = pendant::current();
  for (unsigned i = 0; i < kMacros; ++i) cells.push_back(std::to_string(i + 1) + (s.macros[i].used() ? ": " + s.macros[i].name : ": empty"));
  gridPicker("Save as macro", cells, [name, path](int slot) {
    Settings& s = pendant::current();
    s.macros[slot] = {name.substr(0, 24), path};
    pendant::sanitize(s);
    toast(pendant::save() ? "Saved as macro " + std::to_string(slot + 1) : "Could not save macro", false);
  });
}
void openFile(const grbl::FileEntry& entry) {
  const std::string path = folder + entry.name;
  sheet(entry.name, fileSize(entry.size) + "   " + folder + "\nMotion and laser output may start.",
        {{"Run job", Kind::success, [entry, path]() { run(entry.name, path); }},
         {"Save as macro", Kind::neutral, [entry, path]() { saveAsMacro(entry.name, path); }}});
}
void renderList(const grbl::Status& s, uint64_t now) {
  const size_t total = pages();
  pageIndex = std::min<int>(pageIndex, static_cast<int>(total) - 1);
  for (int i = 0; i < kRows; ++i) {
    const size_t index = static_cast<size_t>(pageIndex) * kRows + i;
    const bool present = shown.valid && index < shown.entries.size();
    setHidden(rowButton[i], !present);
    if (!present) continue;
    const grbl::FileEntry& e = shown.entries[index];
    setText(rowIcon[i], e.directory ? LV_SYMBOL_FOLDER : LV_SYMBOL_FILE);
    setColor(rowIcon[i], e.directory ? rgb::warn : rgb::muted);
    setText(rowName[i], e.name);
    setText(rowSize[i], e.directory ? "" : fileSize(e.size));
  }
  std::string message;
  if (s.listing) message = "Loading...";
  else if (shown.valid && shown.entries.empty()) message = "No G-code files here.";
  else if (!shown.valid && !shown.error.empty()) message = shown.error;
  else if (!shown.valid && !s.idle(now)) message = "The machine must be idle to browse files.";
  else if (!shown.valid) message = "Loading...";
  setText(statusLabel, message);
  setHidden(statusLabel, message.empty());
  setText(pathLabel, folder);
  setText(pageLabel, std::to_string(pageIndex + 1) + "/" + std::to_string(total));
  setEnabled(prevButton, pageIndex > 0);
  setEnabled(nextButton, static_cast<size_t>(pageIndex) + 1 < total);
  setEnabled(upButton, !isRoot());
}
void editMacro(int slot) {
  Settings& s = pendant::current();
  const Macro& m = s.macros[slot];
  textEditor("Macro " + std::to_string(slot + 1), {{"Name", m.name}, {"File on the controller (empty clears)", m.path.empty() ? "/sd/" : m.path}}, 120,
             [slot](const std::vector<std::string>& values) {
               Settings& s = pendant::current();
               std::string path = values[1];
               if (path == "/sd/" || path.empty()) {
                 s.macros[slot] = Macro{};
               } else if (!pendant::validMacroPath(path)) {
                 toast("Path must start with /sd/ or /localfs/", true);
                 return;
               } else {
                 s.macros[slot] = {values[0], path};
               }
               pendant::sanitize(s);
               pendant::save();
               reloadTab();
             });
}
void tapMacro(int slot) {
  const Macro m = pendant::current().macros[slot];
  if (editing || !m.used()) { editMacro(slot); return; }
  confirm("Run macro?", m.name + "\n" + m.path + "\nMotion and laser output may start.", "Run", Kind::success,
          [m]() { run(m.name, m.path); });
}
void setPage(bool macros) {
  macroPage = macros;
  setHidden(filesPage, macros);
  setHidden(macrosPage, !macros);
  setHidden(editButton, !macros);
  modeChoice.select(macros ? 1 : 0);
}
void build(lv_obj_t* parent) {
  needIdle.clear();
  lv_obj_set_style_pad_all(parent, kPad, 0);
  modeChoice = choiceRow(parent, {"Files", "Macros"}, 0, 0, 204, kSegH, macroPage ? 1 : 0, [](int i) { setPage(i == 1); });
  editButton = actionButton(parent, LV_SYMBOL_EDIT " Edit", 212, 0, 92, kSegH, Kind::neutral, []() {
    editing = !editing;
    setChecked(editButton, editing);
  }, Font::small);
  setChecked(editButton, editing);

  filesPage = rowBox(parent, kPageH);
  lv_obj_set_pos(filesPage, 0, kPageTop);
  lv_obj_set_width(filesPage, kInnerW);
  storageChoice = choiceRow(filesPage, {LV_SYMBOL_SD_CARD " SD card", LV_SYMBOL_SAVE " Flash"}, 0, 0, 236, 32, storage, [](int i) {
    storage = i;
    go(kRoots[i]);
  });
  actionButton(filesPage, LV_SYMBOL_REFRESH, 244, 0, 60, 32, Kind::neutral, []() { load(); });
  upButton = actionButton(filesPage, LV_SYMBOL_UP, 0, 36, 44, 30, Kind::neutral, []() { if (!isRoot()) go(parentOf(folder)); });
  pathLabel = label(filesPage, folder.c_str(), Font::small, rgb::muted);
  lv_obj_set_width(pathLabel, 120);
  lv_label_set_long_mode(pathLabel, LV_LABEL_LONG_DOT);
  lv_obj_set_pos(pathLabel, 52, 42);
  prevButton = actionButton(filesPage, LV_SYMBOL_LEFT, 180, 36, 40, 30, Kind::neutral, []() { if (pageIndex > 0) --pageIndex; redraw(); });
  pageLabel = label(filesPage, "1/1", Font::small, rgb::muted);
  lv_obj_set_width(pageLabel, 40);
  lv_obj_set_style_text_align(pageLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(pageLabel, 222, 42);
  nextButton = actionButton(filesPage, LV_SYMBOL_RIGHT, 264, 36, 40, 30, Kind::neutral, []() { ++pageIndex; redraw(); });
  for (int i = 0; i < kRows; ++i) {
    const int y = 70 + i * (kRowH + kRowGap);
    rowButton[i] = actionButton(filesPage, "", 0, y, kInnerW, kRowH, Kind::neutral, [i]() {
      const size_t index = static_cast<size_t>(pageIndex) * kRows + i;
      if (!shown.valid || index >= shown.entries.size()) return;
      const grbl::FileEntry entry = shown.entries[index];
      if (entry.directory) go(folder + entry.name + "/");
      else openFile(entry);
    });
    rowIcon[i] = label(rowButton[i], LV_SYMBOL_FILE, Font::body, rgb::muted);
    lv_obj_align(rowIcon[i], LV_ALIGN_LEFT_MID, 8, 0);
    rowName[i] = label(rowButton[i], "", Font::body);
    lv_obj_set_width(rowName[i], 196);
    lv_label_set_long_mode(rowName[i], LV_LABEL_LONG_DOT);
    lv_obj_align(rowName[i], LV_ALIGN_LEFT_MID, 34, 0);
    rowSize[i] = label(rowButton[i], "", Font::tiny, rgb::muted);
    lv_obj_set_width(rowSize[i], 64);
    lv_obj_set_style_text_align(rowSize[i], LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(rowSize[i], LV_ALIGN_RIGHT_MID, -8, 0);
    lv_obj_add_flag(rowButton[i], LV_OBJ_FLAG_HIDDEN);
  }
  statusLabel = label(filesPage, "", Font::body, rgb::muted);
  lv_obj_set_width(statusLabel, kInnerW - 24);
  lv_obj_set_style_text_align(statusLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(statusLabel, LV_ALIGN_TOP_MID, 0, 140);

  macrosPage = rowBox(parent, kPageH);
  lv_obj_set_pos(macrosPage, 0, kPageTop);
  lv_obj_set_width(macrosPage, kInnerW);
  Settings& s = pendant::current();
  for (int i = 0; i < static_cast<int>(kMacros); ++i) {
    const Macro& m = s.macros[i];
    macroButton[i] = actionButton(macrosPage, "", (i % 2) * 156, (i / 2) * 56, 148, 50, m.used() ? Kind::neutral : Kind::ghost,
                                  [i]() { tapMacro(i); });
    macroCaption[i] = label(macroButton[i], m.used() ? m.name.c_str() : LV_SYMBOL_PLUS " Add", Font::small, m.used() ? rgb::text : rgb::muted);
    lv_obj_set_width(macroCaption[i], 132);
    lv_label_set_long_mode(macroCaption[i], LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(macroCaption[i], LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(macroCaption[i]);
    if (m.used()) needIdle.push_back(macroButton[i]);
  }
  setPage(macroPage);
  seenGeneration = esp3dGcodeParser.snapshot().filesGeneration;
  wantList = true;
  shown = grbl::FileList{};
}
void update(const grbl::Status& s, uint64_t now) {
  lastStatus = s;
  lastNow = now;
  for (lv_obj_t* b : needIdle) setEnabled(b, editing || s.idle(now));
  if (macroPage) return;
  if (s.filesGeneration != seenGeneration) {
    seenGeneration = s.filesGeneration;
    grbl::FileList received = esp3dGcodeParser.files();
    if (received.path == folder) { shown = received; pageIndex = 0; }
    else wantList = true;  // stale reply for a folder we already left
  }
  if (wantList && s.idle(now) && esp3dGcodeParser.listFiles(folder)) wantList = false;
  renderList(s, now);
}
void release() {
  needIdle.clear();
  filesPage = macrosPage = editButton = upButton = pathLabel = pageLabel = prevButton = nextButton = statusLabel = nullptr;
  for (int i = 0; i < kRows; ++i) rowButton[i] = rowIcon[i] = rowName[i] = rowSize[i] = nullptr;
  for (unsigned i = 0; i < kMacros; ++i) macroButton[i] = macroCaption[i] = nullptr;
  storageChoice = modeChoice = Choice{};
}
}
const Tab jobTab = {"Job", LV_SYMBOL_FOLDER, build, update, release};
}
}
