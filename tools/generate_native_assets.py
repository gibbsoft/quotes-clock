from __future__ import annotations

import argparse
import math
import re
import struct
import sys
from collections import Counter
from pathlib import Path
from typing import Any

import yaml
from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from tools.display_text import compact_display_text, normalize_display_text
from tools.generate_quotes_cpp import minute_to_index, quote_rows
from tools.validate_quotes import validate_document


EXTRA_GLYPHS = "¼Éáâäçèéëíñöøüčš°£€‑–—“”‘’…"
ASCII_GLYPHS = "".join(chr(code) for code in range(32, 127))
FULL_FONT_GLYPHS = "".join(dict.fromkeys(ASCII_GLYPHS + EXTRA_GLYPHS))
CLOCK_GLYPHS = "0123456789:"
CLOCK_SUFFIX_GLYPHS = "APM"

SANS_BOLD_FONTS = [
    "C:/Windows/Fonts/arialbd.ttf",
    "C:/Windows/Fonts/arial.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
    "/usr/share/fonts/truetype/lato/Lato-Bold.ttf",
]
SANS_REGULAR_FONTS = [
    "C:/Windows/Fonts/arial.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/lato/Lato-Regular.ttf",
]
SERIF_REGULAR_FONTS = [
    "C:/Windows/Fonts/times.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf",
]
SERIF_ITALIC_FONTS = [
    "C:/Windows/Fonts/timesi.ttf",
    "C:/Windows/Fonts/times.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSerif-Italic.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf",
]

FONT_SPECS = [
    ("clock_300", 300, SANS_BOLD_FONTS, CLOCK_GLYPHS),
    ("clock_suffix_150", 150, SANS_BOLD_FONTS, CLOCK_SUFFIX_GLYPHS),
    ("clock_260", 260, SANS_BOLD_FONTS, CLOCK_GLYPHS),
    ("clock_suffix_130", 130, SANS_BOLD_FONTS, CLOCK_SUFFIX_GLYPHS),
    ("clock_220", 220, SANS_BOLD_FONTS, CLOCK_GLYPHS),
    ("clock_suffix_110", 110, SANS_BOLD_FONTS, CLOCK_SUFFIX_GLYPHS),
    ("clock_180", 180, SANS_BOLD_FONTS, CLOCK_GLYPHS),
    ("clock_suffix_90", 90, SANS_BOLD_FONTS, CLOCK_SUFFIX_GLYPHS),
    ("clock_150", 150, SANS_BOLD_FONTS, CLOCK_GLYPHS),
    ("clock_suffix_75", 75, SANS_BOLD_FONTS, CLOCK_SUFFIX_GLYPHS),
    ("clock_120", 120, SANS_BOLD_FONTS, CLOCK_GLYPHS),
    ("clock_suffix_60", 60, SANS_BOLD_FONTS, CLOCK_SUFFIX_GLYPHS),
    ("clock", 88, SANS_BOLD_FONTS, CLOCK_GLYPHS),
    ("clock_suffix", 44, SANS_BOLD_FONTS, CLOCK_SUFFIX_GLYPHS),
    ("header_date", 24, SANS_BOLD_FONTS, ASCII_GLYPHS),
    ("title", 32, SANS_BOLD_FONTS, ASCII_GLYPHS),
    ("quote_34", 34, SANS_BOLD_FONTS, FULL_FONT_GLYPHS),
    ("quote_30", 30, SANS_BOLD_FONTS, FULL_FONT_GLYPHS),
    ("quote_26", 26, SANS_BOLD_FONTS, FULL_FONT_GLYPHS),
    ("quote_22", 22, SANS_BOLD_FONTS, FULL_FONT_GLYPHS),
    ("quote_18", 18, SANS_BOLD_FONTS, FULL_FONT_GLYPHS),
    ("body", 30, SANS_REGULAR_FONTS, ASCII_GLYPHS),
    ("book_title", 24, SERIF_ITALIC_FONTS, FULL_FONT_GLYPHS),
    ("book_author", 24, SERIF_REGULAR_FONTS + SANS_REGULAR_FONTS, FULL_FONT_GLYPHS),
    ("meta", 24, SANS_REGULAR_FONTS, ASCII_GLYPHS),
    ("footer", 12, SANS_BOLD_FONTS, ASCII_GLYPHS),
]
QUOTE_TOKEN_MARKER = 0xFF
QUOTE_TOKEN_COUNT = 256
QUOTE_PACK_MAGIC = b"QCQ3"
QUOTE_PACK_VERSION = 3
QUOTE_PACK_HEADER = struct.Struct("<4sHHHHB3xIIIIIII")
QUOTE_PACK_RECORD = struct.Struct("<HBBIHIHIHHH")
QUOTE_PACK_TOKEN = struct.Struct("<HB")
QUOTE_CATEGORY_TIME_SPECIFIC = 0
QUOTE_CATEGORY_CLASSIC = 1
QUOTE_CLASSIC_MINUTE = 0xFFFF


