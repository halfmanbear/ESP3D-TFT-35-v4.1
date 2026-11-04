#include "grbl_protocol.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace grbl {
namespace {
bool starts(const std::string& s, const char* p) { return s.compare(0, strlen(p), p) == 0; }
bool numbers(const std::string& s, double* out, unsigned min, unsigned max, unsigned& count) {
  count = 0;
  const char* p = s.c_str();
  while (*p && count < max) {
    char* end;
    double v = strtod(p, &end);
    if (end == p || !std::isfinite(v) || std::fabs(v) > 1e9) return false;
    out[count++] = v;
    if (!*end) return count >= min;
    if (*end != ',' || !end[1]) return false;
    p = end + 1;
  }
  return false;
}
bool knownState(const std::string& s) {
  return s == "Idle" || s == "Run" || s == "Jog" || s == "Alarm" ||
         s == "Home" || s == "Check" || s == "Sleep" || s == "ConfigAlarm" || s == "Critical" ||
         s == "Hold:0" || s == "Hold:1" || s == "Door:0" ||
         s == "Door:1" || s == "Door:2" || s == "Door:3";
}
// Whole-string number; false for trailing junk, NaN and infinity.
bool number(const std::string& s, double& out) {
  if (s.empty()) return false;
  char* end;
  out = strtod(s.c_str(), &end);
  return !*end && std::isfinite(out);
}
// Space separated axis words: "X1.5 Y-2" (jog) or "X0 Z0" (zero), optional final F<feed>.
bool axisWords(const std::string& s, bool zero, double maxDistance) {
  unsigned axes = 0, feeds = 0;
  size_t pos = 0;
  while (pos < s.size()) {
    size_t end = s.find(' ', pos);
    if (end == std::string::npos) end = s.size();
    std::string word = s.substr(pos, end - pos);
    pos = end + 1;
    double v;
    if (word.size() < 2 || feeds) return false;
    if (word[0] == 'F' && !zero) {
      if (!number(word.substr(1), v) || v < 1 || v > 30000) return false;
      ++feeds;
    } else if (strchr("XYZABC", word[0])) {
      if (!number(word.substr(1), v) || std::fabs(v) > maxDistance || (zero && v != 0)) return false;
      ++axes;
    } else {
      return false;
    }
  }
  return axes > 0 && (zero || feeds == 1);
}
bool wcsWord(const std::string& s) { return s.size() == 3 && s[0] == 'G' && s[1] == '5' && s[2] >= '4' && s[2] <= '9'; }
// "G0 G54 G17 G21 G90 M5" -> "G54"
std::string findWcs(const std::string& modal) {
  size_t pos = 0;
  while (pos < modal.size()) {
    size_t end = modal.find(' ', pos);
    if (end == std::string::npos) end = modal.size();
    std::string word = modal.substr(pos, end - pos);
    if (wcsWord(word)) return word;
    pos = end + 1;
  }
  return {};
}
std::string clip(std::string s, size_t max) {
  for (char& c : s) if (static_cast<unsigned char>(c) < 32) c = ' ';
  if (s.size() > max) s.resize(max);
  return s;
}
std::string describe(const std::string& line, const char* text) {
  return text && *text ? line + " " + text : line;
}

// Minimal JSON reader for the file list: objects, arrays, strings, scalars.
struct Json {
  const std::string& s;
  size_t i = 0;
  explicit Json(const std::string& text) : s(text) {}
  void space() { while (i < s.size() && isspace(static_cast<unsigned char>(s[i]))) ++i; }
  bool eat(char c) {
    space();
    if (i < s.size() && s[i] == c) { ++i; return true; }
    return false;
  }
  bool peek(char c) { space(); return i < s.size() && s[i] == c; }
  static void utf8(std::string& out, unsigned v) {
    if (v < 0x80) out += static_cast<char>(v);
    else if (v < 0x800) { out += static_cast<char>(0xC0 | (v >> 6)); out += static_cast<char>(0x80 | (v & 0x3F)); }
    else { out += static_cast<char>(0xE0 | (v >> 12)); out += static_cast<char>(0x80 | ((v >> 6) & 0x3F)); out += static_cast<char>(0x80 | (v & 0x3F)); }
  }
  bool string(std::string& out) {
    if (!eat('"')) return false;
    out.clear();
    while (i < s.size() && s[i] != '"') {
      char c = s[i++];
      if (c != '\\') { out += c; continue; }
      if (i >= s.size()) return false;
      char e = s[i++];
      if (e == 'u') {
        if (i + 4 > s.size()) return false;
        char* end;
        std::string hex = s.substr(i, 4);
        unsigned v = static_cast<unsigned>(strtoul(hex.c_str(), &end, 16));
        if (*end) return false;
        utf8(out, v);
        i += 4;
      } else if (e == 'n') out += '\n';
      else if (e == 't') out += '\t';
      else if (e == 'r' || e == 'b' || e == 'f') out += ' ';
      else out += e;
    }
    if (i >= s.size()) return false;
    ++i;
    return true;
  }
  // Number, true, false or null as raw text.
  bool scalar(std::string& out) {
    space();
    size_t start = i;
    while (i < s.size() && !strchr(",}] \t\r\n", s[i])) ++i;
    out = s.substr(start, i - start);
    return !out.empty();
  }
  bool skip(unsigned depth = 0) {
    if (depth > 8) return false;
    space();
    if (i >= s.size()) return false;
    std::string ignored;
    if (s[i] == '"') return string(ignored);
    if (s[i] == '{' || s[i] == '[') {
      char close = s[i] == '{' ? '}' : ']';
      ++i;
      if (eat(close)) return true;
      do {
        if (close == '}' && (!string(ignored) || !eat(':'))) return false;
        if (!skip(depth + 1)) return false;
      } while (eat(','));
      return eat(close);
    }
    return scalar(ignored);
  }
  // String or scalar value as text (sizes arrive as either "123" or 123).
  bool text(std::string& out) {
    space();
    return peek('"') ? string(out) : scalar(out);
  }
};
bool entry(Json& j, FileEntry& out) {
  if (!j.eat('{')) return false;
  bool named = false, sized = false;
  if (!j.eat('}')) {
    do {
      std::string key, value;
      if (!j.string(key) || !j.eat(':')) return false;
      if (key == "name") { if (!j.string(out.name)) return false; named = true; }
      else if (key == "size") {
        if (!j.text(value)) return false;
        char* end;
        out.size = strtoll(value.c_str(), &end, 10);
        if (*end || value.empty()) return false;
        sized = true;
      } else if (!j.skip()) return false;
    } while (j.eat(','));
    if (!j.eat('}')) return false;
  }
  out.directory = out.size < 0;
  return named && sized && !out.name.empty();
}
std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  return s;
}
}

