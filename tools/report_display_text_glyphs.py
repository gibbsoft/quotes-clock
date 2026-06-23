from __future__ import annotations

import argparse
import sys
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any

import yaml

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from tools.display_text import describe_char, is_display_safe_char, normalize_display_text
from tools.generate_quotes_cpp import one_quote_per_minute, quote_rows
from tools.validate_quotes import ValidationError, validate_document


DISPLAY_FIELDS = ("text", "time_text", "title", "author")


def ascii_snippet(value: str, *, width: int = 96) -> str:
    compact = " ".join(value.split())
    if len(compact) > width:
        compact = compact[: width - 3].rstrip() + "..."
    return ascii(compact)


def report_display_text(path: Path, *, all_quotes: bool, limit: int) -> int:
    with path.open("r", encoding="utf-8") as handle:
        data = yaml.safe_load(handle)

    validate_document(data, path)
    rows = quote_rows(data)
    if not all_quotes:
        rows = one_quote_per_minute(rows)
    display_rows: list[tuple[str, dict[str, Any]]] = []
    for minute_index, quote in rows:
        hour, minute = divmod(minute_index, 60)
        display_rows.append((f"{hour:02d}:{minute:02d}", quote))
    for index, quote in enumerate(data.get("classic_quotes", []), start=1):
        display_rows.append((f"classic:{index:04d}", quote))

    normalized_fields = 0
    unsupported: Counter[str] = Counter()
    examples: dict[str, list[str]] = defaultdict(list)
    normalized_examples: list[str] = []

    for minute_label, quote in display_rows:
        quote_id = quote["id"]
        for field in DISPLAY_FIELDS:
            if field not in quote:
                continue
            raw = str(quote[field])
            normalized = normalize_display_text(raw)
            if normalized != raw:
                normalized_fields += 1
                if len(normalized_examples) < limit:
                    normalized_examples.append(
                        f"{minute_label} {quote_id}.{field}: {ascii_snippet(raw)} -> {ascii_snippet(normalized)}"
                    )
            for char in normalized:
                if is_display_safe_char(char):
                    continue
                unsupported[char] += 1
                if len(examples[char]) < 3:
                    examples[char].append(f"{minute_label} {quote_id}.{field}: {ascii_snippet(normalized)}")

    scope = "all quotes" if all_quotes else "one selected quote per minute"
    print(f"{path}: checked display text for {len(display_rows)} rows ({scope})")
    print(f"  NFKC-normalized fields: {normalized_fields}")
    for example in normalized_examples:
        print(f"    normalized {example}")

    if not unsupported:
        print("  unsupported display glyphs after normalization: none")
        return 0

    print(f"  unsupported display glyphs after normalization: {len(unsupported)}")
    for char, count in unsupported.most_common(limit):
        print(f"    {describe_char(char)} {ascii(char)}: {count} occurrence(s)")
        for example in examples[char]:
            print(f"      {example}")
    if len(unsupported) > limit:
        print(f"    ... {len(unsupported) - limit} more glyph(s)")
    return len(unsupported)


def main() -> int:
    parser = argparse.ArgumentParser(description="Report quote characters outside the display-safe glyph policy.")
    parser.add_argument("path", type=Path, help="Quote-library YAML file")
    parser.add_argument(
        "--all-quotes",
        action="store_true",
        help="Inspect every quote instead of only the generated one-quote-per-minute set.",
    )
    parser.add_argument(
        "--fail-on-unsupported",
        action="store_true",
        help="Return a non-zero exit code when unsupported glyphs remain after normalization.",
    )
    parser.add_argument("--limit", type=int, default=20, help="Maximum examples to print. Default: 20")
    args = parser.parse_args()

    try:
        unsupported_count = report_display_text(args.path, all_quotes=args.all_quotes, limit=args.limit)
    except (OSError, yaml.YAMLError, ValidationError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1

    return 1 if args.fail_on_unsupported and unsupported_count else 0


if __name__ == "__main__":
    raise SystemExit(main())