def cpp_string(value: Any) -> str:
    escaped = ['"']
    for char in normalize_display_text(value):
        codepoint = ord(char)
        if char == '"':
            escaped.append(r"\"")
        elif char == "\\":
            escaped.append(r"\\")
        elif char == "\n":
            escaped.append(r"\n")
        elif char == "\r":
            escaped.append(r"\r")
        elif char == "\t":
            escaped.append(r"\t")
        elif 0x20 <= codepoint <= 0x7E:
            escaped.append(char)
        elif codepoint <= 0xFFFF:
            escaped.append(f"\\u{codepoint:04x}")
        else:
            escaped.append(f"\\U{codepoint:08x}")
    escaped.append('"')
    return "".join(escaped)


def select_one_quote_per_minute(data: dict[str, Any]) -> list[tuple[int, dict[str, Any]]]:
    selected: list[tuple[int, dict[str, Any]]] = []
    seen: set[int] = set()
    for minute, quote in quote_rows(data):
        if minute in seen:
            continue
        selected.append((minute, quote))
        seen.add(minute)
    return selected


def select_quote_records(data: dict[str, Any]) -> list[tuple[int, int, dict[str, Any]]]:
    records = [
        (minute, QUOTE_CATEGORY_TIME_SPECIFIC, quote)
        for minute, quote in select_one_quote_per_minute(data)
    ]
    for quote in data.get("classic_quotes", []):
        records.append((QUOTE_CLASSIC_MINUTE, QUOTE_CATEGORY_CLASSIC, quote))
    return records


def find_font(candidates: list[str]) -> str:
    for candidate in candidates:
        path = Path(candidate)
        if path.exists():
            return str(path)
    raise FileNotFoundError(f"Could not find any font candidate: {candidates}")


def bytes_literal(values: list[int], *, indent: str = "  ") -> list[str]:
    if not values:
        return [f"{indent}0x00,"]

    lines: list[str] = []
    for offset in range(0, len(values), 16):
        chunk = values[offset : offset + 16]
        lines.append(indent + ", ".join(f"0x{value:02X}" for value in chunk) + ",")
    return lines


def quote_field_values(quote: dict[str, Any]) -> list[str]:
    return [
        compact_display_text(quote["text"]),
        normalize_display_text(quote["title"]),
        normalize_display_text(quote["author"]),
    ]


def utf8_byte_span(text: str, start: int, length: int) -> tuple[int, int]:
    return len(text[:start].encode("utf-8")), len(text[start : start + length].encode("utf-8"))


def quote_highlight_span(quote: dict[str, Any]) -> tuple[int, int]:
    if "time_text" not in quote or not str(quote["time_text"]).strip():
        return 0, 0
    text = compact_display_text(quote["text"])
    time_text = compact_display_text(quote["time_text"])
    start = text.lower().find(time_text.lower())
    if start < 0:
        raise ValueError(f"Quote time_text does not appear after display normalization: {quote['id']}")
    return utf8_byte_span(text, start, len(time_text))


