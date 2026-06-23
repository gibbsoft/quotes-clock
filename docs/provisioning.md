# Wi-Fi Provisioning

Small-batch devices should not ship with a customer Wi-Fi network baked into firmware.

## Native MVP Path

The native ESP-IDF firmware owns provisioning directly:

- Device boots with no hard-coded station credentials.
- Fallback AP starts with the `quotes-clock-xxxx` naming convention.
- The e-paper setup screen shows the AP name and HTTPS setup URL.
- User connects to the device AP.
- User opens the HTTPS setup page and sets the first-run admin password.
- User submits Wi-Fi credentials through explicit HTTPS POST endpoints.
- Device stores credentials in NVS and joins the selected network.
- User can later change DHCP/static IP, DNS, NTP, display, and timezone settings from the authenticated admin UI.

Plain HTTP must not carry Wi-Fi credentials. Provisioning should stay on HTTPS endpoints even in fallback AP mode.

## Recovery

If saved Wi-Fi is bad, the native firmware should keep or re-enable the fallback AP and show clear recovery instructions on the display. The authenticated settings UI includes a saved-Wi-Fi clear action so a device can be recovered without reflashing.

Known setup lesson from Android testing: avoid generic WebSocket config plumbing for provisioning. Explicit HTTPS POST endpoints are simpler to reason about and worked more reliably.

## Production Notes

- Require a user-set admin password before exposing local device settings beyond basic setup.
- Run the local admin UI on HTTPS port 443 with a baked-in self-signed certificate by default.
- Allow authenticated users to upload their own TLS certificate and key for deployments that need trusted local HTTPS.
- Consider adding a physical provisioning/reset gesture if the PCB exposes a safe button.
- Avoid hard-coded user Wi-Fi networks in firmware builds intended for other people.
- Keep OTA enabled only if access control and network assumptions are acceptable for the deployment.
