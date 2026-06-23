from __future__ import annotations

import argparse
import csv
import json
import subprocess
import sys
from pathlib import Path


ALIGNMENTS = {
    "app": 0x10000,
    "data": 0x1000,
}


def parse_int(value: str) -> int:
    value = value.strip()
    if value.lower().startswith("0x"):
        return int(value, 16)
    return int(value)


def align_up(value: int, alignment: int) -> int:
    return ((value + alignment - 1) // alignment) * alignment


def partition_offsets(path: Path) -> dict[str, int]:
    offsets: dict[str, int] = {}
    next_offset = 0x9000

    with path.open(newline="") as file:
        for row in csv.reader(line for line in file if not line.lstrip().startswith("#")):
            if len(row) < 5:
                continue
            name, partition_type, _subtype, offset, size = [cell.strip() for cell in row[:5]]
            if not name:
                continue
            if offset:
                current_offset = parse_int(offset)
            else:
                current_offset = align_up(next_offset, ALIGNMENTS.get(partition_type, 0x1000))
            offsets[name] = current_offset
            next_offset = current_offset + parse_int(size)

    return offsets


def main() -> int:
    parser = argparse.ArgumentParser(description="Create a monolithic native ESP-IDF rescue flash image.")
    parser.add_argument("--project-dir", type=Path, default=Path("firmware/native-idf"))
    parser.add_argument("--quote-data", type=Path, default=Path("firmware/native-idf/main/generated/quote_data.bin"))
    parser.add_argument("--output", type=Path, default=Path("firmware/native-idf/build/quotes-clock-native-rescue.bin"))
    args = parser.parse_args()

    project_dir = args.project_dir
    build_dir = project_dir / "build"
    flasher_args = json.loads((build_dir / "flasher_args.json").read_text())
    flash_settings = flasher_args["flash_settings"]
    flash_files = {
        int(offset, 16): build_dir / relative_path for offset, relative_path in flasher_args["flash_files"].items()
    }

    quote_offsets = partition_offsets(project_dir / "partitions.csv")
    flash_files[quote_offsets["quote_data"]] = args.quote_data

    missing = [path for path in flash_files.values() if not path.exists()]
    if missing:
        raise FileNotFoundError("Missing flash image inputs: " + ", ".join(str(path) for path in missing))

    args.output.parent.mkdir(parents=True, exist_ok=True)
    command = [
        sys.executable,
        "-m",
        "esptool",
        "--chip",
        "esp32",
        "merge-bin",
        "-o",
        str(args.output),
        "--flash-mode",
        flash_settings["flash_mode"],
        "--flash-freq",
        flash_settings["flash_freq"],
        "--flash-size",
        flash_settings["flash_size"],
    ]
    for offset, path in sorted(flash_files.items()):
        command.extend([f"0x{offset:x}", str(path)])

    subprocess.run(command, check=True)
    print(f"Wrote {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