def build_quote_dictionary(fields: list[str], *, max_tokens: int = QUOTE_TOKEN_COUNT) -> list[bytes]:
    candidates: Counter[bytes] = Counter()
    for field in fields:
        tokens = re.findall(r"[A-Za-z0-9]+|[^A-Za-z0-9]", field)
        for length in range(1, 8):
            for index in range(0, len(tokens) - length + 1):
                phrase = "".join(tokens[index : index + length]).encode("utf-8")
                if 4 <= len(phrase) <= 64:
                    candidates[phrase] += 1

    scored: list[tuple[int, bytes]] = []
    for phrase, count in candidates.items():
        if count < 3:
            continue
        score = (len(phrase) - 2) * count - (len(phrase) + 3)
        if score > 0:
            scored.append((score, phrase))
    scored.sort(reverse=True)
    return [phrase for _, phrase in scored[:max_tokens]]


def compress_quote_field(value: str, dictionary: list[bytes]) -> bytes:
    source = value.encode("utf-8")
    ordered_tokens = sorted(range(len(dictionary)), key=lambda index: len(dictionary[index]), reverse=True)
    out = bytearray()
    offset = 0
    while offset < len(source):
        token_index: int | None = None
        for index in ordered_tokens:
            phrase = dictionary[index]
            if source.startswith(phrase, offset):
                token_index = index
                break
        if token_index is None:
            out.append(source[offset])
            offset += 1
        else:
            out.append(QUOTE_TOKEN_MARKER)
            out.append(token_index)
            offset += len(dictionary[token_index])
    return bytes(out)


def build_quote_assets(data: dict[str, Any]) -> tuple[
    list[tuple[int, int, dict[str, Any]]],
    list[tuple[int, int]],
    bytearray,
    bytearray,
    list[tuple[int, int, tuple[int, int], tuple[int, int], tuple[int, int], tuple[int, int]]],
]:
    rows = select_quote_records(data)
    fields = [field for _, _, quote in rows for field in quote_field_values(quote)]
    dictionary = build_quote_dictionary(fields)
    dictionary_data = bytearray()
    dictionary_records: list[tuple[int, int]] = []
    for phrase in dictionary:
        if len(phrase) > 255:
            raise ValueError(f"Quote dictionary phrase is too long: {phrase!r}")
        if len(dictionary_data) > 0xFFFF:
            raise ValueError("Quote dictionary data exceeds uint16_t offsets")
        dictionary_records.append((len(dictionary_data), len(phrase)))
        dictionary_data.extend(phrase)

    quote_data = bytearray()
    quote_records: list[tuple[int, int, tuple[int, int], tuple[int, int], tuple[int, int], tuple[int, int]]] = []
    for minute, category, quote in rows:
        record_fields: list[tuple[int, int]] = []
        for field in quote_field_values(quote):
            compressed = compress_quote_field(field, dictionary)
            if len(compressed) > 0xFFFF:
                raise ValueError(f"Quote field for minute {minute} exceeds uint16_t length")
            record_fields.append((len(quote_data), len(compressed)))
            quote_data.extend(compressed)
        highlight = quote_highlight_span(quote)
        if highlight[0] > 0xFFFF or highlight[1] > 0xFFFF:
            raise ValueError(f"Quote highlight span for minute {minute} exceeds uint16_t range")
        quote_records.append((minute, category, record_fields[0], record_fields[1], record_fields[2], highlight))

    return rows, dictionary_records, dictionary_data, quote_data, quote_records


