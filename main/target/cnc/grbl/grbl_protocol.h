// FluidNC/GRBL wire protocol, independent of ESP-IDF and LVGL.
// Protocol reference: FluidTouch (MIT, Jason Yeager); see docs/fluidtouch-port.md.
#pragma once
#include <array>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace grbl {
struct FileEntry {
  std::string name;
  int64_t size = 0;
  bool directory = false;
};
// Result of $Files/ListGcode. Directories sort first, then files, by name.
struct FileList {
  std::string path, error;
  std::vector<FileEntry> entries;
  bool valid = false;
};
struct Status {
  std::string state = "Disconnected", message, modal, wcs, pins, accessories, file, firmware;
  std::array<double, 6> machine{}, work{}, offset{};
  unsigned machineAxes = 0, workAxes = 0, offsetAxes = 0;
  double feed = 0, spindle = 0, progress = 0;
  // Feed, rapid and spindle override percentages.
  std::array<double, 3> overrides{{100, 100, 100}};
  bool hasFeed = false, hasOverrides = false, sdRunning = false;
  bool seenStatus = false, pending = false, uncertain = false, awaitingStatus = false;
  bool messageError = false, modalStale = false;
  // A reset was just sent; the controller has not restarted yet.
  bool resetting = false;
  // A file list reply is being collected.
  bool listing = false;
  // A held-control jog is being fed (see Session::jogHold).
  bool jogHeld = false;
  // Bumped when a new file list or terminal line is available; never reset.
  unsigned filesGeneration = 0, logGeneration = 0;
  uint64_t lastStatus = 0;
  bool fresh(uint64_t now) const { return seenStatus && now - lastStatus < 2000; }
  bool idle(uint64_t now) const { return fresh(now) && state == "Idle" && !pending && !uncertain && !awaitingStatus; }
  bool alarm(uint64_t now) const { return fresh(now) && state == "Alarm" && !pending && !uncertain && !awaitingStatus; }
  bool held(uint64_t now) const { return fresh(now) && !uncertain && (state == "Hold:0" || state == "Door:0"); }
};
class Session {
 public:
  Status status;
  FileList files;
  // Terminal transcript: sent lines start with "> ".
  std::deque<std::string> log;
  bool ingest(const char* line, uint64_t now);
  // Validates a pendant command or realtime byte; on success `wire` holds the exact bytes.
  bool prepare(const char* command, uint64_t now, std::string& wire);
  // Free-form line for the terminal; only when Idle or Alarm and nothing is outstanding.
  bool prepareTerminal(const char* line, uint64_t now, std::string& wire);
  void tick(uint64_t now);
  // True once after the first status report following boot, a lost link or a controller restart.
  // FluidNC's UART channel echoes received characters until it is sent Ctrl-L (0x0c).
  bool takeEchoOff() { const bool due = echoOff; echoOff = false; return due; }
  void echoOffFailed() { echoOff = true; }
  void sendFailed();
  // A jog cancel could not be written: the machine may still be moving.
  void cancelFailed();
  // Held-control (continuous) jog. Call every ~30 ms while a control is held; `weights` are signed
  // axis speed fractions (-1..1, XYZABC, each axis once) of `feed` mm/min. Short $J chunks are fed
  // so that at most ~200 ms of travel is ever queued. `wire` may get a jog cancel, a chunk, or both.
  // Returns false once the hold is over (refused, alarm, error, stale status, time limit); the
  // cancel is in `wire` and further calls keep returning false until jogRelease().
  bool jogHold(const std::vector<std::pair<char, double>>& weights, unsigned feed, uint64_t now, std::string& wire);
  // The control was released: `wire` gets the jog cancel if anything could still be moving.
  bool jogRelease(std::string& wire);
  // Cancels a hold whose feeder went quiet (UI stalled). Call regularly from another context.
  bool jogWatchdog(uint64_t now, std::string& wire);
  static constexpr uint64_t kJogChunkMs = 100, kJogLeadMs = 100, kJogBeatMs = 400, kJogMaxMs = 30000,
                            kJogReplyMs = 2000;
  // Longest line ingest() accepts; larger while a file list is collected.
  size_t maxLine() const { return status.listing ? kCaptureLine : kLine; }
  static bool realtime(const char* command);
  static constexpr size_t kLine = 1023, kCaptureLine = 16383;
 private:
  static constexpr size_t kCaptureMax = 12288;
  uint64_t sentAt = 0, timeout = 5000;
  bool pendingQuery = false, invalidateOffset = false, invalidateModal = false;
  bool terminalCommand = false, captureOverflow = false, echoOff = false;
  // Held jog: active = being fed; blocked = ended early, ignore the control until released;
  // cancel = a cancel still has to be written; pendingJog = the outstanding line is a jog chunk.
  bool jogActive = false, jogBlocked = false, jogCancel = false, pendingJog = false;
  uint64_t jogStart = 0, jogBeat = 0, jogQueued = 0;
  std::vector<std::pair<char, double>> jogDir;
  bool jogEnd(std::string& wire, const char* why);
  std::string capture;
  bool parseStatus(const std::string& line, uint64_t now);
  void restart(const char* message);
  void note(const std::string& text, bool error = false);
  void record(const std::string& text);
  void finishCapture();
  void commit(const std::string& cmd, uint64_t now, uint64_t limit, bool query, std::string& wire);
};
// Explicit metric incremental jog; never changes the controller's modal units.
// Axis words are any of XYZABC, e.g. "X-1.000 Y2.500".
std::string jog(char axis, double distance, unsigned feed);
std::string jog(const std::vector<std::pair<char, double>>& moves, unsigned feed);
// Parses the JSON reply of $Files/ListGcode (with or without the [JSON:...] wrapper).
bool parseFileList(const std::string& json, FileList& out);
// Short human text for a FluidNC error:N / ALARM:N code; empty if unknown.
const char* describeError(int code);
const char* describeAlarm(int code);
}
