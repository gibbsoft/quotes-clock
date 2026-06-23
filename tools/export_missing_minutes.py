from __future__ import annotations

import argparse
import csv
import sys
from pathlib import Path
from typing import Any

import yaml

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from tools.validate_quotes import validate_document


def minute_key(index: int) -> str:
    return f"{index // 60:02d}:{index % 60:02d}"


def load_counts(path: Path) -> dict[str, int]:
    with path.open("r", encoding="utf-8") as handle:
        data: Any = yaml.safe_load(handle)
    validate_document(data, path)
    return {minute: len(entries) for minute, entries in data["quotes"].items()}


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Export missing or sparse quote-library minutes."
    )
    parser.add_argument("input", type=Path, help="Quote-library YAML file")
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("build/missing_minutes.csv"),
        help="CSV output path. Default: build/missing_minutes.csv",
    )
    parser.add_argument(
        "--target-count",
        type=int,
        default=1,
        help="Export minutes with fewer than this many quotes. Default: 1",
    )
    args = parser.parse_args()

    try:
        counts = load_counts(args.input)
        rows = []
        for index in range(24 * 60):
            minute = minute_key(index)
            count = counts.get(minute, 0)
            if count < args.target_count:
                rows.append(
                    {
                        "minute": minute,
                        "current_count": count,
                        "needed": args.target_count - count,
                    }
                )

        args.output.parent.mkdir(parents=True, exist_ok=True)
        with args.output.open("w", encoding="utf-8", newline="") as handle:
            writer = csv.DictWriter(handle, fieldnames=["minute", "current_count", "needed"])
            writer.writeheader()
            writer.writerows(rows)

        print(
            f"Wrote {args.output} "
            f"({len(rows)} minutes below target count {args.target_count})"
        )
    except Exception as error:
        print(f"error: {error}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
