from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import sys
import urllib.request
from pathlib import Path
from typing import Any

import yaml

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from tools.display_text import compact_display_text, is_display_safe_char, normalize_display_text
from tools.import_literature_clock import QuoteLibraryDumper, slug_part
from tools.validate_quotes import validate_document


DEFAULT_URL = "https://raw.githubusercontent.com/alvations/Quotables/master/author-quote.txt"
DEFAULT_SOURCE = "alvations/Quotables author-quote.txt"
DEFAULT_LIMIT = 1440


def fetch_text(url: str) -> str:
    with urllib.request.urlopen(url, timeout=60) as response:
        return response.read().decode("utf-8-sig")


def clean_field(value: str) -> str:
    value = compact_display_text(value)
    value = value.replace("''", '"')
    value = value.replace("``", '"')
    return value.strip(" \t\r\n\"")


def display_safe(value: str) -> bool:
    return all(is_display_safe_char(char) for char in normalize_display_text(value))


def quote_id(author: str, text: str, index: int) -> str:
    digest = hashlib.sha1(f"{author}\0{text}".encode("utf-8")).hexdigest()[:10]
    return f"quotables-{index:04d}-{slug_part(author)[:28]}-{digest}"


def candidate_sort_key(row: tuple[str, str]) -> str:
    author, text = row
    return hashlib.sha256(f"{author}\0{text}".encode("utf-8")).hexdigest()


def parse_rows(raw_text: str, *, min_chars: int, max_chars: int) -> list[tuple[str, str]]:
    rows: list[tuple[str, str]] = []
    seen: set[tuple[str, str]] = set()
    for line_number, line in enumerate(raw_text.splitlines(), start=1):
        if not line.strip():
            continue
        parts = line.split("\t")
        if len(parts) != 2:
            print(f"warning: skipped quotables row {line_number}: expected author and quote", file=sys.stderr)
            continue
        author = clean_field(parts[0])
        text = clean_field(parts[1])
        if not author or not text:
            continue
        if len(author) > 64 or len(text) < min_chars or len(text) > max_chars:
            continue
        if not display_safe(author) or not display_safe(text):
            continue
        signature = (" ".join(author.lower().split()), " ".join(text.lower().split()))
        if signature in seen:
            continue
        seen.add(signature)
        rows.append((author, text))
    rows.sort(key=candidate_sort_key)
    return rows


def import_rows(
    raw_text: str,
    *,
    added_at: str,
    limit: int,
    max_chars: int,
    min_chars: int,
    source_url: str,
) -> dict[str, Any]:
    candidates = parse_rows(raw_text, min_chars=min_chars, max_chars=max_chars)
    selected = candidates[:limit]
    classic_quotes: list[dict[str, Any]] = []
    for index, (author, text) in enumerate(selected, start=1):
        classic_quotes.append(
            {
                "id": quote_id(author, text, index),
                "text": text,
                "title": "Classic Quote",
                "author": author,
                "source": f"{DEFAULT_SOURCE}: {source_url}",
                "source_url": source_url,
                "license": "CC0-1.0",
                "rights": "quotables-cc0-source-unverified",
                "cover_key": None,
                "added_at": added_at,
                "reviewed_at": None,
                "tags": [
                    "imported",
                    "classic",
                    "quotables",
                    "needs-review",
                    "rights-review",
                ],
            }
        )

    return {
        "version": 1,
        "description": (
            "Normalized classics/general quote staging library imported from alvations/Quotables. "
            "The source repository declares CC0-1.0; review provenance and quality before production use."
        ),
        "classic_quotes": classic_quotes,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Import alvations/Quotables into normalized classics YAML.")
    parser.add_argument("--url", default=DEFAULT_URL, help=f"Source URL. Default: {DEFAULT_URL}")
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("data/quotes.classics.yaml"),
        help="Output YAML path.",
    )
    parser.add_argument("--limit", type=int, default=DEFAULT_LIMIT, help=f"Maximum quotes. Default: {DEFAULT_LIMIT}")
    parser.add_argument("--min-chars", type=int, default=45, help="Minimum quote length. Default: 45")
    parser.add_argument("--max-chars", type=int, default=220, help="Maximum quote length. Default: 220")
    parser.add_argument(
        "--added-at",
        default=dt.date.today().isoformat(),
        help="Date to write into added_at fields.",
    )
    args = parser.parse_args()

    try:
        raw_text = fetch_text(args.url)
        data = import_rows(
            raw_text,
            added_at=args.added_at,
            limit=args.limit,
            max_chars=args.max_chars,
            min_chars=args.min_chars,
            source_url=args.url,
        )
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
            newline="\n",
        )
        print(f"Wrote {args.output} ({len(data['classic_quotes'])} classic quotes)")
    except Exception as error:
        print(f"error: {error}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
