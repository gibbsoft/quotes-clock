from __future__ import annotations

import re
import unicodedata
from typing import Any


POST_NFKC_REPLACEMENTS = str.maketrans(
    {
        "\u0301": "'",  # Standalone acute accents are usually apostrophes in imported quote data.
        "\u2010": "-",  # NFKC maps non-breaking hyphen here; keep generated firmware ASCII-safe.
        "\u2044": "/",  # NFKC maps vulgar fractions to digit + fraction slash + digit.
        "\u2212": "-",
    }
)

DISPLAY_EXTRA_GLYPHS = set(
    "\u00bc\u00c9\u00e1\u00e2\u00e4\u00e7\u00e8\u00e9\u00eb\u00ed"
    "\u00f1\u00f6\u00f8\u00fc\u010d\u0161\u2011\u00b0\u00a3\u20ac"
)
DISPLAY_COMMON_GLYPHS = set("\u2018\u2019\u201c\u201d\u2014\u2013")


def normalize_display_text(value: Any) -> str:
    text = unicodedata.normalize("NFKC", str(value))
    text = text.replace(" \u0301", "'")
    return text.translate(POST_NFKC_REPLACEMENTS)


def compact_display_text(value: Any) -> str:
    return re.sub(r"\s+", " ", normalize_display_text(value)).strip()


def is_display_safe_char(char: str) -> bool:
    codepoint = ord(char)
    if char in "\n\r\t":
        return True
    if 0x20 <= codepoint <= 0x7E:
        return True
    return char in DISPLAY_EXTRA_GLYPHS or char in DISPLAY_COMMON_GLYPHS


def describe_char(char: str) -> str:
    return f"U+{ord(char):04X} {unicodedata.name(char, 'UNKNOWN')}"
