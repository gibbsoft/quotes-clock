# Quotes Clock Roadmap

## Planning Notes

- [ESP32-M075 bring-up checklist](docs/esp32-m075-bringup.md)
- [Content pipeline](docs/content-pipeline.md)
- [Datasets](docs/datasets.md)
- [ESP32-WROOM-32D notes](docs/esp32-wroom-32d-notes.md)
- [Quote library](docs/quote-library.md)
- [Timekeeping](docs/timekeeping.md)
- [Native ESP-IDF firmware spike](firmware/native-idf/README.md)
- [Wi-Fi provisioning](docs/provisioning.md)
- [Device settings](docs/device-settings.md)
- [Static OTA hosting](docs/ota-static-hosting.md)

## MVP Hardware And Firmware

The first milestone is a two-device MVP using the 7.5 inch four-color e-paper tags as calm literary clocks. The base quote clock should work without any cloud service or local server; external services only add covers, news, weather, calendar, and other enrichment.

- [x] Back up the original ESP32-M075 firmware over USB before flashing either unit.
- [x] Treat the board as a standard ESP32-WROOM-32D unless hardware inspection says otherwise.
- [ ] Record chip details, flash size, MAC address, partition table, boot logs, and verified hashes for duplicate flash dumps. Local ignored notes now capture the first unit's observed module, MAC, backup hash, pin map, and display timing; exact `chip_id`, `flash_id`, partition table, and duplicate dump verification are still missing.
- [x] Flash one unit first with a minimal proof-of-life build: Wi-Fi, logs, OTA, safe mode, and no display writes until the board is understood.
- [x] Use SNTP when network is available, but keep the quote display loop offline-capable after a successful time sync.
- [ ] Ship small-batch proof-of-concept devices with fallback AP/captive portal provisioning rather than baked-in user Wi-Fi credentials. Native fallback AP provisioning is working on the first unit. No small batch has shipped.
- [ ] Add runtime settings for network mode, DHCP/static IP details, NTP overrides, content modes, enrichment features, quiet mode, display options, and local server URL. Native web controls now cover refresh interval, layout, timezone, daylight saving, DHCP/static station addressing, static DNS, and NTP source selection via default pool, DHCP option 42, or manual servers. Content/enrichment/server settings are still pending.
- [ ] Add a secure local admin UI with first-run password setup, password change, authenticated settings access, HTTPS on port 443, build-time bootstrap TLS material, and support for uploading a user-provided certificate/key pair. Native firmware has first-run password setup, login/logout, authenticated settings access, HTTPS on port 443, and TLS generation from local or CI secrets; stronger password storage and certificate management are still pending.
- [x] Add multiple selectable display layouts with a web UI control. Include at least landscape, portrait, and upside-down rotation options, with the selected layout persisted across reboots.
- [x] Spike a native ESP-IDF firmware path focused on the display driver, especially the vendor fast-update sequence. The native firmware under `firmware/native-idf` owns Wi-Fi STA, fallback AP, NVS config, first-run admin auth, HTTPS-only setup/settings endpoints, native SNTP modes, OTA upload, generated font assets, a separate quote-data partition, the native bit-banged display path, all four orientations, layout-aware PTL main-pane partial refresh, the daily full-refresh guard, and the async display task/watchdog baseline.
- [x] Add Linux CI for the native ESP-IDF firmware and publish a rescue-flash package. Gitea Actions now builds in `espressif/idf:v6.0.1`, checks OTA app size, creates a monolithic rescue image containing bootloader, partition table, OTA data, app, and `quote_data`, and publishes it as the `quotes-clock-native` generic package using `PACKAGES_TOKEN`.
- [ ] Use static OTA hosting for low-cost update distribution: manifest on the Hugo/static site, firmware binaries in Cloudflare R2 or equivalent object storage. Gitea generic packages now provide CI-built rescue images for bench/recovery flashing, but firmware-side manifest polling and a stable static distribution URL are still pending.
- [x] Validate the 800x480, two-bit, four-color framebuffer format and the black, white, red, and yellow color mapping. The first color-bar test is confirmed on hardware.
- [x] Respect the slow four-color refresh time by preventing overlapping updates and sleeping the panel after every refresh.
- [x] Investigate the vendor 7.5 inch fast-update sequence and JD79660A PTL partial-window mode. The native spike now performs layout-aware main-pane partial refreshes; hardware SPI tuning is still pending.
- [x] Keep the second unit on stock firmware until backup, restore, boot, Wi-Fi, OTA, and display behavior are proven on the first.

## MVP Content Pipeline

The first content pipeline should favor reliability over cleverness. The device should display prepared assets rather than doing expensive text layout, cover processing, or summarization locally.

