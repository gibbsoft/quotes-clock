# Timekeeping

The quote clock should not require network access for its normal display loop once it knows the time.

## Policy

- Use SNTP/NTP when Wi-Fi is available.
- After a successful sync, keep time locally using the ESP32 runtime clock.
- If Wi-Fi drops after sync, continue displaying quotes from local time.
- If the device cold-boots without valid time, show a fallback state and keep retrying Wi-Fi/NTP.
- Do not block the device forever on network time.

## Cold Boot Without Network

If the ESP32 boots and no valid time source is available:

- Show a simple "waiting for time" or offline fallback screen.
- Optionally show a random local quote or a fixed safe quote while waiting.
- Keep retrying Wi-Fi and SNTP in the background.
- If unprovisioned, expose the fallback AP/captive portal so the user can add Wi-Fi credentials.
- Avoid pretending the displayed quote maps to the real current minute.
- If a safe physical button is found, consider a manual time-set mode or debug minute-advance mode.

## After First Sync

Once SNTP succeeds:

- Set a `time_valid` state.
- Display the quote for the current `HH:MM`.
- Continue minute updates from local time even if Wi-Fi disconnects.
- Refresh SNTP opportunistically when Wi-Fi is available.

## RTC Upgrade

For stronger offline behavior across power loss, add a hardware RTC such as a DS3231.

With an RTC:

- SNTP updates the RTC when online.
- On boot, the ESP32 reads the RTC before attempting Wi-Fi.
- If Wi-Fi is unavailable, the clock can still display the correct minute after power loss.

This is not required for the first MVP, but it is the clean upgrade if the clock needs reliable offline operation after unplugging or battery depletion.

## Native Firmware

The native firmware checks whether time is valid before choosing a quote. If time is invalid, it renders the offline/waiting fallback instead of selecting a minute quote.

Runtime NTP source settings are available in the HTTPS admin UI. Native firmware supports the default pool, up to three manual server names, and DHCP-supplied NTP servers via RFC 2132 option 42 when the station interface is using DHCP.
