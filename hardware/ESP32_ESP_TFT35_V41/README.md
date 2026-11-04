# ESP-TFT35 v4.1 — FluidNC touch pendant (portrait)

This target follows the manually probed pins in `new_target.txt` and runs a
**touch-only FluidNC pendant** on the 3.5" 320x480 portrait panel. The board has
no dial or usable buttons, so everything is on the screen. Software behaviour is
host-tested; the physical LCD, touch, UART link and machine operation are
**NOT TESTED**.

## Using it

Every screen has the same frame: a header with the machine state, a **Hold /
Resume / Reset** strip that is always reachable, and a tab bar.

| Tab | What it does |
| --- | --- |
| Status | Work and machine position for the axes the controller reports, feed, spindle/laser S, overrides, job progress, modal state, active pins |
| Jog | **Step** mode: step presets (default 0.1 / 1 / 10 / 50 mm), feed presets, XY pad, Z buttons, A/B/C if present, red STOP cancels a jog. **Live** mode: hold-to-jog, see below |
| Job | Browse the controller's **SD card** or **flash**, tap a file to run it or save it as a macro. **Macros**: nine one-tap slots (tap an empty slot or press Edit to set one up) |
| Tools | Home all, unlock, laser/spindle off (M5), G54-G59, zero X/Y/Z/XYZ, feed/rapid/spindle overrides, terminal button when enabled |
| Setup | Brightness, dim timeout, step/feed presets, report units label, terminal on/off, display and touch test, reset settings |

### Live (continuous) jog

Switch the Jog tab to **Live**. The feed chips become *Max /min*.

- **Stick (X/Y):** put a finger on the circle and drag. Distance from the centre sets the speed
  (small dead zone, quadratic curve: gentle near the centre, the chosen *Max* at the rim), direction
  sets X/Y. Near an axis it locks to that axis. Sliding past the rim keeps full speed.
- **Strip (Z):** drag up for Z+, down for Z-, same speed curve.
- **A/B/C** buttons (if the controller reports those axes): move while held.
- **Lifting the finger stops the machine** (jog cancel, `0x85`). The same goes for dragging back to the
  centre, a direction reversal, leaving the Jog tab, an alarm, a controller error such as hitting a soft
  limit, a stale status report, or pressing Hold / STOP / Reset.
- It only starts from Idle, with a fresh status report. If it is refused the screen says why.

How it stays safe (all host-tested, none of it bench-tested): the pendant does not send one long move. It
feeds `$J` chunks of 100 ms of travel, each only after the previous `ok`, and keeps at most about 200 ms of
travel queued. If the pendant dies, the UI stalls or the cable is pulled, the queue runs dry and the machine
stops by itself within roughly 200 ms of motion plus its deceleration distance (at 3000 mm/min that is about
10 mm plus the ramp). A separate watchdog also sends the cancel if the UI stops feeding for 0.4 s, and a hold
ends after 30 s (lift and press again) so a stuck touch panel cannot jog forever.

- Keep **Max /min** at or below your machine's real maximum rate (`$110`-`$112`). Above it FluidNC clips the
  speed, the planner backs up and more motion stays queued than the pendant assumes. Releasing still cancels it.
- Moving up to a soft limit should end the hold with FluidNC's own error text (GRBL-style firmware rejects a
  jog that would cross the limit whole, so the machine stops short of it by up to one chunk, about 100 ms of
  travel). Not yet seen on a real machine.
- Live jog always sends `G91 G21` inside `$J`, so it moves in millimetres whatever the controller's modal state
  is, and it never changes that state.

Safety behaviour you should know about:

- Motion, homing, zeroing, offsets and file/macro runs need a fresh status report
  and an Idle machine. Homing and unlock are also offered in Alarm. Everything else is
  disabled (dimmed) and refused again by the protocol layer.
- Resume, Reset, Home, Unlock, Zero and Run always ask first. Hold does not.
- If a reply is missing, a write fails or the link drops, the header shows
  **Reset required** until the controller restarts. Nothing is retried or replayed.
- The screen dims after the timeout only while the machine is Idle or disconnected,
  and the touch that wakes it is swallowed so it cannot press a button.
- The **terminal** is off by default. Enabling it warns that it can send any text,
  including commands that start the spindle or fire the laser.
- This is not an emergency stop. Use the machine's hardware stop.

Files stay on the controller; copy them there with FluidNC first. Macro files are
conventionally in `/sd/fluidtouch/macros/`. Not implemented: probing, uploads,
delete, WiFi/ESP-NOW, several machines. See
[`docs/fluidtouch-port.md`](../../docs/fluidtouch-port.md).

## Sharing the controller with LightBurn (USB)

