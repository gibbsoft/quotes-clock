from __future__ import annotations

import argparse
import csv
import datetime as dt
import re
import sys
import urllib.request
from collections import defaultdict
from pathlib import Path
from typing import Any

import yaml

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from tools.validate_quotes import MINUTE_RE, validate_document


DEFAULT_URL = "https://cdn.jsdelivr.net/npm/literature-clock/quotes.csv"
DEFAULT_SOURCE = "literature-clock npm package via jsDelivr"


class QuoteLibraryDumper(yaml.SafeDumper):
    pass


def represent_string(dumper: yaml.SafeDumper, value: str) -> yaml.nodes.ScalarNode:
    style = '"' if MINUTE_RE.match(value) else None
    return dumper.represent_scalar("tag:yaml.org,2002:str", value, style=style)


QuoteLibraryDumper.add_representer(str, represent_string)


def slug_part(value: str) -> str:
    value = value.lower()
    value = re.sub(r"[^a-z0-9]+", "-", value)
    return value.strip("-") or "quote"


def normalize_text(value: str) -> str:
    return re.sub(r"\s+", " ", value).strip()


def quote_id(minute: str, title: str, author: str, index: int) -> str:
    return "-".join(
        [
            "litclock",
            minute.replace(":", ""),
            slug_part(author)[:32],
            slug_part(title)[:32],
            f"{index:03d}",
        ]
    )


def fetch_csv(url: str) -> str:
    with urllib.request.urlopen(url, timeout=30) as response:
        return response.read().decode("utf-8-sig")


def import_rows(csv_text: str, *, added_at: str, source_url: str) -> dict[str, Any]:
    grouped: dict[str, list[dict[str, Any]]] = defaultdict(list)
    reader = csv.reader(csv_text.splitlines(), delimiter="|")

    for row_number, row in enumerate(reader, start=1):
        if not row or all(not cell.strip() for cell in row):
            continue
        if len(row) != 5:
            raise ValueError(f"row {row_number} has {len(row)} columns, expected 5")

        minute, time_text, text, title, author = [normalize_text(cell) for cell in row]
        index = len(grouped[minute]) + 1
        grouped[minute].append(
            {
                "id": quote_id(minute, title, author, index),
                "text": text,
                "time_text": time_text,
                "title": title,
                "author": author,
                "source": f"{DEFAULT_SOURCE}: {source_url}",
                "rights": "dataset-mit-text-rights-unverified",
                "cover_key": None,
                "added_at": added_at,
                "reviewed_at": None,
                "tags": [
                    "imported",
                    "literature-clock",
                    "needs-review",
                    "rights-review",
                ],
            }
        )

    return {
        "version": 1,
        "description": (
            "Imported staging copy of literature-clock quotes. "
            "Review rights and quality before using entries in baked-in firmware."
        ),
        "quotes": dict(sorted(grouped.items())),
    }


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Import the literature-clock CSV dataset into quote-library YAML."
    )
    parser.add_argument(
        "--url",
        default=DEFAULT_URL,
        help=f"CSV source URL. Default: {DEFAULT_URL}",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("data/quotes.literature-clock.yaml"),
        help="Output YAML path.",
    )
    parser.add_argument(
        "--added-at",
        default=dt.date.today().isoformat(),
        help="Date to write into added_at fields.",
    )
    args = parser.parse_args()

    try:
        csv_text = fetch_csv(args.url)
        data = import_rows(csv_text, added_at=args.added_at, source_url=args.url)
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
        quote_count = sum(len(entries) for entries in data["quotes"].values())
        print(f"Wrote {args.output} ({len(data['quotes'])} minute keys, {quote_count} quotes)")
    except Exception as error:
        print(f"error: {error}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
