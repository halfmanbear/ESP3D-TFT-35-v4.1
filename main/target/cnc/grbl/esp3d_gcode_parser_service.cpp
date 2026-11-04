/*
  esp3d_gcode_parser_service
  Copyright (c) 2022 Luc Lebosse. All rights reserved.

  This code is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This code is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "esp3d_gcode_parser_service.h"
#include "esp3d_commands.h"
#include "esp3d_hal.h"
#include "serial/esp3d_serial_client.h"
#include <cstring>
#include <mutex>

ESP3DGCodeParserService esp3dGcodeParser;
namespace {
std::mutex sessionMutex;
grbl::Session session;
std::string lineBuffer;
bool overflow = false;
const char* pollingCommands[] = {"?"};
uint64_t lastPoll = 0, lastModalPoll = 0;
}
ESP3DGCodeParserService::ESP3DGCodeParserService() : _lineResend(0) {}
ESP3DGCodeParserService::~ESP3DGCodeParserService() = default;
grbl::Status ESP3DGCodeParserService::snapshot() {
  std::lock_guard<std::mutex> lock(sessionMutex);
  session.tick(esp3d_hal::millis());
  return session.status;
}
grbl::FileList ESP3DGCodeParserService::files() {
  std::lock_guard<std::mutex> lock(sessionMutex);
  return session.files;
}
std::vector<std::string> ESP3DGCodeParserService::terminalLog() {
  std::lock_guard<std::mutex> lock(sessionMutex);
  return {session.log.begin(), session.log.end()};
}
namespace {
// Exact UART bytes: no printer stream queue, newline insertion, or checksum.
bool transmit(const std::string& wire) {
  return esp3dCommands.getOutputClient() == ESP3DClientType::serial &&
         serialClient.writeRaw(wire.c_str(), wire.size());
}
}
bool ESP3DGCodeParserService::send(const char* command) {
  std::lock_guard<std::mutex> lock(sessionMutex);
  std::string wire;
  if (!session.prepare(command, esp3d_hal::millis(), wire)) return false;
  const bool sent = transmit(wire);
  if (!sent) session.sendFailed();
  return sent;
}
bool ESP3DGCodeParserService::sendTerminal(const char* line) {
  std::lock_guard<std::mutex> lock(sessionMutex);
  std::string wire;
  if (!session.prepareTerminal(line, esp3d_hal::millis(), wire)) return false;
  const bool sent = transmit(wire);
  if (!sent) session.sendFailed();
  return sent;
}
bool ESP3DGCodeParserService::jogHold(const std::vector<std::pair<char, double>>& weights, unsigned feed) {
  std::lock_guard<std::mutex> lock(sessionMutex);
  std::string wire;
  const bool held = session.jogHold(weights, feed, esp3d_hal::millis(), wire);
  if (wire.empty() || transmit(wire)) return held;
  session.sendFailed();
  if (wire[0] == '\x85') session.cancelFailed();
  return false;
}
void ESP3DGCodeParserService::jogRelease() {
  std::lock_guard<std::mutex> lock(sessionMutex);
  std::string wire;
  if (session.jogRelease(wire) && !transmit(wire)) session.cancelFailed();
}
bool ESP3DGCodeParserService::listFiles(const std::string& folder) {
  return send(("$Files/ListGcode=" + folder).c_str());
}
void ESP3DGCodeParserService::poll(bool enabled) {
  uint64_t now = esp3d_hal::millis();
  {
    // Dead-man: if the UI stops feeding a held jog, cancel it even when polling is off.
    std::lock_guard<std::mutex> lock(sessionMutex);
    std::string wire;
    if (session.jogWatchdog(now, wire) && !transmit(wire)) session.cancelFailed();
    // Ctrl-L stops FluidNC echoing our bytes back; it is not a line, so it bypasses the session.
    if (session.takeEchoOff() && !transmit("\x0c")) session.echoOffFailed();
  }
  auto state = snapshot();
  if (!enabled) return;
  if (now - lastPoll >= 250) { lastPoll = now; send("?"); }
  // A complete line also acknowledges FluidNC's repeated [MSG:RST] handshake.
  // A changed work coordinate system asks for a prompt refresh.
  const bool stale = state.modal.empty() || state.modalStale;
  if (!state.pending && !state.uncertain && !state.jogHeld && now - lastModalPoll >= (stale ? 500 : 10000)) {
    lastModalPoll = now;
    send("$G");
  }
}
bool ESP3DGCodeParserService::processCommand(const char* data) {
  if (!data) return false;
  std::lock_guard<std::mutex> lock(sessionMutex);
  bool parsed = false;
  // UART messages can split long lines at the receive buffer boundary.
  for (const char* p = data; *p; ++p) {
    if (*p == '\r' || *p == '\n') {
      if (!overflow && !lineBuffer.empty())
        parsed = session.ingest(lineBuffer.c_str(), esp3d_hal::millis()) || parsed;
      lineBuffer.clear(); overflow = false;
    } else if (!overflow) {
      if (lineBuffer.size() >= session.maxLine()) { lineBuffer.clear(); overflow = true; }
      else lineBuffer += *p;
    }
  }
  return parsed;
}
ESP3DDataType ESP3DGCodeParserService::getType(const char* raw) {
  if (!raw) return ESP3DDataType::empty_line;
  std::string s(raw);
  while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ')) s.pop_back();
  if (s.empty()) return ESP3DDataType::empty_line;
  if (s == "ok") return ESP3DDataType::ack;
  if (s.compare(0, 6, "error:") == 0 || s.compare(0, 6, "ALARM:") == 0) return ESP3DDataType::error;
  if (s[0] == '<' && s.back() == '>') return ESP3DDataType::response;
  if (s[0] == '[' || s.compare(0, 5, "Grbl ") == 0) return ESP3DDataType::response;
  if (s[0] == ';') return ESP3DDataType::comment;
  if (grbl::Session::realtime(s.c_str())) return ESP3DDataType::emergency_command;
  if (s[0] == '$' || s[0] == 'G' || s[0] == 'M') return ESP3DDataType::gcode;
  return ESP3DDataType::unknown;
}
bool ESP3DGCodeParserService::hasAck(const char* command) { return !grbl::Session::realtime(command); }
bool ESP3DGCodeParserService::hasMultiLineReport(const char*) { return false; }
bool ESP3DGCodeParserService::forwardToScreen(const char*) { return false; }
const char** ESP3DGCodeParserService::getPollingCommands() { return pollingCommands; }
const char* ESP3DGCodeParserService::getFwCommandString(FW_GCodeCommand) { return ""; }
uint64_t ESP3DGCodeParserService::getPollingCommandsLastRun(uint8_t) { return 0; }
bool ESP3DGCodeParserService::setPollingCommandsLastRun(uint8_t, uint64_t) { return false; }
