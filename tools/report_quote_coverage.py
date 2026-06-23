from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Any

import yaml

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from tools.validate_quotes import validate_document


def minute_key(index: int) -> str:
    return f"{index // 60:02d}:{index % 60:02d}"


def load_quotes(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        data = yaml.safe_load(handle)
    validate_document(data, path)
    return data


def summarize(path: Path, *, sparse_threshold: int, show: int) -> int:
    data = load_quotes(path)
    counts = {minute: len(entries) for minute, entries in data["quotes"].items()}
    all_minutes = [minute_key(index) for index in range(24 * 60)]
    missing = [minute for minute in all_minutes if minute not in counts]
    sparse = [
        (minute, counts.get(minute, 0))
        for minute in all_minutes
        if 0 < counts.get(minute, 0) < sparse_threshold
    ]
    densest = sorted(counts.items(), key=lambda item: (-item[1], item[0]))[:show]

    total_quotes = sum(counts.values())
    covered = len(counts)

    print(f"{path}")
    print(f"  quotes: {total_quotes}")
    print(f"  covered minutes: {covered}/1440 ({covered / 1440:.1%})")
    print(f"  missing minutes: {len(missing)}")
    print(f"  sparse minutes (<{sparse_threshold} quotes): {len(sparse)}")
    print()

    if missing:
        print(f"First {min(show, len(missing))} missing minutes:")
        print("  " + ", ".join(missing[:show]))
        print()

    if sparse:
        print(f"First {min(show, len(sparse))} sparse minutes:")
        print("  " + ", ".join(f"{minute} ({count})" for minute, count in sparse[:show]))
        print()

    if densest:
        print(f"Top {len(densest)} densest minutes:")
        print("  " + ", ".join(f"{minute} ({count})" for minute, count in densest))

    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Report quote-library minute coverage.")
    parser.add_argument("path", type=Path, help="Quote-library YAML file")
    parser.add_argument(
        "--sparse-threshold",
        type=int,
        default=2,
        help="Count minutes below this quote count as sparse. Default: 2",
    )
    parser.add_argument(
        "--show",
        type=int,
        default=20,
        help="Number of examples to show per section. Default: 20",
    )
    args = parser.parse_args()

    try:
        return summarize(args.path, sparse_threshold=args.sparse_threshold, show=args.show)
    except Exception as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
