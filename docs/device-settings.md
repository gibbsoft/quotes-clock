# Device Settings

Small-batch devices need runtime settings that are not baked into firmware.

## Settings Surface

Use the native HTTPS admin UI for both setup and ongoing configuration:

- fallback AP setup page for first-run admin password and Wi-Fi credentials;
- authenticated LAN admin UI after the device joins Wi-Fi;
- explicit HTTPS POST endpoints for settings changes;
- no plain HTTP credential transport.

## Current Native Settings

Network:

- Wi-Fi SSID/password provisioning.
- Network addressing: DHCP or manual/static station IP.
- Static network fields: IP address, subnet mask, gateway, DNS server 1, and DNS server 2.
- Saved-Wi-Fi clear action for recovery.

Time:

- NTP source: default pool, DHCP option 42, or up to three manual NTP servers.
- Timezone from a compact curated list.
- Daylight saving: `Auto`, `On`, or `Off`.

Display:

- Display enabled.
- Clock visibility.
- Quote/attribution visibility.
- Quote category toggles for time-specific quotes and classic/general quotes.
- Main-pane background and text colours.
- Quote time-phrase highlighting with selectable highlight and text colours, including apparent dithered colours.
- Sidebar visibility and colour.
- Top-bar visibility, background colour, text colour, and date format for locale-friendly date presentation.
- Bottom-bar visibility, background colour, and text colour.
- Watch-style clock face for quote-free, top-bar-free, bottom-bar-free clock display.
- Refresh interval: 1 through 1440 minutes; the 1440-minute setting changes the quote once per day.
- Display layout: landscape, landscape upside-down, portrait, and portrait upside-down.
- Clock display: 24-hour or AM/PM.
- Text margin.
- Force display refresh.
- Display telemetry for refresh count, total/stage durations, partial refresh state, and quote-pack status.

The native Display tab groups the main enable switch, clock/highlight/category controls, main-pane appearance, bar appearance, layout, and spacing separately. Display-tab controls save immediately when changed; the force-refresh action saves the current Display state before queueing a panel refresh. When the 24-hour clock is visible, minute ticks refresh only the changed clock glyph cells between scheduled main-pane refreshes; other clock modes fall back to the whole clock rectangle. When quotes are hidden but the clock remains visible, the renderer uses the largest generated clock font that fits the main pane for the current orientation and margin. If both quote categories are enabled, scheduled pane refreshes alternate between time-specific and classic quotes; highlight spans only apply to time-specific quotes.

Security:

- First-run admin password setup.
- Session login/logout in the web UI.
- Authenticated settings and OTA endpoints.
- HTTPS server on port 443.

## Required Next Settings

Content:

- Literary quote mode enabled.
- Hourly news digest enabled.
- BBC RSS feed URL or feed preset.
- Book covers enabled.
- Weather footer enabled.
- Calendar glance enabled.
- Daily literary almanac enabled.
- Cover gallery enabled.
- Quiet mode enabled.
- Quiet mode start/end.

Security and certificates:

- Admin password change.
- Stronger password storage.
- TLS certificate mode: baked-in self-signed certificate or user-provided certificate/key pair.
- TLS certificate/key upload.

Connectivity:

- Provisioning AP DHCP behavior that does not advertise a default gateway/DNS unless captive routing is explicitly needed.
- Local enrichment server URL.
- OTA policy/channel settings.

## Runtime Storage

Settings should be stored on the device and survive reboot. Native firmware currently uses NVS-backed config for admin auth, network, display, SNTP, timezone, DST, and NTP server choices. Display settings are committed as one NVS transaction so runtime rendering and saved config stay aligned across reboot.

Do not block quote display forever if NTP overrides are invalid. If no valid time is available, show the explicit waiting/offline fallback and keep retrying.

## Suggested Defaults

- NTP servers: default pool servers.
- Timezone: `Europe/London` for the current prototypes; configurable for shipped devices.
- Display: enabled, layout `1`, 1-minute refresh cadence, 16 px content margin, 24-hour time, clock hidden, quotes shown.
- Quote time highlighting: enabled with yellow highlight and black text.
- Main pane: white background with black text.
- Bars: red sidebar shown, black top bar with yellow date text, yellow bottom bar with black text.
- Time-specific quotes: enabled.
- Classic/general quotes: disabled by default.
- News digest: disabled by default.
- Covers: enabled when enrichment server is configured.
- Weather/calendar: disabled by default.
- Quiet mode: disabled by default.
- OTA/API: enabled for prototypes, review before broader distribution.
