# Quote Library

The quote library is the offline core of the clock. It should be curated as YAML, then compiled into generated C++ arrays for the first firmware build.

## File Shape

Use a minute-keyed YAML file for time-specific quotes:

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
      cover_key: "openlibrary:OL123W"
      added_at: "2026-05-14"
      reviewed_at: null
      tags:
        - public-domain
```

The minute key must be quoted so YAML does not interpret it as another type.

General/classic quotes live in the optional `classic_quotes` list in the same
schema:

```yaml
version: 1
classic_quotes:
  - id: unique-classic-id
    text: "General quote text without a required time phrase."
    title: "Classic Quote"
    author: "Author name"
    source: "Dataset or URL"
    source_url: "https://example.com/source"
    license: "CC0-1.0"
    rights: "source-license-unverified"
    added_at: "2026-06-02"
    reviewed_at: null
    tags:
      - classic
      - needs-review
```

The public build default is `data/quotes.sample.yaml`. Larger generated
libraries such as `data/quotes.native.yaml` are local staging artifacts and are
ignored by Git until their contents are cleared for redistribution.

## Required Fields

- `id`: stable unique identifier for the quote.
- `text`: display text.
- `time_text`: the phrase in the quote that maps to the minute. Required for
  minute-keyed time-specific quotes and omitted for classic quotes.
- `title`: source work title.
- `author`: source author.
- `source`: dataset name, URL, or citation.
- `rights`: rights status, preferably `public-domain` for the base library.

## Optional Fields

- `cover_key`: stable key for cover lookup, such as an Open Library work ID or local cover ID.
- `license`: source license identifier, where known.
- `source_url`: direct URL for the imported source, where known.
- `added_at`: date the quote was added to the curated library.
- `reviewed_at`: date the quote was manually reviewed, or `null` if it still needs review.
- `translator`: translator name, where relevant.
- `year`: publication year, where known.
- `tags`: curation tags such as `short`, `long`, `poetry`, `night`, or `needs-review`.
- `weight`: selection weight when multiple quotes exist for the same minute.

## Curation Rules

- Prefer public-domain sources for the baked-in library.
- Keep one to five strong candidates per minute for the MVP.
- Preserve the time phrase in the displayed text.
- Avoid quotes that only work with long missing context.
- Keep rights metadata visible even if the source dataset already includes licensing notes.
- Use `needs-review` for mechanically harvested entries that have not been manually checked.

## Validation

Validate quote-library YAML with:

```powershell
uv run python tools/validate_quotes.py data/quotes.sample.yaml
```

The validator checks minute keys, required fields, duplicate quote IDs, date fields, tags, and whether `time_text` appears in the quote text.

Report minute coverage with:

```powershell
uv run python tools/report_quote_coverage.py data/quotes.literature-clock.yaml
```

Export missing minutes with:

```powershell
uv run python tools/export_missing_minutes.py data/quotes.literature-clock.yaml --output build/missing_minutes.csv
```

Export sparse minutes below a larger target, such as three quotes per minute, with:

```powershell
uv run python tools/export_missing_minutes.py data/quotes.literature-clock.yaml --target-count 3 --output build/sparse_minutes.csv
```

Import the first staging dataset with:

```powershell
uv run python tools/import_literature_clock.py
```

See [Datasets](datasets.md) for source and rights notes.

Import the general/classic quote staging dataset with:

```powershell
uv run python tools/import_quotables.py --limit 3500 --output data/quotes.classics.yaml
```

Merge imported staging sources with:

```powershell
uv run python tools/merge_quote_libraries.py data/quotes.literature-clock.yaml data/quotes.johannesne-literature-clock.yaml --output data/quotes.staging.yaml
```

Temporary project-original gap fillers may be merged for prototype completeness:

```powershell
uv run python tools/merge_quote_libraries.py data/quotes.staging.yaml data/quotes.gap-fillers.yaml --output data/quotes.full-staging.yaml
```

Keep these tagged as `gap-filler` and replace them with reviewed literary passages over time.

Find remaining temporary gap fillers with:

```powershell
Select-String -Path data/quotes*.yaml -Pattern "gap-filler"
```

## Firmware Packaging

The YAML is the source of truth, not the on-device format. Do not parse YAML on the ESP32.

For the first build, compile it into generated C++ arrays:

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

This keeps the firmware simple: no SQLite, no YAML parser, no runtime query engine, and no filesystem requirement for the base clock.

For the native firmware workflow, generate the native font header and quote-data partition image:

```powershell
.\build.ps1 native-idf-assets
```

By default this reads `data/quotes.sample.yaml` and writes generated files under `firmware/native-idf/main/generated/`. Those outputs are ignored by Git:

- `quotes_clock_assets.hpp`: bitmap fonts and render assets.
- `quote_data.bin`: versioned quote-data partition image containing one
  selected time-specific quote per minute plus the normalized classic quote
  pool.

The native firmware wraps and measures text on the display itself, then chooses the largest quote font that fits the available area. This means quote curation does not need to pre-wrap lines for the current screen layout.

The native quote-data generator also records the byte offset and length of each quote's `time_text` phrase inside the compacted display text. The renderer uses that metadata to highlight the actual time phrase at refresh time without searching the quote text on the ESP32.

Classic quotes have no highlight span. When both quote categories are enabled
in the UI, the renderer alternates pane refreshes between time-specific and
classic quotes. Minute-only clock updates refresh the clock rectangle without
changing the visible quote pane.

Flash only the quote-data partition after quote edits:

```powershell
.\build.ps1 flash-native-idf-quote-data -SerialPort COM6
```

Curation metadata such as `added_at`, `reviewed_at`, `source`, `rights`, `tags`, and `cover_key` can be stripped or ignored by the firmware compiler unless a specific runtime feature needs it.

Later build artifacts can include:

- a compressed asset stored in flash;
- a packed LittleFS/SPIFFS file with a minute index;
- an SD card file if the final hardware exposes removable storage.

The runtime lookup should work without Wi-Fi or server access. For each minute, the device should pick a quote from local data and render a quote-only display even when enrichment assets are unavailable.
