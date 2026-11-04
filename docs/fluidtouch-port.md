# FluidNC touch pendant for ESP-TFT35 v4.1

## Evidence and scope

Reference checkout: `../FluidTouch`, clean at commit
`917adcd8a0a7397c13cf0fcb546ada31d3023948` when inspected (2026-10-26).
FluidTouch is an Arduino/ESP32-S3 application for an 800x480 RGB panel with PSRAM.
This board is a plain ESP32 (no PSRAM) with a 320x480 portrait SPI panel and no
buttons or dial, so this is a new touch-only UI written for the existing
ESP-IDF/LVGL application. It follows FluidTouch's feature set where a wired UART
pendant can, and it does not import any FluidTouch code.

References inspected:

- `src/network/fluidnc_client.cpp`: framing, status reports, cached WCO, `[GC:]`,
  errors/alarms, `[MSG:RST]`, fallback polling and the startup handshake.
- `src/ui/tabs/control/*`: incremental `$J` jogging, homing, zeroing, unlock, feed
  hold/resume/reset, realtime overrides.
- `src/ui/tabs/ui_tab_files.cpp`: `$Files/ListGcode=<folder>` JSON listing,
  `$SD/Run=` / `$LocalFS/Run=`, size `-1` meaning a directory.
- `src/ui/tabs/ui_tab_macros.cpp`: nine file based macros run with `$SD/Run=`.
- `src/ui/tabs/ui_tab_terminal.cpp`: terminal, command history.
- Upstream FluidNC `Report.cpp` and `Error.h`/`ExecAlarm.h` for report syntax and
  error/alarm numbers.

FluidTouch is MIT licensed, copyright (c) 2025 Jason Yeager; its licence is kept
beside this document as `FluidTouch-LICENSE.txt`. Existing ESP3D source licence
headers are retained.

## What the pendant does

| FluidTouch feature | This port |
| --- | --- |
| Live position, state, feed/spindle | Status tab: work and machine DRO for 2-6 axes as reported, feed, S, overrides, active pins, modal state, firmware banner |
| Button jogging, step and feed presets | Jog tab, Step mode: XY pad, Z column, A/B/C when the controller reports them, four editable step and four feed presets, STOP cancels a jog |
| Joystick jog | Jog tab, Live mode: an XY stick and a Z strip with a speed curve, hold-to-jog A/B/C buttons. FluidTouch streams `$J` chunks every 50 ms and cancels on release; this does the same with a bounded look-ahead, a watchdog and a time limit (below) |
| Homing, unlock, zero, work offsets | Tools tab: `$H`, `$X`, M5, G54-G59 select, zero X/Y/Z/extra axes or XYZ with confirmation |
| Feed hold / resume / reset | Persistent strip on every screen; resume and reset ask first |
| Overrides | Feed, rapid and spindle/laser S using the FluidNC realtime bytes |
| File browser and run | Job tab: browse FluidNC SD and flash, run a file (`$SD/Run=`, `$LocalFS/Run=`). Delete, upload and the display's own SD card are not implemented |
| Nine macros | Job tab: nine slots stored in NVS, tap to run, Edit mode to rename/repoint, "Save as macro" from the file sheet |
| Terminal | Opt-in (Setup > Controller), command history, transcript of replies |
| Persistent settings | NVS: brightness, dim timeout, presets, units label, macros, terminal, display/touch orientation, selected presets |
| Power management | Dim after a timeout; never while the machine is moving. No deep sleep or battery handling |
| Reduced-axis machines | Rows and jog buttons follow the axes in the status report |
| WiFi, ESP-NOW, multi-machine, probing, settings backup, screenshots | Not implemented: UART only, one machine |

## Protocol behaviour (host tested)

- UART only. Exact realtime bytes (`?`, `!`, `~`, `0x18`, `0x85`, `0x90-0x9D`), no
  Marlin numbering or checksums, 250 ms status polling, occasional `$G`.
- One outstanding line command. `ok`/`error:N`/`ALARM:N` complete it; error and alarm
  numbers are shown with a short description. Nothing is replayed after a failure.
- Homing waits up to 120 s; a file listing 20 s; the terminal 10 s; everything else 5 s.
  A missing reply, a failed write or a lost link latches "Reset required" until the
  controller restarts. A late `ok` cannot clear it.
