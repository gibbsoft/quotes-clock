#!/usr/bin/env python3
"""Check that the public release tree does not contain known risky artifacts."""

from __future__ import annotations

import argparse
from pathlib import Path
import sys

import yaml


FORBIDDEN_FILES = [
    "firmware/oem/esp32_oem_backup.bin",
    "hardware/get-platformio.py",
    "tmp/Screenshot 2026-06-12 203638.png",
]
FORBIDDEN_GLOBS = [
    "docs/oem/*.pdf",
    "hardware/samples/GDEM075F52_Arduino/*",
]
FORBIDDEN_TEXT = [
    "Proprietary License",
    "remains proprietary",
    "proprietary software",
    "home.gibbsoft.com",
    "/home/nigel/.espressif",
]
CONFLICT_MARKER_PREFIXES = ("<<<<<<<", "=======", ">>>>>>>")
FORBIDDEN_QUOTE_TAGS = {
    "needs-review",
    "rights-review",
    "replace-with-literary",
    "nsfw",
}


def text_files(root: Path) -> list[Path]:
    result: list[Path] = []
    for path in root.rglob("*"):
        if not path.is_file():
            continue
        relative = path.relative_to(root)
        if path.name == "check_release_tree.py":
            continue
        if path.name == ".env" or path.name.startswith(".env.") or str(relative) == "eim_config.toml":
            continue
        skipped_parts = {".git", ".venv", ".codex", ".agents", "build", "tmp", "generated", "managed_components"}
        if any(part in skipped_parts for part in relative.parts):
            continue
        try:
            path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            continue
        result.append(path)
    return result


def check_forbidden_files(root: Path, errors: list[str]) -> None:
    for relative in FORBIDDEN_FILES:
        if (root / relative).exists():
            errors.append(f"forbidden file is present: {relative}")
    for pattern in FORBIDDEN_GLOBS:
        for path in root.glob(pattern):
            errors.append(f"forbidden file is present: {path.relative_to(root)}")


def check_forbidden_text(root: Path, errors: list[str]) -> None:
    for path in text_files(root):
        text = path.read_text(encoding="utf-8")
        for line_number, line in enumerate(text.splitlines(), start=1):
            if line.startswith(CONFLICT_MARKER_PREFIXES):
                errors.append(f"conflict marker in {path.relative_to(root)}:{line_number}")
        for needle in FORBIDDEN_TEXT:
            if needle in text:
                errors.append(f"forbidden text {needle!r} in {path.relative_to(root)}")


def quote_entries(data: object) -> list[dict]:
    if not isinstance(data, dict):
        return []
    entries: list[dict] = []
    quotes = data.get("quotes", {})
    if isinstance(quotes, dict):
        for rows in quotes.values():
            if isinstance(rows, list):
                entries.extend(row for row in rows if isinstance(row, dict))
    classic_quotes = data.get("classic_quotes", [])
    if isinstance(classic_quotes, list):
        entries.extend(row for row in classic_quotes if isinstance(row, dict))
    return entries


def check_quote_data(path: Path, errors: list[str]) -> None:
    if not path.exists():
        errors.append(f"quote data file does not exist: {path}")
        return
    data = yaml.safe_load(path.read_text(encoding="utf-8"))
    for entry in quote_entries(data):
        quote_id = entry.get("id", "<missing-id>")
        tags = set(entry.get("tags") or [])
        forbidden = tags & FORBIDDEN_QUOTE_TAGS
        if forbidden:
            errors.append(f"{path}: quote {quote_id} has forbidden release tags: {', '.join(sorted(forbidden))}")
        rights = str(entry.get("rights") or "")
        if "review" in rights:
            errors.append(f"{path}: quote {quote_id} has uncleared rights status: {rights}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--quote-data", type=Path, default=Path("data/quotes.sample.yaml"))
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[1]
    errors: list[str] = []
    check_forbidden_files(root, errors)
    check_forbidden_text(root, errors)
    check_quote_data((root / args.quote_data).resolve(), errors)
    if errors:
        for error in errors:
            print(f"release-check: {error}", file=sys.stderr)
        return 1
    print("Release tree checks passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
