#include "grbl_protocol.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>
using grbl::Session;
using grbl::jog;
using grbl::parseFileList;
int main() {
  Session s; std::string wire;
  assert(!s.prepare("$H", 0, wire));
  assert(s.prepare("?", 0, wire) && wire == "?");
  assert(s.ingest("<Idle|MPos:10,20,30|FS:100,0|Ov:90,100,110>", 100));
  assert(s.status.workAxes == 0); // offset is unknown, not zero
  assert(s.ingest("<Idle|WCO:1,2,3|MPos:10,20,30|Pn:XP>", 200));
  assert(s.status.work[0] == 9 && s.status.workAxes == 3);
  assert(s.ingest("<Run|MPos:11,22,33|SD:12.5,folder/a.nc|FS:50,100>", 300));
  assert(s.status.work[1] == 20 && s.status.sdRunning && s.status.file == "folder/a.nc");
  assert(s.status.pins.empty());
  assert(!s.prepare("$H", 300, wire));
  assert(s.prepare("!", 300, wire) && wire == "!");
  assert(!s.prepare("~", 300, wire));
  assert(s.ingest("<Hold:1|MPos:11,22,33>", 400));
  assert(!s.prepare("~", 400, wire));
  assert(s.ingest("<Hold:0|MPos:11,22,33>", 500));
  assert(s.prepare("~", 500, wire) && wire == "~");
  assert(!s.ingest("<Idle|MPos:NaN,1,2>", 600));
  assert(s.status.state == "Hold:0" && s.status.lastStatus == 500);
  assert(!s.ingest("<Idle|MPos:1,2,3", 600));
  assert(!s.ingest("<Idle|MPos:1,,3>", 600));
  assert(!s.ingest("<Idle|FS:1,inf>", 600));
  assert(!s.ingest("<Idle|Ov:1,2>", 600));
  assert(!s.ingest("<Idle|MPos:1,2,3,4,5,6,7>", 600));
  assert(!s.ingest("<Fake|MPos:1,2,3>", 600));
  assert(s.ingest("<ConfigAlarm|MPos:1,2,3>", 600) && s.status.state == "ConfigAlarm");  // bad config is reported, not hidden
  assert(s.ingest("<Critical|MPos:1,2,3>", 600) && !s.status.idle(600));
  assert(s.ingest("<Hold:0|MPos:11,22,33>", 600));
  assert(s.ingest("<Idle|WPos:1,2,3|WCO:10,20,30|Unknown:hello>", 700));
  assert(s.status.machine[2] == 33 && !s.status.sdRunning);
  assert(s.prepare(grbl::jog('X', -0.1, 100).c_str(), 700, wire));
  assert(wire == "$J=G91 G21 X-0.100 F100\n");
  assert(!s.prepare("$H", 700, wire));
  assert(s.prepare("\x85", 700, wire) && wire.size() == 1 && (unsigned char)wire[0] == 0x85);
  assert(s.ingest("<Idle|MPos:1,2,3>", 701)); // report before ack cannot unlock next move
  assert(s.ingest("ok\r\n", 702));
  assert(!s.prepare("$H", 703, wire));
  assert(s.ingest("<Idle|MPos:1,2,3>", 704));
  assert(!s.prepare("$SD/Run=/evil.nc\n$H", 704, wire));
  assert(!s.prepare("$SD/Run=/bad!file.nc", 704, wire));
  assert(!s.prepare("M3 S1000", 704, wire));
  assert(s.prepare("G10 L20 P0 X0", 704, wire));
  assert(s.status.workAxes == 0 && s.status.offsetAxes == 0);
  assert(s.ingest("error:9", 705) && !s.status.pending && s.status.messageError);
  assert(s.status.message == "error:9 Locked: unlock or home first");
  assert(s.ingest("<Idle|MPos:1,2,3>", 800));
  assert(s.prepare("$SD/Run=/job.nc", 800, wire) && wire == "$SD/Run=/job.nc\n");
  s.tick(6000);
  assert(s.status.uncertain && !s.status.pending && !s.status.seenStatus);
  assert(s.ingest("ok", 6001)); // late ack must not clear uncertainty
  assert(s.ingest("<Idle|MPos:1,2,3>", 6002));
  assert(!s.prepare("$H", 6002, wire));
  assert(s.prepare("\x18", 6002, wire) && wire.size() == 1);
  assert(s.status.uncertain && !s.status.seenStatus);
  assert(s.ingest("Grbl 3.9 [FluidNC]", 6003));
  assert(!s.status.uncertain && s.status.offsetAxes == 0);
  assert(s.prepare("$G", 6004, wire) && wire == "$G\n");
  assert(s.ingest("[GC:G0 G54 G17 G21 G90 M5]", 6005));
  assert(s.status.modal == "G0 G54 G17 G21 G90 M5");
  assert(s.ingest("ok", 6006));
  assert(s.ingest("<Alarm|MPos:1,2,3>", 6007));
  assert(s.prepare("$H", 6008, wire));
  for (unsigned t=6100; t<20000; t+=100) { assert(s.ingest("<Home|MPos:1,2,3>", t)); s.tick(t); }
  assert(s.status.pending && !s.status.uncertain); // homing may take >5 seconds
  assert(s.ingest("ALARM:9", 20000) && !s.status.pending && s.status.state == "Alarm");
  assert(s.ingest("[MSG:RST]", 20001));
  assert(s.status.offsetAxes == 0 && s.status.modal.empty());
  assert(s.ingest("<Idle|MPos:1,2,3,4,5,6|WCO:1,1,1,1,1,1>", 20002));
  assert(s.status.workAxes == 6 && s.status.work[5] == 5);
  s.tick(22002);
  assert(s.status.offsetAxes == 0 && !s.status.fresh(22002));
  assert(grbl::jog('Q', 1, 100).empty() && grbl::jog('X', 501, 100).empty() && !grbl::jog('A', 1, 100).empty());
  assert(Session::realtime("\x91") && !Session::realtime("?\n"));

  // Work coordinate system, units and zeroing validation.
  {
    Session t; std::string w;
    assert(t.ingest("<Idle|MPos:0,0,0|WCO:0,0,0>", 0));
    assert(t.prepare("$G", 0, w)); assert(t.ingest("[GC:G0 G55 G17 G21 G90 M5]", 1) && t.ingest("ok", 2));
    assert(t.status.wcs == "G55" && !t.status.modalStale);
    assert(t.ingest("<Idle|MPos:0,0,0>", 3));
    assert(!t.prepare("G60", 3, w) && !t.prepare("G53", 3, w) && !t.prepare("G54 G1", 3, w));
    assert(t.prepare("G56", 3, w) && w == "G56\n");
    assert(t.ingest("ok", 4) && t.status.modalStale); // forces a fresh $G
    assert(t.ingest("<Idle|MPos:0,0,0>", 5));
    assert(t.prepare("G10 L20 P0 X0 Y0 Z0", 5, w) && w == "G10 L20 P0 X0 Y0 Z0\n");
    assert(t.ingest("ok", 6) && t.ingest("<Idle|MPos:0,0,0>", 7));
    assert(!t.prepare("G10 L20 P0 X5", 7, w));        // zero only
    assert(!t.prepare("G10 L20 P0 X0 G1", 7, w));
    assert(!t.prepare("G10 L20 P0 ", 7, w));
    assert(!t.prepare("$J=G91 G21 X1", 7, w));        // jog needs a feed
    assert(!t.prepare("$J=G91 G21 X1 F100 F200", 7, w));
    assert(!t.prepare("$J=G91 G21 X900 F100", 7, w)); // beyond the 500 mm cap
    assert(!t.prepare("$J=G91 G21 Q1 F100", 7, w));
    assert(t.prepare("$J=G91 G21 X1.5 Y-2 F250", 7, w));
    assert(t.ingest("ok", 8));
    assert(jog({{'X', 1.0}, {'Y', -2.5}}, 600) == "$J=G91 G21 X1.000 Y-2.500 F600");
    assert(jog({}, 100).empty() && jog('Z', 0, 100).empty() && jog('Z', 1, 0).empty() && jog('\0', 1, 100).empty());
    assert(jog('C', 0.01, 30000) == "$J=G91 G21 C0.010 F30000");
  }
  // Realtime overrides and human readable errors.
  {
    Session t; std::string w;
    assert(!t.prepare("\x91", 0, w)); // no status yet
    assert(t.ingest("<Run|MPos:0,0,0|Ov:100,100,100>", 0));
    for (const char* o : {"\x90", "\x91", "\x92", "\x93", "\x94", "\x95", "\x96", "\x97", "\x99", "\x9a", "\x9b", "\x9c", "\x9d"})
      assert(t.prepare(o, 0, w) && w == o);
    assert(!t.prepare("\x98", 0, w) && !t.prepare("\x9e", 0, w));
    assert(t.status.overrides[0] == 100 && t.ingest("<Run|MPos:1,0,0|Ov:110,50,90>", 1) && t.status.overrides[2] == 90);
    assert(t.ingest("ALARM:1", 2) && t.status.message == "ALARM:1 Hard limit" && t.status.messageError);
    assert(t.ingest("error:999", 3) && t.status.message == "error:999");
    assert(t.ingest("[MSG:Homed:X]", 4) && t.status.message == "Homed:X" && !t.status.messageError);
    assert(!t.log.empty() && t.log.back() == "[MSG:Homed:X]");
  }
  // Controller SD / flash file listing.
  {
    Session t; std::string w;
    assert(!t.prepare("$Files/ListGcode=/sd/", 0, w));            // needs fresh Idle
    assert(t.ingest("<Idle|MPos:0,0,0>", 0));
    assert(!t.prepare("$Files/ListGcode=/sd/\x01", 0, w));
    assert(t.prepare("$Files/ListGcode=/sd/", 0, w) && w == "$Files/ListGcode=/sd/\n");
    assert(t.status.listing && t.status.pending && !t.status.awaitingStatus && t.maxLine() > 8000);
    assert(!t.prepare("G56", 0, w)); // one outstanding command
    const unsigned before = t.status.filesGeneration;
    assert(t.ingest("<Idle|MPos:0,0,0>", 100)); // status keeps flowing while listing
    assert(t.ingest("[JSON:{\"files\":[{\"name\":\"zebra.nc\",\"size\":\"1024\"},{\"name\":\"fluidtouch\",\"size\":\"-1\"},", 200));
    assert(t.ingest("{\"name\":\"Alpha.gcode\",\"size\":2048},{\"name\":\"a \\\"q\\\".nc\",\"size\":\"7\",\"extra\":[1,{\"x\":2}]}],\"path\":\"/sd\",\"total\":\"3 GB\",\"used\":\"1 MB\",\"occupation\":\"0\"}]", 300));
    assert(t.ingest("[GC:G0 G54]", 350));  // other brackets are not part of the reply
    assert(t.ingest("ok", 400));
    assert(!t.status.listing && !t.status.pending && t.status.filesGeneration == before + 1);
    assert(t.files.valid && t.files.path == "/sd/" && t.files.entries.size() == 4);
    assert(t.files.entries[0].name == "fluidtouch" && t.files.entries[0].directory); // folders first
    assert(t.files.entries[1].name == "a \"q\".nc" && t.files.entries[1].size == 7);
    assert(t.files.entries[2].name == "Alpha.gcode" && t.files.entries[2].size == 2048);
    assert(t.files.entries[3].name == "zebra.nc" && !t.files.entries[3].directory);
    assert(t.status.modal == "G0 G54"); // handled as modal state, not as reply text
    // Bare JSON without wrapper, empty directory, and an error reply.
    assert(t.ingest("<Idle|MPos:0,0,0>", 401));
    assert(t.prepare("$Files/ListGcode=/sd/empty", 401, w));
    assert(t.ingest("{\"files\":[],\"path\":\"/sd/empty\"}", 402) && t.ingest("ok", 403));
    assert(t.files.valid && t.files.entries.empty());
    assert(t.ingest("<Idle|MPos:0,0,0>", 404) && t.prepare("$Files/ListGcode=/sd/nope", 404, w));
    assert(t.ingest("error:63", 405) && !t.status.listing && !t.status.pending);
    assert(!t.files.valid && t.files.error == "error:63 SD directory not found");
    assert(t.ingest("<Idle|MPos:0,0,0>", 406) && t.prepare("$Files/ListGcode=/sd/x", 406, w));
    assert(t.ingest("{\"files\":[{\"name\":\"a\"", 407) && t.ingest("ok", 408));
    assert(!t.files.valid && t.files.error == "Unreadable file list");
    assert(t.ingest("<Idle|MPos:0,0,0>", 409) && t.prepare("$Files/ListGcode=/sd/x", 409, w));
    assert(t.ingest("{\"error\":\"Bad path\"}", 410) && t.ingest("ok", 411) && t.files.error == "Bad path");
    // Oversized reply is refused rather than buffered without bound.
    assert(t.ingest("<Idle|MPos:0,0,0>", 412) && t.prepare("$Files/ListGcode=/sd/big", 412, w));
    std::string chunk(8000, 'x');
    assert(t.ingest(chunk.c_str(), 413) && t.ingest(chunk.c_str(), 414));
    assert(t.ingest("ok", 415) && !t.files.valid && t.files.error == "Too many files to list");
    // A listing that never completes must not hide behind a late ok.
    assert(t.ingest("<Idle|MPos:0,0,0>", 416) && t.prepare("$Files/ListGcode=/sd/slow", 416, w));
    t.tick(416 + 20000);
    assert(t.status.uncertain && !t.status.listing && !t.status.pending);
    assert(t.maxLine() == Session::kLine);
    assert(parseFileList("{\"files\":[{\"name\":\"\\u00e9.nc\",\"size\":1}]}", t.files) && t.files.entries[0].name == "\xc3\xa9.nc");
    assert(!parseFileList("", t.files) && !parseFileList("[]", t.files) && !parseFileList("{\"files\":{}}", t.files));
  }
  // Terminal.
  {
    Session t; std::string w;
    assert(!t.prepareTerminal("$I", 0, w)); // no status
    assert(t.ingest("<Idle|MPos:0,0,0>", 0));
    assert(!t.prepareTerminal("", 0, w) && !t.prepareTerminal("$I\x01", 0, w) && !t.prepareTerminal("G1 !X", 0, w));
    assert(!t.prepareTerminal(std::string(121, 'G').c_str(), 0, w));
    assert(t.prepareTerminal("$I\r\n", 0, w) && w == "$I\n" && t.log.back() == "> $I");
    assert(!t.prepareTerminal("$G", 0, w));        // one at a time
    assert(t.ingest("[VER:3.9 FluidNC:]", 1) && t.log.back() == "[VER:3.9 FluidNC:]");
    assert(t.ingest("$10=1", 1) && t.log.back() == "$10=1");
    assert(t.ingest("ok", 2) && t.log.back() == "ok");
    assert(!t.prepareTerminal("$I", 2, w));        // needs a status report after the reply
    assert(t.ingest("<Idle|MPos:0,0,0>", 3));
    assert(!t.ingest("stray text", 3) && t.log.back() == "ok"); // not logged when no terminal command is open
    assert(t.prepareTerminal("$H", 3, w));
    for (unsigned at = 100; at < 10100; at += 100) { assert(t.ingest("<Home|MPos:0,0,0>", at)); t.tick(at); }
    assert(t.status.pending && !t.status.uncertain); // homing may take far longer than ordinary commands
    assert(t.ingest("ok", 10105));
    assert(t.ingest("<Run|MPos:0,0,0>", 10106) && !t.prepareTerminal("$I", 10106, w)); // only Idle/Alarm
    assert(t.ingest("<Alarm|MPos:0,0,0>", 10107) && t.prepareTerminal("$X", 10107, w));
    for (int i = 0; i < 200; ++i) t.ingest(("[MSG:note " + std::to_string(i) + "]").c_str(), 10008);
    assert(t.log.size() == 80 && t.log.back() == "[MSG:note 199]");
    const unsigned gen = t.status.logGeneration;
    t.ingest("[MSG:more]", 10009);
    assert(t.status.logGeneration == gen + 1);
    // Reset keeps the monotonically increasing generation counters.
    assert(t.prepare("\x18", 10010, w) && t.status.logGeneration == gen + 1);
    assert(t.status.resetting && t.status.uncertain);
    t.tick(10010 + 5001);
    assert(!t.status.resetting && t.status.uncertain); // no banner: stays latched, header says Reset required
    assert(t.prepare("\x18", 15100, w) && t.status.resetting);
    assert(t.ingest("Grbl 3.9 [FluidNC v3.9.5 (wifi) '$' for help]", 15200) && !t.status.resetting && !t.status.uncertain);
    assert(t.ingest("Grbl 3.9 [FluidNC v3.9.5 (wifi) '$' for help]", 10011) &&
           t.status.firmware == "Grbl 3.9 [FluidNC v3.9.5 (wifi) '$' for help]");
  }

  {  // Held-control (continuous) jog
    using Weights = std::vector<std::pair<char, double>>;
    const std::string cancel = "\x85";
    Session j; std::string w;
    auto idle = [&](uint64_t t) { assert(j.ingest("<Idle|MPos:0,0,0>", t)); };
    auto moving = [&](uint64_t t) { assert(j.ingest("<Jog|MPos:5,0,0>", t)); };
    idle(0);
    assert(!j.status.jogHeld);
    // The first chunk is 100 ms of travel (600 mm/min = 1 mm); a new one waits for the previous ok.
    assert(j.jogHold({{'X', 1}}, 600, 10, w) && w == "$J=G91 G21 X1.000 F600\n");
    assert(j.status.jogHeld && j.status.pending && !j.status.awaitingStatus);
    assert(j.jogHold({{'X', 1}}, 600, 40, w) && w.empty());
    assert(j.ingest("ok", 45) && j.status.message != "Command accepted"); // chunk acks are silent
    assert(!j.prepare("$G", 46, w) && !j.prepare(grbl::jog('Y', 1, 100).c_str(), 46, w)); // no other line commands
    assert(j.prepare("?", 46, w) && w == "?");                                              // realtime is fine
    // One second of holding feeds about ten chunks, never two at once, and never goes stale.
    unsigned chunks = 1;
    for (uint64_t t = 50; t < 1010; t += 30) {
      if (t % 240 < 30) moving(t);
      if (j.status.pending) assert(j.ingest("ok", t));
      assert(j.jogHold({{'X', 1}}, 600, t, w) && w.find('\x85') == std::string::npos);
      if (!w.empty()) { ++chunks; assert(j.status.pending); }
    }
    assert(chunks >= 9 && chunks <= 12);
    assert(j.jogRelease(w) && w == cancel && !j.status.jogHeld);
    assert(!j.jogRelease(w) && w.empty());
    assert(j.ingest("ok", 1015));
    // Still showing "Jog" right after release: a new hold waits for Idle and says why, once.
    moving(1020);
    assert(!j.jogHold({{'X', 1}}, 600, 1030, w) && w.empty() && j.status.message == "Jog needs an idle machine");
    assert(!j.jogHold({{'X', 1}}, 600, 1060, w) && w.empty());   // latched until released
    idle(1100);
    assert(!j.jogHold({{'X', 1}}, 600, 1110, w) && w.empty());   // still the same press
    assert(!j.jogRelease(w) && w.empty());
    assert(j.jogHold({{'X', 1}}, 600, 1200, w) && w == "$J=G91 G21 X1.000 F600\n");
    // Diagonal, partial deflection and slow speeds scale the feed, not the direction.
    assert(j.ingest("ok", 1201));
    assert(j.jogRelease(w)); idle(1210);
    assert(j.jogHold({{'X', 1}, {'Y', -1}}, 600, 1300, w) && w == "$J=G91 G21 X0.707 Y-0.707 F600\n");
    assert(j.ingest("ok", 1301) && j.jogRelease(w)); idle(1310);
    assert(j.jogHold({{'Z', 0.5}}, 600, 1400, w) && w == "$J=G91 G21 Z0.500 F300\n");
    assert(j.ingest("ok", 1401));
    // Reversing direction cancels the old direction first.
    assert(j.jogHold({{'Z', -0.5}}, 600, 1450, w) && w == cancel + "$J=G91 G21 Z-0.500 F300\n");
    assert(j.ingest("ok", 1451));
    // Back to the dead zone stops the motion but the finger is still allowed to move out again.
    assert(j.jogHold({{'Z', 0.0}}, 600, 1500, w) && w == cancel && !j.status.jogHeld);
    assert(j.jogHold({{'Z', 0.0}}, 600, 1530, w) && w.empty());
    moving(1540);
    assert(!j.jogHold({{'Z', 1}}, 600, 1550, w) && w.empty());    // controller not idle yet
    j.jogRelease(w); idle(1600);
    assert(j.jogHold({{'Z', 1}}, 600, 1610, w) && !w.empty());
    assert(j.ingest("ok", 1611));

    // An alarm, an error (e.g. soft limit) or a halt ends the hold; the cancel is written once.
    assert(j.ingest("ALARM:1", 1650) && !j.status.jogHeld);
    assert(!j.jogHold({{'Z', 1}}, 600, 1660, w) && w == cancel);
    assert(!j.jogHold({{'Z', 1}}, 600, 1690, w) && w.empty());
    assert(!j.jogRelease(w) && w.empty());
    assert(j.ingest("<Idle|MPos:0,0,0>", 1700));
    assert(j.jogHold({{'Y', 1}}, 600, 1710, w) && !w.empty());
    assert(j.ingest("error:15", 1711) && !j.status.jogHeld && j.status.message.find("15") != std::string::npos);
    assert(!j.jogHold({{'Y', 1}}, 600, 1720, w) && w == cancel);
    j.jogRelease(w); idle(1730);
    assert(j.jogHold({{'Y', 1}}, 600, 1740, w) && j.ingest("ok", 1741));
    assert(j.prepare("!", 1750, w) && !j.status.jogHeld);          // hold / stop / reset end it too
    assert(!j.jogHold({{'Y', 1}}, 600, 1760, w) && w.empty());
    j.jogRelease(w); idle(1770);
    assert(j.jogHold({{'Y', 1}}, 600, 1780, w) && j.ingest("ok", 1781));
    assert(j.prepare("\x85", 1790, w) && !j.status.jogHeld && !j.jogHold({{'Y', 1}}, 600, 1800, w));
    j.jogRelease(w); idle(1810);

    // Stale status ends the hold.
    assert(j.jogHold({{'X', 1}}, 600, 1820, w) && j.ingest("ok", 1821));
    assert(!j.jogHold({{'X', 1}}, 600, 4000, w) && w == cancel);
    j.jogRelease(w); idle(4010);
    // A chunk that never gets its ok latches the uncertain state and ends the hold.
    assert(j.jogHold({{'X', 1}}, 600, 4020, w) && !w.empty());
    for (uint64_t t = 4100; t <= 6100; t += 100) { moving(t); j.jogHold({{'X', 1}}, 600, t, w); }
    assert(j.status.uncertain && !j.status.jogHeld);
    // A failed write does the same and cannot be retried.
    Session k; idle(0); k.ingest("<Idle|MPos:0,0,0>", 0);
    assert(k.jogHold({{'X', 1}}, 600, 10, w) && !w.empty());
    k.sendFailed();
    assert(k.status.uncertain && !k.status.jogHeld && !k.jogHold({{'X', 1}}, 600, 20, w));
    k.cancelFailed();
    assert(k.status.message.find("Hold") != std::string::npos);

    // Time limit: a stuck touch cannot jog forever.
    Session m; m.ingest("<Idle|MPos:0,0,0>", 0);
    assert(m.jogHold({{'X', 1}}, 600, 10, w));
    bool held = true;
    uint64_t t = 10;
    for (; held && t < 40000; t += 30) {
      if (t % 240 < 30) m.ingest("<Jog|MPos:5,0,0>", t);
      if (m.status.pending) m.ingest("ok", t);
      held = m.jogHold({{'X', 1}}, 600, t, w);
    }
    assert(!held && w == cancel && t > 30000 && t < 30100 && m.status.message.find("limit") != std::string::npos);

    // Watchdog: a UI that stops calling jogHold() gets the machine stopped.
    Session d; d.ingest("<Idle|MPos:0,0,0>", 0);
    assert(!d.jogWatchdog(5, w) && d.jogHold({{'X', 1}}, 600, 10, w));
    assert(!d.jogWatchdog(300, w) && w.empty());
    assert(d.jogHold({{'X', 1}}, 600, 310, w));
    assert(!d.jogWatchdog(700, w));                       // heartbeat renewed at 310
    assert(d.jogWatchdog(710, w) && w == cancel && !d.status.jogHeld);
    assert(!d.jogWatchdog(1000, w) && !d.jogHold({{'X', 1}}, 600, 720, w));
    // A controller restart forgets the hold.
    Session r; r.ingest("<Idle|MPos:0,0,0>", 0);
    assert(r.jogHold({{'X', 1}}, 600, 10, w));
    assert(r.ingest("Grbl 3.9 [FluidNC v3.9.5]", 50) && !r.status.jogHeld);
    assert(!r.jogHold({{'X', 1}}, 600, 60, w) && w.empty());

    // Garbage never reaches the wire and ends the press.
    const Weights bad[] = {{{'X', 2}}, {{'Q', 1}}, {{'X', 1}, {'X', -1}}, {{'X', NAN}}, {{'X', 1}, {'Y', 1}, {'Z', 1}, {'A', 1}, {'B', 1}, {'C', 1}, {'U', 1}}};
    for (const Weights& b : bad) {
      Session g; g.ingest("<Idle|MPos:0,0,0>", 0);
      assert(!g.jogHold(b, 600, 10, w) && w.empty() && !g.status.pending);
    }
    for (unsigned feed : {0u, 30001u}) {
      Session g; g.ingest("<Idle|MPos:0,0,0>", 0);
      assert(!g.jogHold({{'X', 1}}, feed, 10, w) && w.empty());
    }
    Session slow; slow.ingest("<Idle|MPos:0,0,0>", 0);
    assert(slow.jogHold({{'X', 0.001}}, 1, 10, w) && w.empty() && !slow.status.pending); // too slow for a 0.001 step
    Session fast; fast.ingest("<Idle|MPos:0,0,0>", 0);
    assert(fast.jogHold({{'X', 1}}, 30000, 10, w) && w == "$J=G91 G21 X50.000 F30000\n");
    Session alarmed; alarmed.ingest("<Alarm|MPos:0,0,0>", 0);
    assert(!alarmed.jogHold({{'X', 1}}, 600, 10, w) && w.empty());
  }
  {
    // FluidNC echoes UART input until Ctrl-L: ask once per (re)connect, boot or controller restart.
    Session e;
    assert(!e.takeEchoOff());
    assert(e.ingest("<Idle|MPos:1,2,3>", 10) && e.takeEchoOff() && !e.takeEchoOff());
    assert(e.ingest("<Idle|MPos:1,2,3>", 20) && !e.takeEchoOff());          // steady state: no repeats
    assert(!e.ingest("<Idle|MPos:NaN,2,3>", 30) && !e.takeEchoOff());
    assert(e.ingest("Grbl 3.9 [FluidNC v3.9.5]", 40) && !e.takeEchoOff());  // restart alone is not a reply
    assert(e.ingest("<Idle|MPos:1,2,3>", 50) && e.takeEchoOff());
    e.tick(50 + 5000);                                                      // link lost
    assert(e.status.state == "Disconnected" && !e.takeEchoOff());
    assert(e.ingest("<Idle|MPos:1,2,3>", 6000) && e.takeEchoOff());
    e.echoOffFailed();                                                      // write failed: ask again
    assert(e.takeEchoOff() && !e.takeEchoOff());
  }
  std::cout << "GRBL parser and command lifecycle tests passed\n";
}
