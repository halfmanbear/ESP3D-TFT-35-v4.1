#include "pendant_settings.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace pendant {
namespace {
Settings active;
bool loaded = false;

bool starts(const std::string& s, const char* p) { return s.compare(0, strlen(p), p) == 0; }
std::string escape(const std::string& s) {
  std::string out;
  for (char c : s) {
    if (c == '%') out += "%25";
    else if (c == '\n') out += "%0A";
    else if (c == '\r') out += "%0D";
    else out += c;
  }
  return out;
}
std::string unescape(const std::string& s) {
  std::string out;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%' && i + 2 < s.size() && isxdigit(static_cast<unsigned char>(s[i + 1])) &&
        isxdigit(static_cast<unsigned char>(s[i + 2]))) {
      out += static_cast<char>(strtol(s.substr(i + 1, 2).c_str(), nullptr, 16));
      i += 2;
    } else {
      out += s[i];
    }
  }
  return out;
}
bool printable(const std::string& s) {
  for (unsigned char c : s) if (c < 32 || c > 126) return false;
  return true;
}
bool flag(const std::string& v) { return v == "1"; }
int toInt(const std::string& v, int fallback) {
  char* end;
  long n = strtol(v.c_str(), &end, 10);
  return v.empty() || *end ? fallback : static_cast<int>(n);
}
double toDouble(const std::string& v, double fallback) {
  char* end;
  double n = strtod(v.c_str(), &end);
  return v.empty() || *end || !std::isfinite(n) ? fallback : n;
}
}

bool validMacroPath(const std::string& path) {
  if (path.size() > 120 || !printable(path)) return false;
  if (path.find_first_of("?!~") != std::string::npos) return false;
  return starts(path, "/sd/") || starts(path, "/localfs/");
}
void sanitize(Settings& s) {
  s.brightness = std::min(100, std::max(10, s.brightness));
  static const int sleeps[] = {0, 1, 2, 5, 10, 30};
  if (std::find(std::begin(sleeps), std::end(sleeps), s.sleepMinutes) == std::end(sleeps)) s.sleepMinutes = 5;
  const Settings defaults;
  for (unsigned i = 0; i < kPresets; ++i) {
    if (!std::isfinite(s.steps[i]) || s.steps[i] < 0.001 || s.steps[i] > 500) s.steps[i] = defaults.steps[i];
    if (s.feeds[i] < 1 || s.feeds[i] > 30000) s.feeds[i] = defaults.feeds[i];
  }
  s.step = std::min<int>(kPresets - 1, std::max(0, s.step));
  s.feed = std::min<int>(kPresets - 1, std::max(0, s.feed));
  for (Macro& m : s.macros) {
    if (m.name.size() > 24 || !printable(m.name)) m.name.clear();
    if (!validMacroPath(m.path)) m = Macro{};
    else if (m.name.empty()) m.name = m.path.substr(m.path.find_last_of('/') + 1).substr(0, 24);
  }
}
std::string serialize(const Settings& s) {
  std::string out;
  char line[64];
  auto put = [&](const char* key, const std::string& value) { out += key; out += '='; out += value; out += '\n'; };
  put("brightness", std::to_string(s.brightness));
  put("sleep", std::to_string(s.sleepMinutes));
  put("terminal", s.terminal ? "1" : "0");
  put("inches", s.reportInches ? "1" : "0");
  put("touchx", s.touchFlipX ? "1" : "0");
  put("touchy", s.touchFlipY ? "1" : "0");
  put("dispx", s.displayFlipX ? "1" : "0");
  put("dispy", s.displayFlipY ? "1" : "0");
  put("step", std::to_string(s.step));
  put("feed", std::to_string(s.feed));
  put("live", s.liveJog ? "1" : "0");
  for (unsigned i = 0; i < kPresets; ++i) {
    snprintf(line, sizeof(line), "steps.%u", i);
    char value[32];
    snprintf(value, sizeof(value), "%.3f", s.steps[i]);
    put(line, value);
    snprintf(line, sizeof(line), "feeds.%u", i);
    put(line, std::to_string(s.feeds[i]));
  }
  for (unsigned i = 0; i < kMacros; ++i) {
    if (!s.macros[i].used()) continue;
    snprintf(line, sizeof(line), "macro.%u.name", i);
    put(line, escape(s.macros[i].name));
    snprintf(line, sizeof(line), "macro.%u.path", i);
    put(line, escape(s.macros[i].path));
  }
  return out;
}
Settings parse(const std::string& text) {
  Settings s;
  const Settings defaults;
  size_t pos = 0;
  while (pos < text.size()) {
    size_t end = text.find('\n', pos);
    if (end == std::string::npos) end = text.size();
    std::string line = text.substr(pos, end - pos);
    pos = end + 1;
    size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string key = line.substr(0, eq), value = line.substr(eq + 1);
    unsigned index = 0, consumed = 0;
    if (key == "brightness") s.brightness = toInt(value, defaults.brightness);
    else if (key == "sleep") s.sleepMinutes = toInt(value, defaults.sleepMinutes);
    else if (key == "terminal") s.terminal = flag(value);
    else if (key == "inches") s.reportInches = flag(value);
    else if (key == "touchx") s.touchFlipX = flag(value);
    else if (key == "touchy") s.touchFlipY = flag(value);
    else if (key == "dispx") s.displayFlipX = flag(value);
    else if (key == "dispy") s.displayFlipY = flag(value);
    else if (key == "step") s.step = toInt(value, defaults.step);
    else if (key == "feed") s.feed = toInt(value, defaults.feed);
    else if (key == "live") s.liveJog = flag(value);
    else if (sscanf(key.c_str(), "steps.%u%n", &index, &consumed) == 1 && consumed == key.size() && index < kPresets)
      s.steps[index] = toDouble(value, defaults.steps[index]);
    else if (sscanf(key.c_str(), "feeds.%u%n", &index, &consumed) == 1 && consumed == key.size() && index < kPresets)
      s.feeds[index] = static_cast<unsigned>(std::max(0, toInt(value, static_cast<int>(defaults.feeds[index]))));
    else if (sscanf(key.c_str(), "macro.%u.name%n", &index, &consumed) == 1 && consumed == key.size() && index < kMacros)
      s.macros[index].name = unescape(value);
    else if (sscanf(key.c_str(), "macro.%u.path%n", &index, &consumed) == 1 && consumed == key.size() && index < kMacros)
      s.macros[index].path = unescape(value);
  }
  sanitize(s);
  return s;
}
Settings& current() {
  if (!loaded) load();
  return active;
}
void load() {
  std::string text;
  active = storageRead(text) ? parse(text) : Settings{};
  loaded = true;
}
bool save() {
  sanitize(active);
  loaded = true;
  return storageWrite(serialize(active));
}
}
