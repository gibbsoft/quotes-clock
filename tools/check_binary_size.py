import argparse
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser(description="Check a firmware binary against a maximum byte size.")
    parser.add_argument("--file", required=True, type=Path, help="Firmware binary to check")
    parser.add_argument("--max-bytes", required=True, type=lambda value: int(value, 0), help="Maximum allowed bytes")
    args = parser.parse_args()

    if not args.file.exists():
        parser.error(f"{args.file} does not exist")

    size = args.file.stat().st_size
    remaining = args.max_bytes - size
    print(f"{args.file}: {size} bytes ({size / 1024:.1f} KiB)")
    print(f"limit: {args.max_bytes} bytes ({args.max_bytes / 1024:.1f} KiB)")
    if remaining < 0:
        print(f"over limit by {-remaining} bytes ({-remaining / 1024:.1f} KiB)")
        return 1

    print(f"headroom: {remaining} bytes ({remaining / 1024:.1f} KiB)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
