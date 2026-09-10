#!/usr/bin/env python3
"""Static guardrails for catalog-backed user-visible UI text.

This check deliberately scans production UI/network/OTA/Xiaozhi sources only.
Protocol schemas, user-command recognition, remote text, log messages, and
calendar calculation lookup tables are not UI literals and are excluded.
"""

from __future__ import annotations

import ast
import json
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import generate_ui_text_catalog as catalog_tool  # noqa: E402


LOCALE_LITERAL_FILES = {
    ROOT / "main" / "ui" / "calendar_lunar.cpp",
    ROOT / "main" / "network" / "ip_region_text.h",
    ROOT / "main" / "network" / "weather_city_text.cpp",
    ROOT / "main" / "network" / "provisioning_validation.cpp",
    ROOT / "main" / "xiaozhi" / "xiaozhi_conversation_policy.h",
    ROOT / "main" / "xiaozhi" / "xiaozhi_mcp_schema.cpp",
    ROOT / "main" / "network" / "weather_city_mcp.cpp",
}

STRING_LITERAL = re.compile(r'"(?:\\.|[^"\\])*"')
EAST_ASIAN_TEXT = re.compile(r"[一-龥ぁ-んァ-ン]")
UI_ID_REFERENCE = re.compile(r"UiTextId::([A-Za-z_][A-Za-z0-9_]*)")


def production_files() -> list[Path]:
    files = []
    for directory in ("main/ui", "main/network", "main/ota", "main/xiaozhi"):
        files.extend((ROOT / directory).glob("*.cpp"))
        files.extend((ROOT / directory).glob("*.h"))
    files.append(ROOT / "main/core/main.cpp")
    return sorted(set(files))


def c_string_value(token: str) -> str:
    try:
        value = ast.literal_eval(token)
    except (SyntaxError, ValueError):
        return ""
    return value if isinstance(value, str) else ""


def check_direct_east_asian_literals(files: list[Path]) -> list[str]:
    failures = []
    for path in files:
        if path in LOCALE_LITERAL_FILES:
            continue
        for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            for token in STRING_LITERAL.findall(line):
                value = c_string_value(token)
                if not EAST_ASIAN_TEXT.search(value):
                    continue
                failures.append(
                    f"{path.relative_to(ROOT)}:{line_number}: "
                    f"unregistered East Asian UI literal {token}"
                )
    return failures


def check_ui_id_references(files: list[Path], header: Path) -> list[str]:
    valid_ids = set(catalog_tool.enum_names(header)) | {"Count"}
    failures = []
    for path in files:
        for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            for identifier in UI_ID_REFERENCE.findall(line):
                if identifier not in valid_ids:
                    failures.append(
                        f"{path.relative_to(ROOT)}:{line_number}: unknown UiTextId::{identifier}"
                    )
    return failures


def check_japanese_glyph_coverage(document: dict) -> list[str]:
    """Ensure every non-ASCII catalog glyph is present in the Japanese font chain."""
    simsun_source = (
        ROOT
        / "managed_components"
        / "lvgl__lvgl"
        / "src"
        / "font"
        / "lv_font_simsun_16_cjk.c"
    )
    extra_source = ROOT / "main" / "assets" / "ja_font_extra.c"
    if not simsun_source.exists() or not extra_source.exists():
        return ["Japanese font sources are missing"]

    simsun_codepoints = {
        int(match, 16)
        for match in re.findall(
            r"/\* U\+([0-9A-Fa-f]+) ",
            simsun_source.read_text(encoding="utf-8"),
        )
    }
    extra_codepoints = {
        int(match, 16)
        for match in re.findall(
            r"\{0x([0-9A-Fa-f]+),",
            extra_source.read_text(encoding="utf-8"),
        )
    }
    available = simsun_codepoints | extra_codepoints
    required = {
        ord(character)
        for entry in document["entries"]
        for character in entry["ja"]
        if ord(character) >= 0x80
    }
    missing = sorted(required - available)
    return [
        "Japanese catalog glyphs missing from font chain: "
        + ", ".join(f"U+{codepoint:04X}" for codepoint in missing)
    ] if missing else []


def check_legacy_and_portal_sources() -> list[str]:
    failures = []
    language_source = ROOT / "main/ui/ui_language.cpp"
    if "kCompatibilityLiterals" in language_source.read_text(encoding="utf-8"):
        failures.append("main/ui/ui_language.cpp still contains kCompatibilityLiterals")

    source_paths = [
        path
        for path in production_files()
        if path.name not in {"ui_i18n.cpp", "ui_language.cpp", "ui_language.h"}
    ]
    for path in source_paths:
        for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            if "ui_language_text(" in line:
                failures.append(
                    f"{path.relative_to(ROOT)}:{line_number}: "
                    "production call site still uses ui_language_text()"
                )

    asset_source = (ROOT / "main/network/wifi_portal_ui_assets.h").read_text(
        encoding="utf-8"
    )
    old_assets = re.findall(
        r"k(?:CommonScript|FormHtml)(?:Traditional|Simplified|English|Japanese)",
        asset_source,
    )
    for name in sorted(set(old_assets)):
        failures.append(f"wifi_portal_ui_assets.h still contains locale-specific asset {name}")
    for name in ("kCommonScript", "kFormHtml"):
        match = re.search(
            rf'inline constexpr char {name}\[\] = R"PORTAL\((.*?)\)PORTAL";',
            asset_source,
            flags=re.DOTALL,
        )
        if match and EAST_ASIAN_TEXT.search(match.group(1)):
            failures.append(f"wifi_portal_ui_assets.h {name} embeds visible East Asian text")
    return failures


def main() -> int:
    header = ROOT / "main/ui/ui_i18n.h"
    catalog = ROOT / "tools/ui_text_catalog.json"
    generated = ROOT / "main/ui/generated/ui_catalog_entries.inc"
    document = json.loads(catalog.read_text(encoding="utf-8"))
    catalog_tool.validate_against_header(document, header)
    catalog_tool.validate_generated(document, generated)

    files = production_files()
    failures = []
    failures.extend(check_direct_east_asian_literals(files))
    failures.extend(check_ui_id_references(files, header))
    failures.extend(check_japanese_glyph_coverage(document))
    failures.extend(check_legacy_and_portal_sources())
    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1
    print(
        f"UI catalog check passed: {len(document['entries'])} entries, "
        f"{len(files)} production files scanned"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
