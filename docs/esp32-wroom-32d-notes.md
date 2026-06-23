# ESP32-WROOM-32D Notes

These notes summarize project-relevant details from the ESP32-WROOM-32D/32U datasheet.

## Module Basics

- CPU: ESP32-D0WD, dual-core Xtensa LX6, up to 240 MHz.
- ROM: 448 KB.
- SRAM: 520 KB, plus 8 KB RTC SRAM.
- Flash: 4 MB SPI flash on the module.
- Crystal: 40 MHz.
- Supply: 3.0 V to 3.6 V.
- Temperature range: -40 C to 85 C.
- ESP32-WROOM-32D uses an onboard PCB antenna.
- ESP32-WROOM-32U uses an external antenna connector.

## Project Implications

- Treat the board as a standard ESP32 target unless inspection proves a more specific board is needed.
- Assume no PSRAM.
- Keep only one full 800x480 2-bit display framebuffer in RAM where possible. One framebuffer is 96,000 bytes.
- Keep cover processing, dithering, and rich image composition off-device.
- Expect 4 MB flash unless `esptool.py flash_id` proves otherwise.
- Keep generated quote data outside the app image when possible; the native firmware uses a separate quote-data partition.

## Pins To Treat Carefully

Strapping pins are sampled at reset and can affect boot behavior. Avoid driving them externally during reset unless the circuit is understood:

- GPIO0: boot mode strap, default pull-up.
- GPIO2: boot mode strap, default pull-down.
- GPIO5: strapping pin, default pull-up.
- GPIO12 / MTDI: VDD_SDIO voltage strap, default pull-down.
- GPIO15 / MTDO: U0TXD boot printing and SDIO timing strap, default pull-up.

Boot mode:

- SPI boot: GPIO0 high.
- UART download boot: GPIO0 low and GPIO2 low.

GPIO12 / MTDI is especially sensitive because it selects VDD_SDIO at reset:

- MTDI low: VDD_SDIO uses the 3.3 V domain.
- MTDI high: VDD_SDIO uses the internal 1.8 V LDO.

Do not assign display control lines to strapping pins until the board schematic or vendor sample proves the design already handles the strap safely.

## UART And Flashing

- UART0 is on GPIO1 `U0TXD` and GPIO3 `U0RXD`.
- USB flashing should work through the board's USB-to-UART path if exposed.
- During boot, serial logs may appear on U0TXD.
- Do not reuse UART0 pins for e-paper or other peripherals while relying on USB serial logs/flashing.

## Manual UART Download Strap

If automatic USB flashing does not enter the ROM bootloader, hold `IO0` low while resetting or powering the board.

For the ESP32-WROOM-32D module viewed from the top, with the PCB antenna/keepout at the top:

```text
              ESP32-WROOM-32D module, top view
                 antenna / keepout at top

      left edge                              right edge
      pin 1  GND  o|----------------------|o pin 38 GND
      pin 2  3V3  o|                      |o pin 37 IO23
      pin 3  EN   o|                      |o pin 36 IO22
                  .|                      |.
                  .|                      |.
      pin 14 IO12 o|                      |o pin 26 IO4
                  .|______________________|o pin 25 IO0 / BOOT

                         bottom edge
                  pin 15 GND, then pins 16..24
                  run along the bottom row
```

Temporary flashing strap:

```text
module pin 25  IO0 / BOOT  ---- temporary short ----  module pin 15  GND
```

Hold that short only while resetting/powering into download mode, then remove it before normal boot.

Do not confuse module pin `15` with `GPIO15`. Module pin `15` is `GND`; `GPIO15` is a different strapping pin.

## SPI Notes

The datasheet lists these common SPI mux groups:

- Flash SPI uses GPIO6 through GPIO11 internally. Do not use these for external peripherals.
- HSPI can use GPIO2, GPIO4, GPIO12 through GPIO15.
- VSPI can use GPIO5, GPIO18, GPIO19, GPIO21 through GPIO23.

The supplied GDEM075F52 Arduino sample comments mention SCK on GPIO23 and MOSI on GPIO18, which is not the usual VSPI naming. Verify the ESP32-M075 board mapping before finalizing display pins.

## Antenna Notes

For ESP32-WROOM-32D, keep metal, copper, batteries, and dense wiring away from the PCB antenna area where possible. The display enclosure may already fix this layout, but avoid adding wires or shielding over the antenna during modifications.
