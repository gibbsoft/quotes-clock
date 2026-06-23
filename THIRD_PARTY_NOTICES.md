# Third-Party Notices

Quotes Clock source code, firmware, scripts, and project documentation are
distributed under the MIT License. Third-party software, generated datasets,
quoted passages, OEM references, and externally sourced materials remain subject
to their own licenses, copyrights, and usage restrictions.

This notice is a working release checklist input and should be reviewed before
shipping binaries or curated quote datasets.

## Firmware Components

### Espressif IoT Development Framework

- Source: https://github.com/espressif/esp-idf
- License: Apache-2.0
- Use: ESP32 firmware build system, platform APIs, Wi-Fi, HTTPS server, OTA,
  NVS, partition tooling, and related system services.

### FreeRTOS

- Source: https://www.freertos.org/
- License: MIT
- Use: RTOS scheduler and task primitives as supplied through ESP-IDF.

### mbedTLS

- Source: https://github.com/Mbed-TLS/mbedtls
- License: Apache-2.0
- Use: TLS and cryptography components as supplied through ESP-IDF.

### casio-f91w-fsm

- Source: https://github.com/jdno/casio-f91w-fsm
- License: MIT
- Copyright: Copyright (c) 2023 Jakub Dundalek; Copyright (c) 2020 Alexis
  Philip.
- Use: reference SVG segment geometry for the optional Casio-inspired watch
  style clock display.

## Quote And Content Sources

The public repository keeps only sample quote data. Import tools can generate
local staging files from the sources below, but those generated outputs are
ignored by Git until individual quote entries are reviewed for redistribution.

### alvations/Quotables

- Source: https://github.com/alvations/Quotables
- Imported file: `author-quote.txt`
- License claimed by source repository: CC0-1.0
- Generated output: `data/quotes.classics.yaml`
- Use: general/classic quote category for prototype display testing.
- Rights note: the source repository declares CC0-1.0, but individual quote
  provenance should still be reviewed before production distribution.

### literature-clock npm package

- Source: https://cdn.jsdelivr.net/npm/literature-clock/
- Imported file: `quotes.csv`
- Package license: MIT
- Generated output: `data/quotes.literature-clock.yaml`
- Use: time-specific literary quote staging data.
- Rights note: the package license covers the dataset package, but quoted book
  passages may have separate rights.

### JohannesNE/literature-clock

- Source: https://github.com/JohannesNE/literature-clock
- Imported file: `litclock_annotated.csv`
- Generated output: `data/quotes.johannesne-literature-clock.yaml`
- Use: time-specific literary quote staging data.
- Rights note: review individual generated rows before redistribution.

## Python Tooling

Repository-side build and import tools use Python packages managed by
`pyproject.toml` and `uv.lock`. Those tools are not embedded in the firmware
image, but their licenses should be reviewed for release tooling distribution.