The pendant needs **its own UART channel** on FluidNC; LightBurn keeps the USB channel. Both are ordinary
FluidNC channels, so both can be connected at once. Example config (pins are yours to choose; the pendant is
TFT TX17 -> controller RX, TFT RX16 -> controller TX, common ground, 3.3 V levels):

```yaml
uart1:
  txd_pin: gpio.<to the pendant RX16>
  rxd_pin: gpio.<from the pendant TX17>
  baud: 115200
  mode: 8N1

uart_channel1:
  uart_num: 1
  report_interval_ms: 0
```

Behaviour with both connected, from FluidNC's channel design and how FluidTouch works (**not yet tried**):

- Each channel gets its own `ok`, `error:` and status replies, so the two programs do not confuse each other's
  commands. Alarms, resets and `[MSG:...]` lines go to both.
- While LightBurn is running a job or framing, the controller reports Run/Jog/Hold, so the pendant's motion,
  zero, home and file controls are disabled. Hold and Reset (realtime) still work, and Reset **will abort
  LightBurn's job** (it asks first).
- Anything LightBurn changes (work offsets, G54-G59, units) shows up on the pendant within a few seconds. The
  pendant's jogs are self-contained (`G91 G21` inside `$J`), so they do not depend on LightBurn's modal state.
- Avoid jogging from both at once. What FluidNC does with two simultaneous jog requests has not been checked;
  the pendant only ever acts on its own replies.
- If LightBurn soft-resets the controller on connect, the pendant shows "Controller restarted" and resyncs.

## Configuration

- ESP32-WROOM-32U-N8: 8 MB flash, no PSRAM, DIO at 40 MHz.
- UART2 RX16 / TX17, 115200, 8N1; UART0 remains the USB boot/log connection.
- LCD SPI3: MOSI23 / MISO19 / CLK18 / CS25 / DC33 / RESET27, 26.7 MHz (SPI clock is 80 MHz / n: 40 MHz gave a blank screen on the real board, 20 MHz works but scrolls slower), portrait
  320x480 (ST7796 MADCTL `0x48`), 20-line double draw buffers in internal RAM.
- Backlight GPIO5, active low, 5 kHz PWM so brightness and dimming work.
- FT6336U: I2C address 0x38, SDA0 / SCL4 at 100 kHz, RESET14. INT21 stays an
  input; touch is polled. Native portrait coordinates, no writes to factory tuning
  registers.
- Wired-only build: Wi-Fi, Bluetooth, SD and network services disabled.
- GPIO35 button and the unpopulated GPIO22 buzzer are unused.
- Standard 8 MB partition table: two 2 MB app slots plus flash filesystem.
- Settings live in NVS (namespace `pendant`); the LVGL task stack is 8 KB.

The panel's ST7796-compatible initialization is inferred from a published
sequence for **CL35BC219-40A**, not from a chip-ID measurement, and the touch panel's
axis directions relative to the glass are unknown. Because of that the UI has a
**Setup > Display and touch test** with four toggles (mirror display X/Y, invert touch
X/Y) that are applied immediately and saved, so a mirrored image or reversed touch
can be corrected on the device without reflashing. Colour order, backlight
polarity and PWM flicker, RAM headroom and communication still need the physical
board.

## Build

Initialize the pinned repository submodules, activate ESP-IDF **v5.1.5**, then:

```sh
git submodule update --init --recursive
scripts/build_esp_tft35_v41.sh
```

Build files go to `build/esp_tft35_v41`; generated sdkconfig stays there.
The repository's default board selection remains unchanged. Host tests are
separate: `scripts/test_grbl.sh`.

## Bench acceptance (not yet performed)

1. Keep the laser disabled and controller data connection disconnected.
2. Boot screen only: verify a stable image, correct colours, readable (not
   mirrored) text and active-low backlight. If the image is mirrored use the
   display toggles in the test screen; if the colours are swapped or inverted, stop
   and adjust the panel initialisation.
3. Open **Setup > Display and touch test**. Touch all four corner squares, the
   centre and release. The dot must land under the finger; fix reversed axes with the
   invert toggles. Stop if touches stay pressed or jump.
4. Navigate every tab, open a dialog and the keyboard; check the free-memory
   figure in Setup > About, UART0 logs for allocation failures, and that there are
   no crashes or reboot loops. Try brightness and the dim timeout and look for PWM flicker.
5. With laser power disabled and motion inhibited, connect a dedicated FluidNC
   UART at 115200 8N1 using **3.3 V UART levels and common ground**: TFT TX17 to
   controller RX, TFT RX16 to controller TX. The controller UART must already be
   configured as a FluidNC channel; USB logging and the pendant use different
   ports. Check live state, axes, positions and the firmware banner against FluidNC,
   and that errors and alarms appear with their text.
6. Disconnect UART: within two seconds the header must say Disconnected, controls
   disable and positions become `---`. Reconnect and verify fresh state. Any
   reply-timeout warning requires an intentional controller reset.
