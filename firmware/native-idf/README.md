# Native ESP-IDF Firmware Spike

This folder is the pure native ESP-IDF path for Quotes Clock. It owns provisioning, UI, config, networking, OTA, and display refresh directly.

## Current Scope

- ESP-IDF app scaffold for ESP32-WROOM-32D.
- 4 MB flash layout with two `0x190000` OTA app slots, a separate `0xC0000` `quote_data` partition, and tiny SPIFFS.
- Generated bitmap font assets plus a versioned native quote pack from `data/quotes.sample.yaml` by default.
- Native JD79660/GDEM075F52 display controller using hardware SPI on the confirmed ESP32-M075 pin mapping, with DMA enabled for framebuffer chunks.
- Clock/quote rendering, all four orientations, optional clock/quote/sidebar/top-bar/bottom-bar visibility, selectable time-specific/classic quote categories, configurable main-pane and bar colours/date formats, quote time-phrase highlighting, enlarged clock-only rendering, main-pane PTL partial refresh, clock-glyph minute partial refreshes, daily full-refresh guard, refresh timing telemetry, and async display task/watchdog model.
- Setup-screen rendering that uses the native fallback AP naming convention: `quotes-clock-xxxx`.
- Native Wi-Fi STA ownership with DHCP/static IPv4 settings and a fallback setup AP.
- Native NVS-backed config for admin auth, network, display, SNTP, timezone, DST, and NTP server choices.
- HTTPS-only setup/admin surface on port 443 with explicit POST endpoints for first-run password setup, Wi-Fi, display, time, refresh, and OTA upload. The Display tab saves controls immediately and groups power, clock/highlight/category controls, main-pane appearance, bar appearance, layout, and spacing controls.
- Native SNTP ownership with default pool, DHCP option 42, or manual server modes.

## Security Notes

The HTTPS server uses build-time bootstrap TLS material from local or CI secrets. That keeps Wi-Fi and admin credentials off plain HTTP during this spike and keeps the private key out of source, but every device flashed from the same artifact still shares the same TLS identity. Before this becomes a product build, replace it with first-boot per-device certificate generation or uploaded user-managed certificates.

## Build

From the repo root:

```powershell
.\build.ps1 compile-native-idf
.\build.ps1 check-native-idf-size
```

Build a single rescue-flash image containing the bootloader, partition table, OTA data, app, and `quote_data` partition:

```powershell
.\build.ps1 native-idf-rescue-bin
```

Flash a device:

```powershell
.\build.ps1 flash-native-idf -SerialPort COM6
# or, on Linux:
make flash-native-idf PORT=/dev/ttyUSB0
```

Flash only the quote data partition after quote changes, or after moving to this partition table:

```powershell
.\build.ps1 flash-native-idf-quote-data -SerialPort COM6
# or, on Linux:
make flash-native-idf-quote-data PORT=/dev/ttyUSB0
```

The native build generates:

- `firmware/native-idf/main/generated/quotes_clock_assets.hpp`
- `firmware/native-idf/main/generated/quote_data.bin`
- `firmware/native-idf/main/generated/tls_bootstrap.hpp`
- `firmware/native-idf/build/quotes-clock-native-rescue.bin`

Generated files, build products, `sdkconfig`, and dependency locks are intentionally ignored.

The CI workflow publishes the rescue image from push builds as a Gitea generic package named `quotes-clock-native`, versioned by the short commit SHA. Package upload uses the repository Actions secret `PACKAGES_TOKEN`.

For local builds, copy `.env.example` to `.env` and set either `QUOTES_CLOCK_TLS_CERT_PEM` / `QUOTES_CLOCK_TLS_KEY_PEM` or their base64 forms, `QUOTES_CLOCK_TLS_CERT_PEM_B64` / `QUOTES_CLOCK_TLS_KEY_PEM_B64`. Linux CI builds should set the same values as repository secrets; the `Makefile` path reads them from the environment and can use an ephemeral throwaway fallback certificate for untrusted PR build validation.

Generation targets are split so quote-data flashes do not touch TLS material:

```powershell
.\build.ps1 native-idf-assets
.\build.ps1 native-idf-tls
.\build.ps1 native-idf-generated
```

## Next Checkpoints

1. Exercise DHCP/static IP, DHCP option 42 SNTP, manual NTP, bad Wi-Fi recovery, and the saved-Wi-Fi clear action.
2. Add OTA/update UI handling for the separate quote pack once the binary format settles.
3. Harden auth storage beyond salted SHA-256 and add user-provided certificate/key management.
4. Expand the settings UI toward the full product option set after the platform path is proven.