- [x] Store the quote library on the device so the clock always works offline. Native firmware uses a generated quote-data partition covering all 1,440 minutes.
- [x] Keep the canonical quote library in YAML so it is easy to curate and review, then compile it into a native quote-pack binary.
- [x] Split the quote pack from the app image so code-only OTA/dev builds do not rebake the stable quote table.
- [x] Render quote-only screens locally, or display locally cached pre-rendered quote screens if that proves more practical for the first build.
- [ ] Use public-domain-first literary quote datasets as the baseline quote source.
- [ ] Include title, author, source, time, and rights metadata for each quote. YAML schema supports this; firmware currently renders title and author only.
- [ ] Fetch book covers where practical, cache processed cover images, and fall back to generated title/author cards when no suitable cover is available.
- [ ] Use a server, Raspberry Pi, Home Assistant host, or development machine for optional cover fetching, RSS, LLM summaries, weather, calendar, richer layouts, dithering, and framebuffer packing.
- [ ] Experiment with apparent 10-color-style spatial dithering from the four physical panel colors for static accents and highlights; avoid using dithered blends for small text.
- [x] Let the ESP32 fall back to local quote rendering whenever enrichment services are unavailable.
- [x] If time is invalid on cold boot, show an explicit offline/waiting-for-time fallback rather than pretending a quote maps to the real current minute.

## Ambient Features

These features should be layered on after the quote clock is stable. They should feel like quiet interruptions rather than a dashboard.

- [ ] Hourly news digest: show a short digest on the hour for about one minute, then return to the quote display.
- [ ] BBC RSS support: ingest BBC RSS feeds and use thumbnail images where the feed provides them.
- [ ] Optional LLM summaries: summarize RSS items server-side into a concise digest before rendering the image for the display.
- [ ] News fallback mode: show raw RSS headlines if the LLM is unavailable, a thumbnail cannot be fetched, or summarization fails.
- [ ] Daily literary almanac: show author birthdays, publication anniversaries, public-domain poems, first lines, or "on this day" literary notes.
- [ ] Weather footer: add a small, restrained area for temperature, rain chance, sunrise/sunset, or moon phase.
- [ ] Reading/details mode: allow a button or local control to show source metadata, a longer surrounding passage, or a QR code/link for the current quote.
- [ ] Personal library mode: prefer quotes from books in a personal reading list or exported library when that data is available.
- [ ] Calendar glance: briefly show the next calendar event or a morning/evening agenda summary at configured times.
- [ ] Quiet modes: support night mode, weekend mode, fewer news interruptions, and a literary-only mode.
- [ ] Cover gallery: show cached covers or generated title cards on demand or during idle moments.

## Architecture Direction

The device should stay useful on its own. Network-dependent and compute-heavy features belong outside the ESP32 unless a later prototype proves there is a good reason to move them onto the device.

- [ ] ESP32 responsibilities: local quote selection, local quote display, Wi-Fi connection, OTA updates, display refresh, image cache management, schedule awareness, and simple fallback screens. Core prototype responsibilities are started; image cache management is not.
- [ ] Server responsibilities: optional RSS ingestion, LLM summarization, cover fetching, weather/calendar enrichment, richer layout, image processing, dithering, and packed framebuffer generation.
- [ ] Update infrastructure should be static/dormant by default: devices occasionally poll a manifest and continue normally when it is unavailable. CI now publishes native rescue binaries to Gitea packages; static manifest hosting and device polling are still pending.
- [ ] Cache rendered assets locally so the display can continue showing quotes during temporary Wi-Fi outages.
- [ ] Treat BBC thumbnails and LLM summaries as optional enhancements, not hard dependencies.

## Failure Modes To Handle

- [ ] No Wi-Fi: keep showing the local quote library and avoid blocking the normal minute update after valid time is available. First-unit testing confirmed cold offline boots render the waiting-for-time screen and can be recovered through the fallback AP. The product provisioning AP still needs review so it does not steal a user's default route/DNS when their client also has wired internet.
- [x] No valid time: show a waiting/offline fallback and keep retrying SNTP or another configured time source.
- [ ] No news feed: skip the hourly digest or show the most recent cached digest with a stale marker.
- [ ] No thumbnail: render the news digest without an image or use a simple typographic fallback.
- [ ] LLM unavailable: render raw RSS headlines instead of a summarized digest.
- [ ] Cover unavailable: render a generated title/author card.
- [x] Refresh still running: delay the next update rather than queueing overlapping display writes. A guarded refresh script now blocks scheduled and manual refresh overlap for the observed slow full-refresh window.
- [ ] OTA manifest unavailable: skip update check and continue normal clock operation.