7. On an isolated bench controller, check exact commands and acknowledgements:
   hold/resume/reset, alarm/home/unlock, G54-G59 and zeroing, overrides, jog cancel,
   browsing SD and flash (including a folder with many files), running a harmless file,
   macro save/edit/run, and the terminal if enabled. Confirm the `$13` unit label.
   **Live jog on the bench, motors disabled or the axis free to move with nothing on it:** with
   a logic analyser or UART sniffer on TX17, hold the stick at the rim: expect about ten `$J=G91 G21 X..` lines a
   second, each after an `ok`, then `0x85` the moment the finger lifts. Pull the UART TX wire during a hold and
   confirm the machine stops within the expected distance. Hold a stick for 30 s and confirm it stops by itself.
   Jog into a soft limit and confirm it stops short with an error message. Try it with LightBurn connected and idle.
8. Only after those checks pass, commission low-speed motion with laser output
   physically disabled. Verify directions, homing/limits and actual distances.
   Laser operation requires separate interlock/output validation. The touchscreen
   is not an emergency stop. All these hardware checks currently remain NOT TESTED.

## Bench log

- 2026-10-26, image SHA256 `2018da65...d4354` flashed at 0x0 over UART0 (esptool verified the write; no backup
  of the original flash was taken, by the owner's choice). Chip ESP32-D0WD rev v1.1, 8 MB flash, 40 MHz crystal.
- Reported by the owner: display image, colours and touch look right; Setup > About shows **193 KB free heap**.
- Scroll performance (measured on the board with temporary counters, since removed): the Status, Tools and
  Setup tabs went from about 3-4 FPS to **about 10 FPS** while scrolling. What mattered, in order:
  1. The UI task ran at FreeRTOS priority 0, the same as the idle task, so every blocking wait cost up to a
     10 ms tick, and the FT6336U's ~1 ms I2C read took ~20 ms inside the LVGL loop. Touch is now read by its own
     task on core 0 (`touch_task` in `bsp.c`, ~100 samples/s; a sample older than 150 ms counts as released so a
     dead touch task can never leave a control "held") and the UI task runs at priority 2.
  2. SPI clock 20 -> 26.7 MHz. The frame is now bound by the SPI transfer of ~115,000 pixels.
  3. Smaller items: `-O2` instead of `-Os`, 2 ms LVGL tick, 16 ms redraw/touch periods, no elastic scroll, shorter fling.
  Tried and **rejected**: flash QIO at 80 MHz (the boot ROM reads garbage and the board boot-loops: keep DIO 40 MHz);
  LVGL hot code in IRAM (no measurable gain, cost 27 KB); display clock 40 MHz (blank screen).
  A render-cost benchmark for the UI is in `tests/grbl/ui/bench.cpp` (real LVGL, counts overdraw, rectangles, glyphs).
- Still not done: UART link to a FluidNC controller, anything involving motion, live jog on a machine, brightness/PWM
  flicker check, LightBurn coexistence.

## Evidence

- Supplied pinout: `new_target.txt`.
- Board discussion: https://github.com/luc-github/ESP3D-TFT/issues/82
- Exact-panel command sequence:
  https://github.com/embeddedboys/rpi_dm_yt350s006_software/blob/main/rpi-dm-cl35bc219-40a.txt
- FocalTech register and GPIO handling reference:
  https://github.com/espressif/esp-bsp/blob/master/components/lcd_touch/esp_lcd_touch_ft5x06/esp_lcd_touch_ft5x06.c
- Standard ST7796 rotation values (portrait `0x48`, landscape `0x28`) as used by TFT_eSPI.

## Local build result

ESP-IDF v5.1.5 / GCC 12.2.0 (esp-12.2.0_20230208) compiled the target with no compiler
warnings. The application is 1,216,896 bytes in the 2 MiB slot (42% free); static RAM
is about 24 KB (10.9 KB data + 13.7 KB bss), leaving the rest of the ESP32's heap to
LVGL and the UI. Esptool checks the image checksum and validation hash during
packaging. Runtime RAM headroom on the device remains unmeasured (Setup > About shows
the free heap).

Host tests (`scripts/test_grbl.sh`) pass under AddressSanitizer/UndefinedBehaviorSanitizer.
The real LVGL UI was rendered at 320x480, inspected, and exercised against a mock
controller. These are software tests, not physical UART/touch tests.

Artifacts in `build/esp_tft35_v41/`:

- `ESP-TFT35-v4.1-fluidnc-pendant-merged.bin`: combined image at **0x0**, screen only.
- `ESP3D-TFT.bin`: application only at **0x10000**, not a complete initial install.
- `build-manifest.json`: binary/source hashes, sizes and build environment notes.
- `build.log` / `validation.log`: logs from the build and host-test run, when present.
