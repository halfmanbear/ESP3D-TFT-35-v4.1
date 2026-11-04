#include <cstdint>
#include "esp3d_gcode_parser_service.h"
#include "esp3d_hal.h"
#include "serial/esp3d_serial_client.h"
#include <cassert>
#include <iostream>
int main() {
  auto& p = esp3dGcodeParser;
  serialClient.available = false;
  assert(!p.send("$G") && !p.snapshot().uncertain); // screen boots before UART
  serialClient.available = true;
  assert(!p.processCommand("<Idle|MPos:1,2,"));
  assert(!p.snapshot().seenStatus);
  assert(p.processCommand("3|WCO:0,1,2>\r"));
  assert(p.snapshot().work[2] == 1);
  p.processCommand("\n");
  {
    // The first status report after connecting makes poll() send Ctrl-L once, as a bare byte.
    const size_t before = serialClient.writes.size();
    p.poll(false);
    assert(serialClient.writes.size() == before + 1 && serialClient.writes.back() == "\x0c");
    p.poll(false);
    assert(serialClient.writes.size() == before + 1);
  }
  assert(p.send("?")); assert(serialClient.writes.back() == "?");
  assert(p.send("$H")); assert(serialClient.writes.back() == "$H\n");
  assert(p.send("!")); assert(serialClient.writes.back() == "!");
  assert(p.send("\x85")); assert(serialClient.writes.back() == "\x85");
  p.processCommand("ok\r\n<Idle|MPos:1,2,3>\n");
  assert(!p.snapshot().pending && p.snapshot().idle(testClock));
  std::string huge = "<Idle|Unknown:" + std::string(1100,'x');
  p.processCommand(huge.c_str()); p.processCommand(">\n");
  assert(p.snapshot().machine[0] == 1);
  assert(p.processCommand("<Idle|MPos:9,8,7>\n"));
  assert(p.snapshot().machine[0] == 9);
  assert(p.getType("error:9\n") == ESP3DDataType::error);
  assert(p.getType("ALARM:2\r\n") == ESP3DDataType::error);
  assert(p.getType("<Idle|MPos:1,2,3>\n") == ESP3DDataType::response);
  assert(!p.hasAck("?") && !p.hasAck("\x91") && p.hasAck("$G"));
  testClock = 3000; p.poll(true);
  assert(serialClient.writes[serialClient.writes.size()-2] == "?");
  assert(serialClient.writes.back() == "$G\n");

  // Controller file list: a long JSON line arriving in 512 byte UART chunks, as on the device.
  testClock = 3100;
  p.processCommand("[GC:G0 G54 G17 G21 G90 M5]\nok\n<Idle|MPos:1,2,3>\n");
  assert(p.snapshot().wcs == "G54" && !p.snapshot().pending);
  assert(p.listFiles("/sd/") && serialClient.writes.back() == "$Files/ListGcode=/sd/\n");
  assert(p.snapshot().listing);
  std::string json = "[JSON:{\"files\":[";
  for (int i = 0; i < 60; ++i) {
    if (i) json += ",";
    json += "{\"name\":\"part-number-" + std::to_string(i) + ".nc\",\"size\":\"" + std::to_string(1000 + i) + "\"}";
  }
  json += "],\"path\":\"/sd\"}]\nok\n";
  assert(json.size() > 2000);
  const unsigned generation = p.snapshot().filesGeneration;
  for (size_t at = 0; at < json.size(); at += 512) p.processCommand(json.substr(at, 512).c_str());
  assert(!p.snapshot().listing && p.snapshot().filesGeneration == generation + 1);
  auto list = p.files();
  assert(list.valid && list.entries.size() == 60 && list.path == "/sd/" && list.entries[59].name == "part-number-9.nc");
  p.processCommand("<Idle|MPos:1,2,3>\n");
  // The device appends '\n' to every UART chunk that lacks one (ESP3DClient::setDataContent), so a long
  // reply is cut into lines at 512 byte boundaries, including inside file names. It must still reassemble.
  testClock = 3150;
  p.processCommand("<Idle|MPos:1,2,3>\n");
  assert(p.listFiles("/sd/") && p.snapshot().listing);
  for (size_t at = 0; at < json.size(); at += 512) {
    std::string chunk = json.substr(at, 512);
    if (chunk.back() != '\n') chunk += "\n";
    p.processCommand(chunk.c_str());
  }
  assert(!p.snapshot().listing && p.files().valid && p.files().entries.size() == 60);
  p.processCommand("<Idle|MPos:1,2,3>\n");
  // A normal line may still not exceed 1023 characters once the listing is over.
  std::string longLine = "<Idle|Unknown:" + std::string(1500, 'x') + ">\n";
  p.processCommand(longLine.c_str());
  assert(p.snapshot().machine[0] == 1);

  // Terminal: sent verbatim, transcript kept in the session.
  testClock = 3200;
  p.processCommand("<Idle|MPos:1,2,3>\n");
  assert(p.sendTerminal("$I") && serialClient.writes.back() == "$I\n");
  assert(!p.sendTerminal("$G"));
  p.processCommand("[VER:3.9 FluidNC:]\nok\n");
  auto lines = p.terminalLog();
  assert(lines.size() >= 3 && lines[lines.size() - 3] == "> $I" && lines.back() == "ok");
  serialClient.available = false;
  p.processCommand("<Idle|MPos:1,2,3>\n");
  assert(!p.sendTerminal("$I") && p.snapshot().uncertain); // a partial write cannot be ruled out
  serialClient.available = true;

  // Held-control jog through the service: exact bytes, silent acks, release, UI-stall watchdog.
  serialClient.available = true;
  testClock = 20000;
  p.processCommand("Grbl 3.9 [FluidNC v3.9.5]\n<Idle|MPos:1,2,3>\n");  // restart clears the earlier latched failure
  serialClient.writes.clear();
  assert(p.jogHold({{'X', 1}}, 600) && serialClient.writes.size() == 1 && serialClient.writes[0] == "$J=G91 G21 X1.000 F600\n");
  assert(p.snapshot().jogHeld);
  p.processCommand("ok\n");
  testClock = 20030; p.poll(true);
  assert(serialClient.writes.back() == "?" || serialClient.writes.back() == "$J=G91 G21 X1.000 F600\n");  // no $G while jogging
  for (const auto& w : serialClient.writes) assert(w != "$G\n");
  p.jogRelease();
  assert(serialClient.writes.back() == "\x85" && !p.snapshot().jogHeld);
  size_t count = serialClient.writes.size();
  p.jogRelease();
  assert(serialClient.writes.size() == count);                        // nothing to cancel the second time
  // Watchdog: nobody calls jogHold any more.
  p.processCommand("ok\n<Idle|MPos:1,2,3>\n");
  testClock = 20100;
  assert(p.jogHold({{'Y', -1}}, 600) && serialClient.writes.back() == "$J=G91 G21 Y-1.000 F600\n");
  p.processCommand("ok\n");
  testClock = 20400; p.poll(false);                                  // within the heartbeat window
  assert(p.snapshot().jogHeld);
  testClock = 20600; p.poll(false);                                  // polling disabled: still guarded
  assert(serialClient.writes.back() == "\x85" && !p.snapshot().jogHeld);
  p.jogRelease();
  // A refused or failed write ends the hold; a failed cancel is flagged.
  p.processCommand("<Idle|MPos:1,2,3>\n");
  serialClient.available = false;
  assert(!p.jogHold({{'X', 1}}, 600) && !p.snapshot().jogHeld && p.snapshot().uncertain);
  serialClient.available = true;
  p.jogRelease();                                                      // finger lifted (nothing left to cancel)
  p.processCommand("Grbl 3.9 [FluidNC v3.9.5]\n<Idle|MPos:1,2,3>\n");
  assert(p.jogHold({{'X', 1}}, 600) && p.snapshot().jogHeld);
  serialClient.available = false;
  p.jogRelease();
  assert(p.snapshot().uncertain && p.snapshot().message.find("Hold") != std::string::npos);
  serialClient.available = true;
  p.processCommand("Grbl 3.9 [FluidNC v3.9.5]\n");
  std::cout << "GRBL receive framing, handshake and exact UART bytes passed\n";
}
