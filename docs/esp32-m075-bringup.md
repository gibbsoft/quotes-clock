# ESP32-M075 Bring-Up Checklist

This checklist is for the first GDP075FW1 / ESP32-M075 unit. Keep the second unit on stock firmware until backup, restore, and display behavior are proven on the first unit.

The ESP32-M075 board is expected to be based on an ESP32-WROOM-32D module. Treat it as a standard ESP32 during initial bring-up unless board inspection proves otherwise. See [ESP32-WROOM-32D Notes](esp32-wroom-32d-notes.md).

## Before Flashing

- Photograph the board, labels, connectors, and any visible pin markings.
- Confirm the display panel is GDEM075F52 or a compatible 800x480 four-color panel.
- Confirm the MCU module marking is ESP32-WROOM-32D.
- Look for exposed buttons, key pads, BOOT/EN buttons, or labelled button test pads.
- Connect by USB only at first.
- Identify the serial port.
- Install or locate `esptool.py`.

## Firmware Backup

Run these before flashing anything:

```powershell
esptool.py --port COMx chip_id
esptool.py --port COMx flash_id
esptool.py --port COMx read_mac
```

Record the output in `hardware/bringup-notes.md` once that folder exists.

Detect the flash size, then dump the full flash. If the flash is 4 MB:

```powershell
esptool.py --port COMx --baud 460800 read_flash 0x000000 0x400000 backups/esp32-m075-stock-01.bin
esptool.py --port COMx --baud 460800 read_flash 0x000000 0x400000 backups/esp32-m075-stock-01-verify.bin
```

If the flash is 16 MB, use `0x1000000` instead of `0x400000`.

Hash both dumps and confirm they match:

```powershell
Get-FileHash backups/esp32-m075-stock-01.bin -Algorithm SHA256
Get-FileHash backups/esp32-m075-stock-01-verify.bin -Algorithm SHA256
```

Do not flash replacement firmware until the duplicate hashes match.

## First Native Flash

The first firmware build originally avoided the display and only proved that the board could boot, connect, log, and accept OTA updates. That proof-of-life step is complete.

Use `firmware/native-idf` for the current prototype firmware.
See [Timekeeping](timekeeping.md) for the offline time policy.

Use the ESP32 target unless the vendor documentation requires a more specific board profile:

```powershell
.\build.ps1 compile-native-idf
.\build.ps1 flash-native-idf -SerialPort COM6
```

Include:

- Wi-Fi
- logger
- OTA
- safe mode
- fallback access point
- API or MQTT
- SNTP time sync for proof-of-life

Avoid adding new uses of:

- status LEDs unless the pin is confirmed
- GPIOs used by flash, PSRAM, USB, boot mode, or the e-paper controller
- strapping pins during reset unless the board schematic or vendor sample proves they are safe

ESP32-WROOM-32D modules usually do not include PSRAM, so avoid designs that need multiple full-screen framebuffers or on-device image processing.

## Display Bring-Up

Use the supplied GDEM075F52 Arduino sample as the source of truth for the initial driver behavior.

Known sample details:

- Resolution: 800x480
- Framebuffer: 96,000 bytes
- Pixel format: 2 bits per pixel, 4 pixels per byte
- Colors: black, white, yellow, red
- Data command: `0x10`
- Refresh command: `0x12`
- Power off command: `0x02`
- Deep sleep command: `0x07`

The sample uses these logical pins:

- `A14`: busy
- `A15`: reset
- `A16`: data/command
- `A17`: chip select

Those map to the confirmed native GPIO pins:

- `SCK`: GPIO18
- `MOSI`: GPIO23
- `BUSY`: GPIO13, inverted
- `RESET`: GPIO12
- `DC`: GPIO14
- `CS`: GPIO27

The sample comments list SPI as `SCK--GPIO23` and `SDIN/MOSI--GPIO18`, but the code calls `SPI.begin()` without arguments. On the ESP32 default VSPI pins are `SCK GPIO18` and `MOSI GPIO23`.

## Native Display Optimisation Notes

Keep these findings with the bring-up notes so future driver work does not repeat rejected experiments.

Current native IDF display path:

- Uses hardware SPI on `SCK GPIO18` and `MOSI GPIO23`.
- Uses `SPI_DMA_CH_AUTO`; `SPI_DMA_DISABLED` failed with the native driver's 4 KiB framebuffer chunks.
- Keeps the SPI clock at 12 MHz. Earlier ESPHome testing showed 20 MHz and 40 MHz caused visible text jitter.
- Keeps the vendor-derived JD79660/GDEM075F52 init sequence, including `R30H PLL = 0x08`.
- Uses the vendor fast-update sequence `E0=0x02`, `E6=0x5A`, `A5=0x00` after power-on for partial refreshes.
- Keeps the controller in warm standby after successful partial refreshes, then wakes with `PSR_ACTIVE` plus explicit `PON`.
- Uses PTL (`0x83`) for partial-window refreshes, including tight clock-glyph rectangles for 24-hour minute ticks.
- Performs a daily full refresh before relying on partial refreshes.