- Live jog (`Session::jogHold`, fed every ~30 ms from the touch events): `$J=G91 G21` chunks of 100 ms of
  travel at the requested feed, one per `ok`, never more than about 200 ms queued; each axis weight is a
  fraction of the chosen maximum feed. It starts only from a fresh Idle status and continues while the state is
  Idle or Jog. It ends, with `0x85`, on release, centre, reversal, tab change, alarm, any `error:`, stale
  status, a missing `ok` after 2 s, a failed write, Hold/STOP/Reset, after 30 s, or when no update arrives for
  0.4 s (watchdog in the UART service, independent of the UI task). While a hold is active no other line command
  is accepted and the `$G` poll is paused. A failed cancel write latches "Reset required".
- Status older than 2 s disables normal actions. Offsets are never assumed zero; WCO
  is cached, invalidated on restart, home, zero and `G10`.
- Only whitelisted pendant commands are accepted: `$H`, `$X`, `$G`, `M5`, `G54`-`G59`,
  `G10 L20 P0 <axis>0 ...`, `$J=G91 G21 <axes> F<feed>` (axes XYZABC, 500 mm and
  30000 mm/min caps), `$Files/ListGcode=`, `$SD/Run=`, `$LocalFS/Run=`. Control
  characters and the realtime characters `? ! ~` are rejected in line commands.
- The terminal is the one exception: any printable line, only when Idle or in
  Alarm, off by default and behind a warning. It can start the spindle or laser.
- File lists arrive as one long `[JSON:{...}]` line, or several, followed by `ok`.
  They are collected up to 12 KB (about 250 files); larger folders report an error.
- Incoming lines are reassembled across UART chunks, CRLF is supported, ordinary
  lines over 1023 bytes are discarded through the next delimiter, and a recognised
  numeric field that is malformed rejects the whole status report.
- Coordinates and the reported feed keep the controller's `$13` units. The Setup tab
  only changes the unit label. Jogging always sends `G91 G21`, so steps are mm.

## UI notes

- 320x480 portrait, dark theme, 48 px class touch targets, bottom tab bar, no
  hardware buttons needed. Fonts: Montserrat 12/14/16/20/28 plus the ESP3D icon set.
- Only the active tab's widgets exist, shared styles are used and `lv_obj_remove_style_all`
  strips the default theme from the app's own objects, to stay inside a no-PSRAM
  ESP32's heap. Host measurement: about 50 KB total (64-bit), 0.7-6 KB per tab, no growth
  over 40 rounds of tab and dialog cycling.
- Controls that need an idle machine are disabled (dimmed colours, not LVGL's `opa`
  property, which allocates a layer buffer) and every command is validated again in
  the protocol layer.
- Rendering and wake-up: while dimmed a transparent layer absorbs the waking touch so
  it cannot press a button. Dimming is suppressed whenever the state is not Idle,
  Sleep or disconnected.
- The previous ESP3D menu, settings and information screens are still compiled but
  are not reachable from this UI.

## Validation

Run `scripts/test_grbl.sh` on a host with CMake and a C++17 compiler (Python 3 is used
to turn the screenshots into PNG). It builds with AddressSanitizer and
UndefinedBehaviorSanitizer and runs:

1. Protocol tests: offsets, both coordinate formats, 2-6 axes, malformed reports,
   stale status, command gating and validation, jog bytes, reply ordering, timeouts,
   late acknowledgements, reset state, homing, WCS, overrides, error text, file list
   parsing (wrapper, escapes, sorting, errors, oversize, timeout), terminal gating, and the whole
   held-jog life cycle (chunk pacing, one outstanding line, every stop condition, watchdog, bad input).
2. Settings tests: defaults, round trip, hostile or damaged storage, write failure.
3. Service tests with a mocked clock and UART: fragmented lines, CRLF, overflow
   recovery, a 2 KB file list in 512 byte chunks, raw bytes, polling, terminal.
4. The real LVGL UI at 320x480 against a simulated controller: every tab, dialog,
   keyboard editor, preset edit, macro edit, terminal, display/touch test, dimming,
   toasts and 25 screen rebuilds, and live jog driven by a simulated finger on a real LVGL
   pointer device (stick, strip, A/B/C hold, release, alarm mid-hold, refusal, tab change), checking the exact bytes sent and the saved
   settings, plus a leak check. Screenshots go to `build/grbl-ui-test/previews`.

Then run `scripts/build_esp_tft35_v41.sh` with ESP-IDF v5.1.5 active. The packaging
step writes `ESP-TFT35-v4.1-fluidnc-pendant-merged.bin` and a SHA256 manifest in
`build/esp_tft35_v41`.

**Software tests and a successful build are not hardware validation.** The LCD,
touch orientation and polarity, backlight PWM, UART link, free heap, controller SD
execution, interlocks, motion and laser behaviour remain NOT TESTED. Follow the
bench steps in the board README before any operational use.
