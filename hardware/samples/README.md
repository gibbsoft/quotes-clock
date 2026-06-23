# Hardware Samples

This folder documents vendor and reference display code used during hardware bring-up. The vendor source files are not checked into the public tree; keep local copies out of Git unless their redistribution terms are confirmed and notices are complete.

The `GDEM075F52_Arduino` sample is not built by this project. It was used as provenance for the ESP32-M075 display configuration and as a reference for native display-driver behavior.

Confirmed ESP32-M075 e-paper mapping:

- `SCK`: GPIO18
- `MOSI`: GPIO23
- `BUSY`: GPIO13, inverted
- `RESET`: GPIO12
- `DC`: GPIO14
- `CS`: GPIO27

The vendor sample comments list `SCK--GPIO23` and `SDIN/MOSI--GPIO18`, but the Arduino code calls `SPI.begin()` without explicit pins. The ESPHome build that worked on the ESP32-M075 used the default VSPI mapping above (`SCK GPIO18`, `MOSI GPIO23`).

Native ESP-IDF hardware SPI also works on that mapping when the bus is initialized with DMA. `SPI_DMA_DISABLED` failed with the native driver's 4 KiB framebuffer chunks, while `SPI_DMA_CH_AUTO` allows both full and partial refreshes to complete.

Do not commit large sample bitmap headers such as `Ap_29demo.h`; they are local bring-up references only.
