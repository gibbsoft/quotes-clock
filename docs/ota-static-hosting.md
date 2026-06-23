# Static OTA Hosting

The clock should avoid mandatory, always-on fleet infrastructure. For proof-of-concept and small-batch deployments, use a dormant/static update channel:

- static site hosts update metadata;
- object storage hosts firmware binaries;
- devices poll occasionally with jitter;
- base clock behavior never depends on update infrastructure being online.

## Suggested Hosting

- Static site: Hugo-generated site on GitHub Pages, Cloudflare Pages, or similar.
- Firmware storage: Cloudflare R2 behind a stable HTTPS URL.
- DNS: stable custom domains for both manifest and firmware binaries.

Example layout:

```text
https://quotes-clock.example.com/firmware/manifest.json
https://firmware.quotes-clock.example.com/releases/0.1.2/quotes-clock.ota.bin
https://firmware.quotes-clock.example.com/releases/0.1.2/quotes-clock.md5
```

## Native ESP-IDF Path

For production firmware, use ESP-IDF OTA primitives:

- static manifest on the product website;
- firmware binary in R2/object storage;
- SHA256 and signed metadata;
- HTTPS download;
- rollback-capable OTA partitions;
- staged rollout channels such as `dev`, `beta`, and `stable`.

The native spike currently supports authenticated local OTA upload. Static polling/downloading is still future work.

Production manifests should include at least:

```json
{
  "product": "quotes-clock",
  "channel": "stable",
  "version": "1.2.3",
  "hardware": ["esp32-m075"],
  "firmware_url": "https://firmware.quotes-clock.example.com/releases/1.2.3/quotes-clock.bin",
  "sha256": "hex-encoded-sha256",
  "signature": "base64-signature",
  "min_version": "1.0.0",
  "release_notes_url": "https://quotes-clock.example.com/firmware/releases/1.2.3/"
}
```

## Device Behavior

- Check for updates weekly or monthly by default.
- Add random jitter before polling.
- Timeout quickly and fail silently.
- Continue normal quote display if DNS, HTTPS, manifest, or firmware download fails.
- Never require update infrastructure for base clock behavior.
- Verify firmware before installing.
- Roll back if the updated firmware does not mark itself healthy.

## Cost Goal

Most months should cost almost nothing:

- no always-on MQTT broker required;
- no per-device SaaS subscription required;
- no cloud instance required just to say "no update";
- update months only pay for object storage egress and static hosting traffic.
