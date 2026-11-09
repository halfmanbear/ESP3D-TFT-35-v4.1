# ESP-TFT35 v4.1 — FluidNC touch pendant (MKS DLC32 MAX)

**Pre-release. Experimental firmware.** The display and touch have been checked on a real
ESP-TFT35 v4.1 board. Nothing has been tested against a FluidNC controller or a machine yet.

A touch-only FluidNC pendant for the 3.5" 320x480 portrait ESP-TFT35 v4.1 screen, for use with an
MKS DLC32 MAX (or any FluidNC controller) over a wired UART. It is a new UI on the ESP3D-TFT
code base, written with FluidTouch as a guide. It does
not reuse FluidTouch code. See `docs/fluidtouch-port.md`.

## Features

- Status: work and machine position, feed, spindle/laser S, overrides, job progress, state.
- Jog: step mode (XY pad, Z, A/B/C, editable presets) and live hold-to-jog with a stick and Z strip.
- Job: browse the controller's SD card or flash, run a file, nine one-tap macros.
- Tools: home, unlock, M5, G54-G59, zeroing, feed/rapid/spindle overrides, optional terminal.
- Setup: brightness, dim timeout, presets, display and touch orientation test.
- Hold / Resume / Reset strip on every screen. Resume, Reset, Home, Unlock, Zero and Run ask first.

Not implemented: probing, file upload or delete, Wi-Fi, ESP-NOW, several machines.

## Status

| Area | State |
| --- | --- |
| Protocol, settings, services, UI logic (host tests, `scripts/test_grbl.sh`) | Passing |
| ESP-IDF v5.1.5 firmware build | Passing |
| LCD image, colours, touch, scrolling on the real board | Checked by the owner |
| UART link to a FluidNC controller | **Not tested** |
| Any machine motion, homing, live jog, file runs, laser behaviour | **Not tested** |
| Sharing the controller with LightBurn over USB | **Not tested** |

**This is not an emergency stop.** Use the machine's hardware stop. Keep the laser output disabled
until you have worked through the bench acceptance steps in
`hardware/ESP32_ESP_TFT35_V41/README.md`.

## Install

Download `ESP-TFT35-v4.1-fluidnc-pendant-merged.bin`. It is a full image (bootloader, partition
table, OTA data and app) and goes at offset `0x0`. Flashing replaces everything on the screen's
flash, and the original firmware is not kept.

```
pip install esptool
esptool.py --chip esp32 --baud 230400 write_flash --flash_mode dio --flash_freq 40m --flash_size 8MB 0x0 ESP-TFT35-v4.1-fluidnc-pendant-merged.bin
```

- Use the UART0 USB connection. Auto-reset worked on the test board, so no BOOT/RESET presses were needed.
- Use 230400 baud. 460800 failed with serial noise.
- Keep the flash mode at DIO 40 MHz. QIO or 80 MHz makes the boot ROM read garbage and the board boot-loops.
  Reflashing with the command above recovers it.
- Check the download with the `.sha256` file or `build-manifest.json`.

## Wiring

3.3 V UART levels and a common ground: screen TX17 to controller RX, screen RX16 to controller TX,
115200 8N1. The controller needs a spare UART configured as a FluidNC channel (example in the board
README). USB stays free for LightBurn.

## Known limits

- The panel initialisation is inferred from a published sequence for the CL35BC219-40A, not from a
  chip-ID measurement. If the picture is mirrored or touch is reversed, use Setup > Display and
  touch test.
- Wi-Fi, Bluetooth, SD and network services are disabled in this build. It is wired-only.
- LCD SPI clock is 26.7 MHz. 40 MHz gave a blank screen.
- `ESP3D-TFT.bin` (app only) is not attached. Flash the merged image.

## Source

Built from the tagged commit with ESP-IDF v5.1.5 by `.github/workflows/release-esp-tft35-v41.yml`.
`build-manifest.json` records the source commit and the SHA256 of every part.
