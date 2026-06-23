# Datasets

This project can import existing literary-clock datasets, but generated/imported data is local staging material until rights and quality are reviewed. The public tree intentionally keeps only the small sample library and import tools.

## alvations/Quotables

The classics/general quote importer targets the CC0-claimed Quotables corpus:

- Repository: https://github.com/alvations/Quotables
- Dataset: https://raw.githubusercontent.com/alvations/Quotables/master/author-quote.txt
- Repository license: CC0-1.0

Import with:

```powershell
uv run python tools/import_quotables.py --limit 3500 --output data/quotes.classics.yaml
```

The importer normalizes text, filters display-unsafe characters, removes duplicate
author/text pairs, hash-sorts the candidates for deterministic spread across the
source corpus, and writes `classic_quotes` entries tagged:

- `imported`
- `classic`
- `quotables`
- `needs-review`
- `rights-review`

The source repository declares CC0-1.0, but individual quote provenance can still
be messy. Treat imported rows as prototype/staging material until reviewed.

## literature-clock

The first importer targets the `literature-clock` npm package:

- Dataset CDN: https://cdn.jsdelivr.net/npm/literature-clock/quotes.csv
- Package files: https://cdn.jsdelivr.net/npm/literature-clock/
- Package license: MIT

Import with:

```powershell
uv run python tools/import_literature_clock.py
```

This writes a generated local file ignored by Git:

```text
data/quotes.literature-clock.yaml
```

Imported entries are tagged:

- `imported`
- `literature-clock`
- `needs-review`
- `rights-review`

The package is MIT-licensed, but the quoted book passages may include modern copyrighted works. Do not treat imported entries as automatically suitable for baked-in firmware. Review each entry before promoting it into the production quote library.

After importing, inspect coverage with:

```powershell
uv run python tools/report_quote_coverage.py data/quotes.literature-clock.yaml
```

Export a target list for the next harvest with:

```powershell
uv run python tools/export_missing_minutes.py data/quotes.literature-clock.yaml --target-count 1 --output build/missing_minutes.csv
```

## JohannesNE/literature-clock

The second importer targets the annotated CSV from Johannes Enevoldsen's literature-clock project:

- Dataset: https://raw.githubusercontent.com/JohannesNE/literature-clock/master/litclock_annotated.csv
- Repository: https://github.com/JohannesNE/literature-clock

Import with:

```powershell
uv run python tools/import_johannesne_literature_clock.py
```

This writes a generated local file ignored by Git:

```text
data/quotes.johannesne-literature-clock.yaml
```

Imported entries are tagged:

- `imported`
- `johannesne-literature-clock`
- `needs-review`
- `rights-review`
- the source safety marker, such as `sfw` or `nsfw`

This source is useful for farming coverage, but imported entries remain staging data until rights and quality are reviewed.

## Merged Staging Library

Merge imported datasets into one staging file with:

```powershell
uv run python tools/merge_quote_libraries.py data/quotes.literature-clock.yaml data/quotes.johannesne-literature-clock.yaml --output data/quotes.staging.yaml
```

The merge tool deduplicates exact quote matches by minute, title, author, and text. The output is ignored by Git and remains staging data; review and promote selected entries into a separately cleared release library later.

## Gap Fillers

Temporary project-original fillers live in:

```text
data/quotes.gap-fillers.yaml
```

They cover minutes still missing from imported sources so firmware prototypes can display something for every minute. They are tagged `gap-filler` and `replace-with-literary`; do not use them as release data.

Merge them with staging data using:

```powershell
uv run python tools/merge_quote_libraries.py data/quotes.staging.yaml data/quotes.gap-fillers.yaml --output data/quotes.full-staging.yaml
```
