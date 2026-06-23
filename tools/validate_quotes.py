from __future__ import annotations

import argparse
import datetime as dt
import re
import sys
from pathlib import Path
from typing import Any

import yaml


MINUTE_RE = re.compile(r"^(?:[01]\d|2[0-3]):[0-5]\d$")
DATE_RE = re.compile(r"^\d{4}-\d{2}-\d{2}$")

TIME_REQUIRED_FIELDS = {
    "id",
    "text",
    "time_text",
    "title",
    "author",
    "source",
    "rights",
}

CLASSIC_REQUIRED_FIELDS = TIME_REQUIRED_FIELDS - {"time_text"}

OPTIONAL_FIELDS = {
    "added_at",
    "category",
    "cover_key",
    "license",
    "reviewed_at",
    "source_url",
    "tags",
    "translator",
    "weight",
    "year",
}

KNOWN_FIELDS = TIME_REQUIRED_FIELDS | OPTIONAL_FIELDS


class ValidationError(Exception):
    pass


def fail(message: str) -> None:
    raise ValidationError(message)


def expect_string(value: Any, path: str, *, allow_empty: bool = False) -> None:
    if not isinstance(value, str):
        fail(f"{path} must be a string")
    if not allow_empty and not value.strip():
        fail(f"{path} must not be empty")


def expect_date_or_null(value: Any, path: str) -> None:
    if value is None:
        return
    if isinstance(value, dt.date) and not isinstance(value, dt.datetime):
        return
    if isinstance(value, str) and DATE_RE.match(value):
        return
    fail(f"{path} must be null or YYYY-MM-DD")


def validate_common_quote_fields(quote: dict[str, Any], path: str, required_fields: set[str], seen_ids: set[str]) -> None:
    missing = required_fields - set(quote)
    if missing:
        fail(f"{path} is missing required fields: {', '.join(sorted(missing))}")

    unknown = set(quote) - KNOWN_FIELDS
    if unknown:
        fail(f"{path} has unknown fields: {', '.join(sorted(unknown))}")

    for field in required_fields:
        expect_string(quote[field], f"{path}.{field}")

    quote_id = quote["id"]
    if quote_id in seen_ids:
        fail(f"{path}.id duplicates quote id {quote_id!r}")
    seen_ids.add(quote_id)

    for field in ("category", "cover_key", "license", "source_url", "translator"):
        if field in quote and quote[field] is not None:
            expect_string(quote[field], f"{path}.{field}")

    for field in ("added_at", "reviewed_at"):
        if field in quote:
            expect_date_or_null(quote[field], f"{path}.{field}")

    if "tags" in quote:
        tags = quote["tags"]
        if not isinstance(tags, list):
            fail(f"{path}.tags must be a list")
        for index, tag in enumerate(tags):
            expect_string(tag, f"{path}.tags[{index}]")

    if "year" in quote and not isinstance(quote["year"], int):
        fail(f"{path}.year must be an integer")

    if "weight" in quote and not isinstance(quote["weight"], int | float):
        fail(f"{path}.weight must be a number")

    if quote["rights"] == "unknown":
        fail(f"{path}.rights must not be unknown for the baked-in library")


def validate_quote(quote: Any, path: str, minute: str, seen_ids: set[str]) -> None:
    if not isinstance(quote, dict):
        fail(f"{path} must be a mapping")

    validate_common_quote_fields(quote, path, TIME_REQUIRED_FIELDS, seen_ids)

    text = quote["text"]
    time_text = quote["time_text"]
    if time_text.lower() not in text.lower():
        fail(f"{path}.time_text must appear in text")

    if not MINUTE_RE.match(minute):
        fail(f"{path} has invalid minute key {minute!r}")


def validate_classic_quote(quote: Any, path: str, seen_ids: set[str]) -> None:
    if not isinstance(quote, dict):
        fail(f"{path} must be a mapping")

    validate_common_quote_fields(quote, path, CLASSIC_REQUIRED_FIELDS, seen_ids)
    if "time_text" in quote:
        expect_string(quote["time_text"], f"{path}.time_text", allow_empty=True)
        if quote["time_text"].strip():
            fail(f"{path}.time_text must be omitted or empty for classic quotes")


def validate_document(data: Any, source: Path) -> tuple[int, int]:
    if not isinstance(data, dict):
        fail(f"{source} must contain a top-level mapping")

    if data.get("version") != 1:
        fail(f"{source}.version must be 1")

    quotes = data.get("quotes", {})
    classic_quotes = data.get("classic_quotes", [])
    if not isinstance(quotes, dict):
        fail(f"{source}.quotes must be a mapping")
    if not isinstance(classic_quotes, list):
        fail(f"{source}.classic_quotes must be a list")
    if not quotes and not classic_quotes:
        fail(f"{source} must contain quotes or classic_quotes")

    seen_ids: set[str] = set()
    quote_count = 0

    for minute, entries in quotes.items():
        if not isinstance(minute, str):
            fail(f"{source}.quotes contains a non-string minute key")
        if not MINUTE_RE.match(minute):
            fail(f"{source}.quotes.{minute} is not an HH:MM minute key")
        if not isinstance(entries, list) or not entries:
            fail(f"{source}.quotes.{minute} must be a non-empty list")

        for index, quote in enumerate(entries):
            validate_quote(
                quote,
                f"{source}.quotes.{minute}[{index}]",
                minute,
                seen_ids,
            )
            quote_count += 1

    for index, quote in enumerate(classic_quotes):
        validate_classic_quote(quote, f"{source}.classic_quotes[{index}]", seen_ids)
        quote_count += 1

    return len(quotes), quote_count


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate quote-library YAML.")
    parser.add_argument(
        "paths",
        nargs="*",
        type=Path,
        default=[Path("data/quotes.sample.yaml")],
        help="YAML files to validate",
    )
    args = parser.parse_args()

    try:
        total_minutes = 0
        total_quotes = 0
        for path in args.paths:
            with path.open("r", encoding="utf-8") as handle:
                data = yaml.safe_load(handle)
            minutes, quotes = validate_document(data, path)
            total_minutes += minutes
            total_quotes += quotes
            print(f"{path}: OK ({minutes} minute keys, {quotes} quotes)")
        print(f"Validated {total_quotes} quotes across {total_minutes} minute keys.")
    except (OSError, yaml.YAMLError, ValidationError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