Observed timings on the first unit after hardware SPI and dirty-clock rendering:

- Clock-only minute partial: about 12.0 to 12.2 seconds total.
- Clock-only minute partial panel waveform time: about 11.8 seconds.
- Clock-only minute partial transfer time: effectively 0 to 1 ms.
- Clock-only minute partial RAM render time after dirty-rectangle rendering: about 8 to 10 ms.
- Full/pane refreshes remain around 21 to 22 seconds.

Rejected or low-value experiments:

- Swapping SPI pins to the vendor sample comment mapping (`SCK GPIO23`, `MOSI GPIO18`) did not help. The working mapping is the ESPHome/default VSPI mapping.
- Hardware SPI without DMA produced invalid SPI transaction setup with the current chunk size.
- Skipping explicit `PON` when waking from warm standby caused fake short refreshes around 1 second and could leave the visible panel unchanged. Do not retry without a stronger busy/visual validation harness.
- Increasing JD79660 `R30H PLL` from the vendor value `0x08` to `0x0A` or `0x0F` produced real refreshes but no useful panel-time reduction; the clock partial stayed around 11.8 seconds of panel waveform time.
- Increasing SPI speed above 12 MHz was already rejected in the ESPHome phase because 20 MHz and 40 MHz introduced visible text jitter.
- Earlier broad AUTO/PWS/register-LUT experiments caused gray ghosting or no useful speed gain. Treat LUT/waveform changes as risky panel-quality experiments, not routine optimisations.

The main remaining time cost is the panel/controller waveform, not SPI transfer or CPU rendering.

## Display Safety

Treat the e-paper panel as a fragile high-voltage load, not just a passive screen.

- Do not leave the panel actively driven after a refresh. The display may retain a static image without power, but the driver must power off and deep-sleep the panel after each update to avoid long high-voltage exposure.
- The JD79660 path should transition through refresh, power off, and deep sleep after each update. Keep this behavior in mind if changing driver model or experimenting with partial refresh.
- Do not refresh continuously during development. Keep the quote-clock cadence at one full refresh per minute or slower unless testing a specific display behavior.
- Handle the FPC carefully. Keep the ribbon clamped and avoid pulling, tearing, or repeatedly bending it during bench work.
- Keep display logic at 3.3 V signal levels. The board is powered from USB/5 V, but the ESP32 GPIO and panel control lines should remain 3.3 V logic.
- Full-refresh flicker is normal for this panel.
- If colors look wrong in a cold environment, let the display return to room temperature before judging the result.
- For longer storage, prefer leaving the panel on a clean white screen after a final refresh, then powered down.

## Display Tests

Run tests in this order:

- No-display boot: done
- Busy pin read: done
- Reset pulse only: done
- All white: covered by color test
- All black: covered by color test
- All yellow: covered by color test
- All red: covered by color test
- Four-quadrant color test: done
- Orientation test with labels in each corner
- One full quote image
- Repeated once-per-minute refresh loop

Always send the panel to deep sleep after each refresh.

One 96 KB framebuffer is reasonable on ESP32-WROOM-32D. Avoid keeping additional full-size image buffers in RAM.

## Buttons And Manual Time

No exposed user button has been confirmed from the ESP32-WROOM-32D datasheet, the GDEM075F52 Arduino sample, or the public GDP075FW1 spec page. Inspect the physical unit for:

- external buttons;
- internal tactile switches;
- BOOT and EN buttons;
- labelled key/button test pads;
- unused GPIO pads that can safely take a button later.

If a safe button is available, it can be used for manual time fallback, mode switching, or forcing a refresh. Do not rely on this until the PCB is inspected and the GPIO is confirmed not to interfere with boot strapping, UART flashing, flash pins, or the e-paper controller.

## USB Recovery Strap

The first unit has successfully entered USB flashing without manually grounding `IO0`, so try normal `esptool.py` first.

If automatic entry fails, use the ESP32-WROOM-32D module pins directly:

```text
ESP32-WROOM-32D top view, antenna/keepout at top

             .|______________________|o pin 25 IO0 / BOOT
              ^ pin 15 GND starts     ^ first pin up the right edge
                the bottom row
```

For ROM download mode, temporarily short module pin `25` (`IO0` / `BOOT`) to module pin `15` (`GND`) while resetting or powering the board. Remove the short before normal boot.

This is module pin `15`, not `GPIO15`.

## Restore Test

Before modifying the second unit, confirm the first unit can be restored from the stock firmware backup.

Use the matching flash size from the backup step:

```powershell
esptool.py --port COMx --baud 460800 write_flash 0x000000 backups/esp32-m075-stock-01.bin
```

After restore, confirm the stock firmware boots and behaves as expected.
