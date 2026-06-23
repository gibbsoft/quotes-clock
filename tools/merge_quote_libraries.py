from __future__ import annotations

import argparse
import sys
from collections import defaultdict
from pathlib import Path
from typing import Any

import yaml

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from tools.import_literature_clock import QuoteLibraryDumper
from tools.validate_quotes import validate_document


def normalize_signature(value: Any) -> str:
    return " ".join(str(value).lower().split())


def quote_signature(minute: str, quote: dict[str, Any]) -> tuple[str, str, str, str]:
    return (
        minute,
        normalize_signature(quote["title"]),
        normalize_signature(quote["author"]),
        normalize_signature(quote["text"]),
    )


def classic_quote_signature(quote: dict[str, Any]) -> tuple[str, str, str]:
    return (
        normalize_signature(quote["author"]),
        normalize_signature(quote["title"]),
        normalize_signature(quote["text"]),
    )


def load_library(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        data = yaml.safe_load(handle)
    validate_document(data, path)
    return data


def merge_libraries(paths: list[Path]) -> dict[str, Any]:
    grouped: dict[str, list[dict[str, Any]]] = defaultdict(list)
    seen_signatures: set[tuple[str, str, str, str]] = set()
    classic_quotes: list[dict[str, Any]] = []
    seen_classic_signatures: set[tuple[str, str, str]] = set()
    seen_ids: set[str] = set()

    for path in paths:
        data = load_library(path)
        for minute, entries in data.get("quotes", {}).items():
            for quote in entries:
                signature = quote_signature(minute, quote)
                if signature in seen_signatures:
                    continue
                seen_signatures.add(signature)

                quote = dict(quote)
                original_id = quote["id"]
                if original_id in seen_ids:
                    quote["id"] = f"{original_id}-{len(seen_ids) + 1}"
                seen_ids.add(quote["id"])

                grouped[minute].append(quote)
        for quote in data.get("classic_quotes", []):
            signature = classic_quote_signature(quote)
            if signature in seen_classic_signatures:
                continue
            seen_classic_signatures.add(signature)

            quote = dict(quote)
            original_id = quote["id"]
            if original_id in seen_ids:
                quote["id"] = f"{original_id}-{len(seen_ids) + 1}"
            seen_ids.add(quote["id"])
            classic_quotes.append(quote)

    merged: dict[str, Any] = {
        "version": 1,
        "description": (
            "Merged staging quote library. Review rights and quality before using "
            "entries in baked-in firmware."
        ),
        "quotes": {
            minute: sorted(entries, key=lambda quote: quote["id"])
            for minute, entries in sorted(grouped.items())
        },
    }
    if classic_quotes:
        merged["classic_quotes"] = sorted(classic_quotes, key=lambda quote: quote["id"])
    return merged


def main() -> int:
    parser = argparse.ArgumentParser(description="Merge quote-library YAML files.")
    parser.add_argument("inputs", nargs="+", type=Path, help="Input YAML files")
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("data/quotes.staging.yaml"),
        help="Merged output path. Default: data/quotes.staging.yaml",
    )
    args = parser.parse_args()

    try:
        data = merge_libraries(args.inputs)
        validate_document(data, args.output)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(
            yaml.dump(
                data,
                allow_unicode=False,
                Dumper=QuoteLibraryDumper,
                default_flow_style=False,
                sort_keys=False,
                width=100,
            ),
            encoding="utf-8",
        )
        timed_quote_count = sum(len(entries) for entries in data.get("quotes", {}).values())
        classic_quote_count = len(data.get("classic_quotes", []))
        print(
            f"Wrote {args.output} ({len(data.get('quotes', {}))} minute keys, "
            f"{timed_quote_count} timed quotes, {classic_quote_count} classic quotes)"
        )
    except Exception as error:
        print(f"error: {error}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
