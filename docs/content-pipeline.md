# Content Pipeline

The ESP32 should be an offline-first quote clock. Quote selection must work from a baked-in or locally stored quote library so the core clock remains usable without Wi-Fi, cloud services, or a local server.

Optional enrichment should happen on a server, Raspberry Pi, Home Assistant host, or development machine: cover fetching, RSS ingestion, summarization, richer layout, dithering, and framebuffer packing.

## Responsibilities

ESP32:

- Connect to Wi-Fi.
- Select the quote for the current minute from local data.
- Render a quote-only screen locally, or display a locally cached pre-rendered quote screen if that is the chosen MVP format.
- Receive or fetch prepared enrichment assets when a server is available.
- Cache the next enriched assets.
- Refresh the GDEM075F52 panel on schedule.
- Avoid overlapping refreshes.
- Show baked-in/local quote screens when offline.
- Use local time after the first successful sync; show a fallback screen if no valid time is available.

The board is expected to use an ESP32-WROOM-32D module with 520 KB SRAM, 4 MB flash, and no PSRAM unless hardware inspection proves otherwise.

Server:

- Fetch and cache book covers.
- Generate fallback title/author cards.
- Ingest RSS feeds.
- Summarize news with an optional LLM.
- Render final 800x480 layouts.
- Dither and reduce images to black, white, red, and yellow.
- Pack the framebuffer into the panel's 2-bit format.

## Quote Assets

The canonical quote library should be YAML so it stays comfortable to curate by hand and review in diffs. Build tooling can later compile this YAML into a compact firmware representation. See [Quote Library](quote-library.md) for the proposed shape.

Quotes should be grouped by minute:

```yaml
version: 1
quotes:
  "22:15":
    - id: unique-quote-id
      text: "Quote text containing or implying the time."
      time_text: "quarter past ten"
      title: "Book title"
      author: "Author name"
      source: "Dataset or URL"
      rights: "public-domain"
      cover_key: "open-library-or-local-key"
      added_at: "2026-05-14"
      reviewed_at: null
```

For the MVP, prefer public-domain-first literary datasets and keep rights metadata visible in the source data. The quote library should be compiled into generated C++ arrays for the first firmware build so it is always available without a filesystem or parser.

Validate quote files before generating firmware assets:

```powershell
uv run python tools/validate_quotes.py data/quotes.sample.yaml
```

## Rendered Images

The base quote renderer may run on the ESP32, especially for quote-only screens. Server-side rendering is still useful for enriched layouts with covers, thumbnails, weather, or other imagery.

The server-side renderer should output one or more of these formats:

- PNG preview for debugging.
- Four-color indexed image for verification.
- Packed GDEM075F52 framebuffer for direct display.

The packed framebuffer is:

- 800x480 pixels.
- 2 bits per pixel.
- 4 pixels per byte.
- 96,000 bytes per full image.

On ESP32-WROOM-32D, keep only one full display framebuffer in RAM where possible.

## Four-Color Dithering Experiments

The GDEM075F52 panel has four physical colors: black, white, red, and yellow. Some comparable four-color e-paper products advertise a larger apparent palette by spatially dithering those base colors at high DPI. Our 7.5 inch 800x480 panel is about 124 DPI, so the effect will be more visibly patterned than on a 140 DPI panel, but it is still worth testing for static or rarely changing design accents.

Candidate apparent colors:

- Grey: black and white.
- Light red: red and white.
- Dark red: red and black.
- Light yellow: yellow and white.
- Dark yellow: yellow and black.
- Orange: red and yellow.

Use these blends for large highlights, category ribbons, cover/poster-style artwork, or header/footer accents. Avoid them for small text, where dithering will reduce edge crispness and make the text look noisy.

## Device Quote Format

Do not parse YAML on the ESP32. YAML is the human-curated source format only.

For the first firmware build, generate compact C++ arrays from the YAML:

```powershell
uv run python tools/generate_quotes_cpp.py data/quotes.sample.yaml build/quotes_generated.h
```

```cpp
struct Quote {
  uint16_t minute;
  const char *id;
  const char *text;
  const char *time_text;
  const char *title;
  const char *author;
};
```

This keeps runtime lookup simple and avoids carrying a YAML parser or SQLite database on the device.

Curation-only fields such as `added_at` and `reviewed_at` should be ignored or stripped when generating the first C++ firmware arrays.

If the library grows too large for comfortable firmware builds, switch the generated artifact to a packed LittleFS/SPIFFS file with a small minute index.

## Book Covers

Cover fetching should be opportunistic.

- Use a cover when it is already cached or easy to fetch.
- Resize, crop/pad, dither, and color-reduce server-side.
- Cache processed covers by stable key.
- Render a generated title/author card when no cover is available.

The display should never wait indefinitely for a cover.

## Hourly News Digest

On the hour, the server may generate a digest image that replaces the quote for about one minute.

Inputs:

- BBC RSS feeds.
- Thumbnail images from RSS items where available.
- Optional LLM summary.

Fallbacks:

- If RSS is unavailable, skip the digest or show the most recent cached digest with a stale marker.
- If a thumbnail is unavailable, render a text-only digest.
- If the LLM is unavailable, render raw headlines.
- If Wi-Fi is unavailable, keep showing local baked-in quotes.

## Scheduling

Suggested cadence:

- Quote image: every minute.
- Cover prefetch: 5 to 15 minutes ahead.
- News digest: on the hour for about one minute.
- Weather footer: refresh every 15 to 30 minutes.
- Literary almanac: refresh daily.

The ESP32 should treat the display as busy during a refresh and delay any new update until the panel is ready.