const char* describeError(int code) {
  switch (code) {
    case 1: return "Expected command letter";
    case 2: return "Bad number format";
    case 3: return "Invalid $ statement";
    case 8: return "Not idle";
    case 9: return "Locked: unlock or home first";
    case 13: return "Check door";
    case 15: return "Travel exceeded";
    case 16: return "Invalid jog command";
    case 20: return "Unsupported command";
    case 21: return "Modal group violation";
    case 22: return "Undefined feed rate";
    case 33: return "Invalid target";
    case 60: return "SD failed to mount";
    case 61: return "SD failed to read";
    case 62: return "SD failed to open directory";
    case 63: return "SD directory not found";
    case 64: return "SD file empty";
    case 65: return "SD file not found";
    case 66: return "SD failed to open file";
    case 67: return "SD busy";
    default: return "";
  }
}
const char* describeAlarm(int code) {
  switch (code) {
    case 1: return "Hard limit";
    case 2: return "Soft limit";
    case 3: return "Aborted while moving";
    case 4: return "Probe failed: not triggered at start";
    case 5: return "Probe failed: no contact";
    case 6: return "Homing failed: reset";
    case 7: return "Homing failed: door";
    case 8: return "Homing failed: pull-off";
    case 9: return "Homing failed: switch not found";
    case 10: return "Spindle control";
    case 11: return "Control pin active at start";
    case 12: return "Ambiguous limit switch";
    case 13: return "Hard stop";
    case 14: return "Unhomed";
    case 15: return "Initialisation";
    default: return "";
  }
}