def generate_quote_pack(data: dict[str, Any]) -> bytes:
    rows, dictionary_records, dictionary_data, quote_data, quote_records = build_quote_assets(data)
    records = bytearray()
    for minute, category, text, title, author, highlight in quote_records:
        records.extend(
            QUOTE_PACK_RECORD.pack(
                minute,
                category,
                0,
                text[0],
                text[1],
                title[0],
                title[1],
                author[0],
                author[1],
                highlight[0],
                highlight[1],
            )
        )

    tokens = bytearray()
    for offset, length in dictionary_records:
        tokens.extend(QUOTE_PACK_TOKEN.pack(offset, length))

    header_size = QUOTE_PACK_HEADER.size
    records_offset = header_size
    tokens_offset = records_offset + len(records)
    dictionary_offset = tokens_offset + len(tokens)
    quote_data_offset = dictionary_offset + len(dictionary_data)
    total_size = quote_data_offset + len(quote_data)
    header = QUOTE_PACK_HEADER.pack(
        QUOTE_PACK_MAGIC,
        QUOTE_PACK_VERSION,
        header_size,
        len(rows),
        len(dictionary_records),
        QUOTE_TOKEN_MARKER,
        records_offset,
        tokens_offset,
        dictionary_offset,
        len(dictionary_data),
        quote_data_offset,
        len(quote_data),
        total_size,
    )
    return bytes(header + records + tokens + dictionary_data + quote_data)


