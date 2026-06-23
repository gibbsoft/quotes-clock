# Contributing

Quotes Clock is an ESP32/e-paper firmware and tooling project. Contributions
are welcome once the repository is public, especially around hardware bring-up,
firmware reliability, documentation, and quote-library tooling.

## Development Setup

- Install Python 3.12 or newer.
- Install `uv`.
- Install ESP-IDF v6.0.x and make sure `idf.py` is available on `PATH`.
- Copy `.env.example` to `.env` for local-only build settings and TLS bootstrap
  material.

Common checks:

```sh
uv run python tools/validate_quotes.py data/quotes.sample.yaml
uv run python tools/report_quote_coverage.py data/quotes.sample.yaml
make release-check
```

Native firmware build:

```sh
make native-idf-rescue-bin NATIVE_IDF_TLS_ARGS=--allow-insecure-fallback
make check-native-idf-size
```

## Quote Data

Do not commit generated/imported quote corpora unless the entries have been
reviewed for redistribution. Keep generated staging files local and use the
sample quote library for CI/build validation.

## Pull Requests

- Keep changes focused.
- Include the verification commands you ran.
- Update documentation and `CHANGELOG.md` for user-visible firmware, tooling, or
  workflow changes.
- Do not include device secrets, private TLS keys, local firmware backups, OEM
  PDFs, or generated quote corpora.
