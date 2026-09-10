"""Print the Unicode code points required by the built-in i18n resources.

This intentionally has no third-party dependency. It is a sizing aid for a
future LVGL glyph subset and does not alter firmware sources.
"""
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
SOURCES = (
    ROOT / "main" / "ui" / "ui_i18n.cpp",
    ROOT / "main" / "ui" / "ui_language.cpp",
)

strings = []
for source in SOURCES:
    text = source.read_text(encoding="utf-8")
    strings.extend(re.findall(r'"((?:\\.|[^"\\])*)"', text))
codepoints = sorted({ord(ch) for value in strings for ch in value})
print(" ".join(f"0x{codepoint:04X}" for codepoint in codepoints))
print(f"# glyphs: {len(codepoints)}")