def render_font(name: str, size: int, candidates: list[str], glyphs: str) -> str:
    font_path = find_font(candidates)
    font = ImageFont.truetype(font_path, size=size)
    ascent, descent = font.getmetrics()
    line_height = int(math.ceil((ascent + descent) * 1.08))
    leading = max(0, line_height - ascent - descent)
    baseline = ascent + (leading // 2)
    bitmap: list[int] = []
    glyph_lines: list[str] = []

    for char in glyphs:
        codepoint = ord(char)
        advance = max(1, int(math.ceil(font.getlength(char))))
        bbox = font.getbbox(char, anchor="ls")

        if bbox is None or bbox[2] <= bbox[0] or bbox[3] <= bbox[1]:
            width = 0
            height = 0
            x_offset = 0
            y_offset = 0
            offset = len(bitmap)
        else:
            left, top, right, bottom = bbox
            width = right - left
            height = bottom - top
            x_offset = left
            y_offset = baseline + top
            offset = len(bitmap)
            cropped = Image.new("L", (width, height), 0)
            draw = ImageDraw.Draw(cropped)
            draw.text((-left, -top), char, font=font, fill=255, anchor="ls")
            for y in range(height):
                current = 0
                mask = 0x80
                for x in range(width):
                    if cropped.getpixel((x, y)) >= 128:
                        current |= mask
                    mask >>= 1
                    if mask == 0:
                        bitmap.append(current)
                        current = 0
                        mask = 0x80
                if mask != 0x80:
                    bitmap.append(current)

        glyph_lines.append(
            "  {"
            f"0x{codepoint:04X}, {offset}, {width}, {height}, {x_offset}, {y_offset}, {advance}"
            "},"
        )

    symbol = "".join(part.capitalize() for part in name.split("_"))
    lines = [
        f"static const uint8_t kFont{symbol}Bitmap[] = {{",
        *bytes_literal(bitmap),
        "};",
        "",
        f"static const Glyph kFont{symbol}Glyphs[] = {{",
        *glyph_lines,
        "};",
        "",
        f"static const Font kFont{symbol} = {{",
        f"  {line_height},",
        f"  {baseline},",
        f"  {len(glyphs)},",
        f"  kFont{symbol}Glyphs,",
        f"  kFont{symbol}Bitmap,",
        "};",
        "",
    ]
    return "\n".join(lines)


def generate(data: dict[str, Any], source: Path, *, omit_quote_arrays: bool = False) -> str:
    rows, dictionary_records, dictionary_data, quote_data, quote_records = build_quote_assets(data)
    lines = [
        "// Generated by tools/generate_native_assets.py.",
        f"// Source: {source.as_posix()}",
        "// Do not edit by hand.",
        "",
        "#pragma once",
        "",
        "#include <stddef.h>",
        "#include <stdint.h>",
        "",
        "namespace quotes_clock::assets {",
        "",
        "struct QuoteRecord {",
        "  uint16_t minute;",
        "  uint8_t category;",
        "  uint8_t reserved;",
        "  uint32_t text_offset;",
        "  uint16_t text_length;",
        "  uint32_t title_offset;",
        "  uint16_t title_length;",
        "  uint32_t author_offset;",
        "  uint16_t author_length;",
        "  uint16_t highlight_offset;",
        "  uint16_t highlight_length;",
        "};",
        "",
        "struct QuoteToken {",
        "  uint16_t offset;",
        "  uint8_t length;",
        "};",
        "",
        "struct Glyph {",
        "  uint32_t codepoint;",
        "  uint32_t bitmap_offset;",
        "  uint16_t width;",
        "  uint16_t height;",
        "  int16_t x_offset;",
        "  int16_t y_offset;",
        "  uint16_t x_advance;",
        "};",
        "",
        "struct Font {",
        "  uint16_t line_height;",
        "  uint16_t baseline;",
        "  uint16_t glyph_count;",
        "  const Glyph *glyphs;",
        "  const uint8_t *bitmap;",
        "};",
        "",
        f"static constexpr size_t kQuoteCount = {len(rows)};",
        f"static constexpr uint8_t kQuoteTokenMarker = 0x{QUOTE_TOKEN_MARKER:02X};",
        f"static constexpr size_t kQuoteTokenCount = {len(dictionary_records)};",
        f"static constexpr size_t kQuoteDataSize = {len(quote_data)};",
        f"static constexpr size_t kQuoteDictionaryDataSize = {len(dictionary_data)};",
        "",
        "static const QuoteRecord kQuotes[] = {",
    ]

    if omit_quote_arrays:
        lines.extend(
            [
                "  {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},",
                "};",
                "",
                "// Quote arrays are emitted to the native quote_data partition.",
                "",
            ]
        )
    else:
        for minute, category, text, title, author, highlight in quote_records:
            lines.append(
                "  {"
                f"{minute}, {category}, 0, {text[0]}, {text[1]}, {title[0]}, {title[1]}, {author[0]}, {author[1]}, "
                f"{highlight[0]}, {highlight[1]}"
                "},"
            )

        lines.extend(
            [
                "};",
                "",
                "static const QuoteToken kQuoteTokens[] = {",
                *[f"  {{{offset}, {length}}}," for offset, length in dictionary_records],
                "};",
                "",
                "static const uint8_t kQuoteDictionaryData[] = {",
                *bytes_literal(list(dictionary_data)),
                "};",
                "",
                "static const uint8_t kQuoteData[] = {",
                *bytes_literal(list(quote_data)),
                "};",
                "",
            ]
        )
    for spec in FONT_SPECS:
        lines.append(render_font(*spec))

    lines.extend(["}  // namespace quotes_clock::assets", ""])
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate native firmware quote and font assets.")
    parser.add_argument("--input", type=Path, default=Path("data/quotes.sample.yaml"))
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("firmware/native-idf/main/generated/quotes_clock_assets.hpp"),
    )
    parser.add_argument("--quote-output", type=Path, help="Optional native quote_data partition binary output.")
    parser.add_argument(
        "--omit-quote-arrays",
        action="store_true",
        help="Emit font/layout assets but leave quote data out of the generated C++ header.",
    )
    args = parser.parse_args()

    with args.input.open("r", encoding="utf-8") as handle:
        data = yaml.safe_load(handle)

    validate_document(data, args.input)
    output = generate(data, args.input, omit_quote_arrays=args.omit_quote_arrays)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(output, encoding="utf-8", newline="\n")
    print(f"Wrote {args.output}")
    if args.quote_output:
        quote_output = generate_quote_pack(data)
        args.quote_output.parent.mkdir(parents=True, exist_ok=True)
        args.quote_output.write_bytes(quote_output)
        print(f"Wrote {args.quote_output} ({len(quote_output)} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