bool parseFileList(const std::string& json, FileList& out) {
  out.entries.clear();
  out.error.clear();
  out.valid = false;
  Json j(json);
  bool haveFiles = false;
  std::string reply;
  if (!j.eat('{')) { out.error = "Unreadable file list"; return false; }
  if (!j.eat('}')) {
    do {
      std::string key, value;
      if (!j.string(key) || !j.eat(':')) { out.error = "Unreadable file list"; return false; }
      if (key == "files") {
        if (!j.eat('[')) { out.error = "Unreadable file list"; return false; }
        haveFiles = true;
        if (!j.eat(']')) {
          do {
            FileEntry item;
            if (!entry(j, item)) { out.error = "Unreadable file list"; return false; }
            if (out.entries.size() < 400) out.entries.push_back(item);
          } while (j.eat(','));
          if (!j.eat(']')) { out.error = "Unreadable file list"; return false; }
        }
      } else if (key == "error" || key == "status") {
        if (!j.text(value)) { out.error = "Unreadable file list"; return false; }
        if (key == "error" || value != "ok") reply = value;
      } else if (!j.skip()) {
        out.error = "Unreadable file list";
        return false;
      }
    } while (j.eat(','));
    if (!j.eat('}')) { out.error = "Unreadable file list"; return false; }
  }
  if (!haveFiles) {
    out.error = reply.empty() ? "No file list in reply" : reply;
    return false;
  }
  std::stable_sort(out.entries.begin(), out.entries.end(), [](const FileEntry& a, const FileEntry& b) {
    if (a.directory != b.directory) return a.directory;
    return lower(a.name) < lower(b.name);
  });
  out.valid = true;
  return true;
}

