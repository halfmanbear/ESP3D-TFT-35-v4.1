#pragma once
#include <lvgl.h>
#include "grbl_protocol.h"
#include <string>
#include <vector>
// Same interface as the firmware service, backed by the real protocol Session.
struct MockParser {
  grbl::Session session;
  std::vector<std::string> sent, terminalSent;
  grbl::Status snapshot() { session.tick(lv_tick_get()); return session.status; }
  bool send(const char* command) {
    std::string wire;
    if (!session.prepare(command, lv_tick_get(), wire)) return false;
    sent.push_back(wire); return true;
  }
  bool sendTerminal(const char* line) {
    std::string wire;
    if (!session.prepareTerminal(line, lv_tick_get(), wire)) return false;
    terminalSent.push_back(wire); return true;
  }
  bool jogHold(const std::vector<std::pair<char, double>>& weights, unsigned feed) {
    std::string wire;
    const bool held = session.jogHold(weights, feed, lv_tick_get(), wire);
    if (!wire.empty()) sent.push_back(wire);
    return held;
  }
  void jogRelease() {
    std::string wire;
    if (session.jogRelease(wire)) sent.push_back(wire);
  }
  bool listFiles(const std::string& folder) { return send(("$Files/ListGcode=" + folder).c_str()); }
  grbl::FileList files() { return session.files; }
  std::vector<std::string> terminalLog() { return {session.log.begin(), session.log.end()}; }
};
inline MockParser esp3dGcodeParser;
