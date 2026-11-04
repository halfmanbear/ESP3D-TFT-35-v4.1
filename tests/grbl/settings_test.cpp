#include "pendant_settings.h"
#include <cassert>
#include <iostream>

namespace pendant {
static std::string stored;
static bool present = false, failWrites = false;
bool storageRead(std::string& text) { text = stored; return present; }
bool storageWrite(const std::string& text) { if (failWrites) return false; stored = text; present = true; return true; }
}

int main() {
  using namespace pendant;
  // Defaults on a blank device.
  assert(!present);
  Settings& s = current();
  assert(s.brightness == 100 && s.sleepMinutes == 5 && !s.terminal && s.step == 1 && s.feed == 1 && !s.liveJog);
  assert(s.steps[0] == 0.1 && s.steps[3] == 50 && s.feeds[0] == 100 && s.feeds[3] == 3000);
  for (const Macro& m : s.macros) assert(!m.used());

  // Round trip, including awkward macro text.
  s.brightness = 40; s.sleepMinutes = 10; s.terminal = true; s.reportInches = true;
  s.touchFlipX = true; s.displayFlipY = true; s.step = 2; s.feed = 3; s.liveJog = true;
  s.steps[1] = 0.25; s.feeds[2] = 1500;
  s.macros[0] = {"Probe 100%", "/sd/fluidtouch/macros/probe 1.nc"};
  s.macros[8] = {"Flash", "/localfs/home.nc"};
  assert(save());
  Settings back = parse(stored);
  assert(back.brightness == 40 && back.sleepMinutes == 10 && back.terminal && back.reportInches);
  assert(back.touchFlipX && !back.touchFlipY && !back.displayFlipX && back.displayFlipY);
  assert(back.liveJog && back.step == 2 && back.feed == 3 && back.steps[1] == 0.25 && back.feeds[2] == 1500);
  assert(back.macros[0].name == "Probe 100%" && back.macros[0].path == "/sd/fluidtouch/macros/probe 1.nc");
  assert(back.macros[8].path == "/localfs/home.nc" && !back.macros[4].used());
  load();
  assert(current().brightness == 40 && current().macros[8].name == "Flash");

  // Hostile or damaged storage never produces out-of-range values.
  Settings bad = parse("brightness=0\nsleep=7\nstep=9\nfeed=-4\nsteps.0=-1\nsteps.1=nan\nsteps.2=9999\nfeeds.0=0\nfeeds.1=99999\n"
                       "steps.9=5\nfeeds.x=3\nmacro.0.path=/etc/passwd\nmacro.0.name=x\nmacro.1.path=/sd/a?b.nc\n"
                       "macro.2.path=/sd/ok.nc\nmacro.2.name=" + std::string(60, 'n') + "\nmacro.3.path=/sd/q.nc\nmacro.3.name=bad%0Aname\n"
                       "garbage line\n=\nterminal=yes\nlive=yes\n");
  assert(bad.brightness == 10 && bad.sleepMinutes == 5 && bad.step == 3 && bad.feed == 0);
  assert(bad.steps[0] == 0.1 && bad.steps[1] == 1 && bad.steps[2] == 10 && bad.feeds[0] == 100 && bad.feeds[1] == 500);
  assert(!bad.macros[0].used() && !bad.macros[1].used());
  assert(bad.macros[2].path == "/sd/ok.nc" && bad.macros[2].name == "ok.nc"); // oversize name replaced by the file name
  assert(bad.macros[3].name == "q.nc");   // control characters rejected
  assert(!bad.terminal && !bad.liveJog);  // only "1" enables
  assert(parse("").brightness == 100 && parse("\n\n\n").sleepMinutes == 5);

  assert(validMacroPath("/sd/a.nc") && validMacroPath("/localfs/x/y.gcode"));
  assert(!validMacroPath("/sd") && !validMacroPath("sd/a.nc") && !validMacroPath("/sd/a!.nc") &&
         !validMacroPath("/sd/\n.nc") && !validMacroPath("/sd/" + std::string(130, 'a')));

  // A failed write is reported and the previous copy survives.
  failWrites = true;
  current().brightness = 70;
  assert(!save() && parse(stored).brightness == 40);
  std::cout << "Pendant settings tests passed\n";
}