bool Session::realtime(const char* c) {
  if (!c || !c[0] || c[1]) return false;
  const unsigned char b = c[0];
  return b == '?' || b == '!' || b == '~' || b == 0x18 || b == 0x84 ||
         b == 0x85 || (b >= 0x90 && b <= 0x97) || (b >= 0x99 && b <= 0x9d);
}
void Session::note(const std::string& text, bool error) {
  status.message = text;
  status.messageError = error;
}
void Session::record(const std::string& text) {
  log.push_back(clip(text, 160));
  if (log.size() > 80) log.pop_front();
  ++status.logGeneration;
}
// Controller restart or local reset: forget everything about the machine.
void Session::restart(const char* message) {
  Status fresh;
  fresh.filesGeneration = status.filesGeneration;
  fresh.logGeneration = status.logGeneration;
  fresh.firmware = status.firmware;
  status = fresh;
  note(message);
  capture.clear();
  captureOverflow = terminalCommand = invalidateOffset = invalidateModal = false;
  if (jogActive || jogCancel) jogBlocked = true;  // the controller dropped any queued jog itself
  jogActive = jogCancel = pendingJog = false;
}
bool Session::parseStatus(const std::string& line, uint64_t now) {
  if (line.size() < 3 || line.back() != '>') return false;
  Status next = status; // Reject the whole report if a known numeric field is malformed.
  size_t pos = line.find('|');
  next.state = line.substr(1, (pos == std::string::npos ? line.size()-1 : pos)-1);
  if (!knownState(next.state)) return false;
  bool m = false, w = false;
  next.pins.clear(); next.accessories.clear(); next.file.clear(); next.sdRunning = false;
  while (pos != std::string::npos && pos < line.size()-1) {
    size_t end = line.find('|', pos+1);
    if (end == std::string::npos) end = line.size()-1;
    std::string f = line.substr(pos+1, end-pos-1);
    unsigned n = 0;
    if (starts(f, "MPos:")) {
      if (!numbers(f.substr(5), next.machine.data(), 2, 6, next.machineAxes)) return false;
      m = true;
    } else if (starts(f, "WPos:")) {
      if (!numbers(f.substr(5), next.work.data(), 2, 6, next.workAxes)) return false;
      w = true;
    } else if (starts(f, "WCO:")) {
      if (!numbers(f.substr(4), next.offset.data(), 2, 6, next.offsetAxes)) return false;
    } else if (starts(f, "FS:") || starts(f, "F:")) {
      double v[2] = {};
      bool fs = starts(f, "FS:");
      if (!numbers(f.substr(fs ? 3 : 2), v, fs ? 2 : 1, fs ? 2 : 1, n)) return false;
      next.feed = v[0]; next.spindle = v[1]; next.hasFeed = true;
    } else if (starts(f, "Ov:")) {
      if (!numbers(f.substr(3), next.overrides.data(), 3, 3, n)) return false;
      next.hasOverrides = true;
    } else if (starts(f, "Pn:")) next.pins = f.substr(3);
    else if (starts(f, "A:")) next.accessories = f.substr(2);
    else if (starts(f, "SD:")) {
      size_t comma = f.find(',');
      if (comma == std::string::npos || !numbers(f.substr(3, comma-3), &next.progress, 1, 1, n) ||
          next.progress < 0 || next.progress > 100) return false;
      next.file = f.substr(comma+1); next.sdRunning = true;
    }
    pos = end == line.size()-1 ? std::string::npos : end;
  }
  // WCO is intermittent. Never assume a zero offset before it has arrived.
  if (m && !w) {
    next.workAxes = next.offsetAxes == next.machineAxes ? next.machineAxes : 0;
    for (unsigned i=0; i<next.workAxes; ++i) next.work[i] = next.machine[i] - next.offset[i];
  } else if (w && !m) {
    next.machineAxes = next.offsetAxes == next.workAxes ? next.workAxes : 0;
    for (unsigned i=0; i<next.machineAxes; ++i) next.machine[i] = next.work[i] + next.offset[i];
  }
  if (!next.pending) next.awaitingStatus = false;
  if (!status.seenStatus) echoOff = true;
  next.seenStatus = true; next.lastStatus = now;
  status = next;
  return true;
}
void Session::finishCapture() {
  FileList next;
  std::string json = capture;
  while (!json.empty() && isspace(static_cast<unsigned char>(json.back()))) json.pop_back();
  if (starts(json, "[JSON:")) {
    json.erase(0, 6);
    if (!json.empty() && json.back() == ']') json.pop_back();
  }
  if (captureOverflow) next.error = "Too many files to list";
  else parseFileList(json, next);
  next.path = files.path;
  files = next;
  ++status.filesGeneration;
  status.listing = status.pending = false;
  capture.clear();
  captureOverflow = false;
}
bool Session::ingest(const char* raw, uint64_t now) {
  if (!raw || strlen(raw) > maxLine()) return false;
  std::string line(raw);
  while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
  if (line.empty()) return false;
  if (line[0] == '<') return parseStatus(line, now);
  if (starts(line, "Grbl ") || line == "[MSG:RST]") {
    const bool banner = line[0] == 'G';
    restart("Controller restarted");
    if (banner) status.firmware = line;
    record(line);
    return true;
  }
  if (status.listing) {
    if (line == "ok") { finishCapture(); return true; }
    const bool other = starts(line, "error:") || starts(line, "ALARM:") ||
                       (line[0] == '[' && !starts(line, "[JSON:"));
    if (!other) {
      // Reply text, possibly split over several lines: [JSON:{...}] or {...}
      if (capture.size() + line.size() > kCaptureMax) captureOverflow = true;
      else capture += line;
      return true;
    }
  }
  if (line == "ok") {
    if (status.pending) {
      status.pending = false;
      if (!pendingQuery && !pendingJog) note("Command accepted");
      pendingJog = false;
      if (invalidateOffset) status.offsetAxes = status.workAxes = 0;
      if (invalidateModal) status.modalStale = true;
      if (terminalCommand) { record("ok"); terminalCommand = false; }
    }
    return true;
  }
  if (starts(line, "error:") || starts(line, "ALARM:")) {
    const bool alarm = line[0] == 'A';
    const int code = atoi(line.c_str() + 6);
    status.pending = pendingJog = false;
    if (jogActive) { jogActive = false; jogCancel = jogBlocked = true; status.jogHeld = false; }
    if (status.listing) {
      files.valid = false; files.entries.clear();
      files.error = describe(line, alarm ? describeAlarm(code) : describeError(code));
      ++status.filesGeneration;
      status.listing = false;
      capture.clear();
    }
    note(describe(line, alarm ? describeAlarm(code) : describeError(code)), true);
    record(line);
    terminalCommand = false;
    if (alarm) { status.state = "Alarm"; status.machineAxes = status.workAxes = status.offsetAxes = 0; }
    return true;
  }
  if (starts(line, "[GC:") && line.back() == ']') {
    status.modal = line.substr(4, line.size()-5);
    status.wcs = findWcs(status.modal);
    status.modalStale = false;
    if (terminalCommand) record(line);
    return true;
  }
  if (line[0] == '[' && line.back() == ']') {
    std::string text = line.substr(1, line.size()-2);
    if (starts(text, "MSG:")) text.erase(0, 4);
    if (starts(line, "[MSG:") || terminalCommand) record(line);
    note(text);
    return true;
  }
  if (terminalCommand) { record(line); return true; }
  return false;
}
void Session::tick(uint64_t now) {
  if (status.resetting && now - sentAt >= 5000) status.resetting = false;  // no banner: stay latched
  if (status.pending && now - sentAt >= timeout) {
    status.pending = status.listing = false;
    status.uncertain = true;
    terminalCommand = false;
    capture.clear();
    note("Reply timeout - reset controller to resync", true);
  }
  if (status.seenStatus && !status.fresh(now)) {
    status.seenStatus = false;
    status.machineAxes = status.workAxes = status.offsetAxes = 0;
    status.modal.clear(); status.wcs.clear(); status.hasFeed = status.hasOverrides = false;
    status.state = "Disconnected";
    // Never replay a command after reconnecting.
    if (status.pending) { status.uncertain = true; status.pending = status.listing = false; }
    terminalCommand = false;
    capture.clear();
  }
}
void Session::sendFailed() {
  if (status.pending && !pendingQuery) status.uncertain = true;
  status.pending = status.listing = pendingJog = false;
  if (jogActive) { jogActive = false; jogBlocked = true; status.jogHeld = false; }
  terminalCommand = false;
  capture.clear();
  note(status.uncertain ? "Send failed - reset controller to resync" : "Controller connection unavailable", true);
}
void Session::cancelFailed() {
  status.uncertain = true;
  note("Jog stop not sent - press Hold or reset the controller", true);
}
void Session::commit(const std::string& cmd, uint64_t now, uint64_t limit, bool query, std::string& wire) {
  if (invalidateOffset) status.offsetAxes = status.workAxes = 0;
  if (!query) status.awaitingStatus = true;
  wire = cmd + "\n";
  status.pending = true;
  pendingJog = false;
  sentAt = now;
  pendingQuery = query;
  timeout = limit;
  if (!query) note("Waiting for reply");
}
bool Session::prepare(const char* raw, uint64_t now, std::string& wire) {
  wire.clear(); tick(now);
  if (!raw || !*raw) return false;
  if (realtime(raw)) {
    unsigned char b = raw[0];
    if (b == '~' && !status.held(now)) return false;
    if (b >= 0x90 && (!status.fresh(now) || status.uncertain)) return false;
    wire = raw;
    if (jogActive && (b == 0x85 || b == '!' || b == 0x18)) {
      jogActive = false; jogBlocked = true; status.jogHeld = false;  // the cancel is being sent now
    }
    if (b == 0x18) {
      restart("Reset sent - waiting for controller restart");
      status.uncertain = status.resetting = true;
      sentAt = now;
    }
    return true;
  }
  std::string cmd(raw);
  if (!cmd.empty() && cmd.back() == '\n') cmd.pop_back();
  if (!cmd.empty() && cmd.back() == '\r') cmd.pop_back();
  if (cmd.empty() || cmd.size() > 240 || status.pending || status.uncertain || jogActive) return false;
  // Exactly one printable line, including file paths. Reject injected realtime bytes.
  for (unsigned char c : cmd) if (c < 32 || c > 126 || c == '?' || c == '!' || c == '~') return false;
  const bool query = cmd == "$G", home = cmd == "$H", unlock = cmd == "$X";
  const bool list = starts(cmd, "$Files/ListGcode=");
  const bool jogging = starts(cmd, "$J=G91 G21 "), zero = starts(cmd, "G10 L20 P0 ");
  const bool wcs = wcsWord(cmd);
  if (jogging && !axisWords(cmd.substr(11), false, 500)) return false;
  if (zero && !axisWords(cmd.substr(11), true, 0)) return false;
  if (!(query || home || unlock || list || jogging || zero || wcs ||
        starts(cmd, "$SD/Run=") || starts(cmd, "$LocalFS/Run=") || cmd == "M5")) return false;
  if (!query && !status.idle(now) && !((home || unlock) && status.alarm(now))) return false;
  invalidateOffset = zero || home;
  invalidateModal = wcs;
  terminalCommand = false;
  if (list) {
    files = FileList{};
    files.path = cmd.substr(17);
    capture.clear();
    captureOverflow = false;
    status.listing = true;
  }
  commit(cmd, now, home ? 120000 : (list ? 20000 : 5000), query || list, wire);
  return true;
}
bool Session::prepareTerminal(const char* raw, uint64_t now, std::string& wire) {
  wire.clear(); tick(now);
  if (!raw) return false;
  std::string cmd(raw);
  while (!cmd.empty() && (cmd.back() == '\n' || cmd.back() == '\r' || cmd.back() == ' ')) cmd.pop_back();
  if (cmd.empty() || cmd.size() > 120) return false;
  for (unsigned char c : cmd) if (c < 32 || c > 126 || c == '?' || c == '!' || c == '~') return false;
  if (!status.idle(now) && !status.alarm(now)) return false;
  if (jogActive) return false;
  invalidateOffset = invalidateModal = true;
  terminalCommand = true;
  record("> " + cmd);
  commit(cmd, now, starts(cmd, "$H") ? 120000 : 10000, false, wire);
  return true;
}
// Ends the hold. The cancel goes out only if a jog could still be queued or moving.
bool Session::jogEnd(std::string& wire, const char* why) {
  if (jogActive || jogCancel) wire += "\x85";
  jogActive = jogCancel = false;
  jogBlocked = true;
  status.jogHeld = false;
  if (why) note(why, true);
  return false;
}
bool Session::jogHold(const std::vector<std::pair<char, double>>& input, unsigned feed, uint64_t now, std::string& wire) {
  wire.clear(); tick(now);
  if (jogCancel) return jogEnd(wire, nullptr);  // an error or alarm ended the hold: write the cancel
  if (jogBlocked) return false;
  jogBeat = now;
  std::vector<std::pair<char, double>> dir;
  double sum = 0;
  for (const auto& w : input) {
    if (!w.first || !strchr("XYZABC", w.first) || !std::isfinite(w.second) || std::fabs(w.second) > 1.0001 ||
        std::any_of(input.begin(), input.end(), [&](const std::pair<char, double>& o) { return &o != &w && o.first == w.first; }))
      return jogEnd(wire, "Invalid jog request");
    if (std::fabs(w.second) < 0.001) continue;
    dir.push_back(w);
    sum += w.second * w.second;
  }
  if (feed == 0 || feed > 30000) return jogEnd(wire, "Invalid jog request");
  if (dir.empty()) {  // stick back in the dead zone: stop, but keep listening
    if (jogActive) { wire = "\x85"; jogActive = false; status.jogHeld = false; }
    return true;
  }
  double mag = std::sqrt(sum);
  if (mag > 1) { for (auto& w : dir) w.second /= mag; mag = 1; }
  const bool was = jogActive;
  const bool state = status.state == "Idle" || status.state == "Jog";
  if (was ? !(status.fresh(now) && !status.uncertain && state)
          : !status.idle(now))
    return jogEnd(wire, was ? "Jog stopped - machine status changed" : "Jog needs an idle machine");
  if (was && now - jogStart > kJogMaxMs) return jogEnd(wire, "Jog time limit - release and press again");
  if (was) {
    double dot = 0;
    for (const auto& n : dir)
      for (const auto& o : jogDir) if (o.first == n.first) dot += o.second * n.second;
    if (dot < 0) { wire = "\x85"; jogQueued = now; }  // reversal: don't let the old direction finish first
  } else {
    jogActive = true; jogStart = now; jogQueued = now;
    status.jogHeld = true;
  }
  jogDir = dir;
  if (status.pending || jogQueued > now + kJogLeadMs) return true;
  std::vector<std::pair<char, double>> moves;
  for (const auto& w : dir) {
    const double d = w.second * feed / 60.0 * (kJogChunkMs / 1000.0);
    if (std::fabs(d) >= 0.001) moves.emplace_back(w.first, d);
  }
  const unsigned rate = std::max(1u, static_cast<unsigned>(std::lround(feed * mag)));
  const std::string cmd = jog(moves, rate);
  if (cmd.empty()) return true;  // too slow to move a whole 0.001 step
  wire += cmd + "\n";
  status.pending = true;
  pendingJog = true; pendingQuery = false;
  sentAt = now;
  timeout = kJogReplyMs;
  jogQueued = std::max(jogQueued, now) + kJogChunkMs;
  return true;
}
bool Session::jogRelease(std::string& wire) {
  wire.clear();
  if (jogActive || jogCancel) wire = "\x85";
  jogActive = jogCancel = jogBlocked = false;
  status.jogHeld = false;
  return !wire.empty();
}
bool Session::jogWatchdog(uint64_t now, std::string& wire) {
  wire.clear();
  if (!jogActive || now - jogBeat < kJogBeatMs) return false;
  jogEnd(wire, "Jog stopped - touch updates stopped");
  return true;
}
std::string jog(const std::vector<std::pair<char, double>>& moves, unsigned feed) {
  if (moves.empty() || moves.size() > 6 || feed == 0 || feed > 30000) return {};
  std::string out = "$J=G91 G21";
  char word[40];
  for (const auto& move : moves) {
    if (!move.first || !strchr("XYZABC", move.first) || !std::isfinite(move.second) ||
        move.second == 0 || std::fabs(move.second) > 500) return {};
    snprintf(word, sizeof(word), " %c%.3f", move.first, move.second);
    out += word;
  }
  snprintf(word, sizeof(word), " F%u", feed);
  return out + word;
}
std::string jog(char axis, double distance, unsigned feed) { return jog({{axis, distance}}, feed); }
}
