#!/usr/bin/env python3
"""Generate the compile-time UI catalog from one locale source file.

The extractor is intentionally included so the first migration can turn the
existing checked-in catalog into JSON without hand-copying Unicode text. CI
uses the normal generation mode and fails when an id is missing or a locale
entry is empty.
"""
from __future__ import annotations

import argparse
import ast
import json
import re
from pathlib import Path

LOCALES = ("zh-TW", "zh-CN", "en", "ja")


def enum_names(header: Path) -> list[str]:
    text = header.read_text(encoding="utf-8")
    body = text.split("enum class UiTextId", 1)[1].split("};", 1)[0]
    body = body.split("{", 1)[1]
    names = []
    for item in body.split(","):
        item = item.strip()
        if item and re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", item) and item != "Count":
            names.append(item)
    return names


def extract(source: Path, header: Path) -> dict:
    names = enum_names(header)
    entries = []
    pattern = re.compile(r"UI_CATALOG\((.*)\),")
    for line in source.read_text(encoding="utf-8").splitlines():
        match = pattern.search(line)
        if not match:
            continue
        values = ast.literal_eval("(" + match.group(1) + ")")
        if not isinstance(values, tuple) or len(values) != 4:
            raise ValueError(f"invalid catalog row: {line}")
        entries.append(dict(zip(LOCALES, values)))
    if len(entries) != len(names):
        raise ValueError(f"catalog rows={len(entries)} stable ids={len(names)}")
    return {
        "catalog_version": 1,
        "locales": list(LOCALES),
        "entries": [dict(id=name, **entry) for name, entry in zip(names, entries)],
    }


def validate(catalog: dict) -> list[dict]:
    if catalog.get("locales") != list(LOCALES):
        raise ValueError("catalog locales must remain zh-TW, zh-CN, en, ja")
    entries = catalog.get("entries")
    if not isinstance(entries, list) or not entries:
        raise ValueError("catalog entries must be a non-empty list")
    ids = set()
    placeholder_pattern = re.compile(r"%(?:\d+\$)?[-+#0 ]*(?:\d+)?(?:\.\d+)?[diouxXeEfFgGcs%]")

    def placeholders(value: str) -> list[str]:
        return [token for token in placeholder_pattern.findall(value) if token != "%%"]

    for entry in entries:
        if not isinstance(entry, dict) or not entry.get("id"):
            raise ValueError("every catalog entry needs a stable id")
        if entry["id"] in ids:
            raise ValueError(f"duplicate catalog id: {entry['id']}")
        ids.add(entry["id"])
        for locale in LOCALES:
            if not isinstance(entry.get(locale), str) or not entry[locale]:
                raise ValueError(f"missing translation: {entry['id']} / {locale}")
        expected = placeholders(entry[LOCALES[0]])
        for locale in LOCALES[1:]:
            actual = placeholders(entry[locale])
            if actual != expected:
                raise ValueError(
                    f"placeholder mismatch: {entry['id']} / {locale}: "
                    f"expected {expected}, got {actual}"
                )
    return entries


def validate_against_header(catalog: dict, header: Path) -> None:
    catalog_ids = [entry["id"] for entry in validate(catalog)]
    stable_ids = enum_names(header)
    if catalog_ids != stable_ids:
        missing = [item for item in stable_ids if item not in catalog_ids]
        extra = [item for item in catalog_ids if item not in stable_ids]
        raise ValueError(
            "catalog/header IDs must match in order: "
            f"missing={missing}, extra={extra}"
        )


def generated_rows(output: Path) -> list[tuple[str, ...]]:
    pattern = re.compile(r"UI_CATALOG\((.*)\),")
    rows: list[tuple[str, ...]] = []
    for line in output.read_text(encoding="utf-8").splitlines():
        match = pattern.search(line)
        if not match:
            continue
        values = ast.literal_eval("(" + match.group(1) + ")")
        if not isinstance(values, tuple) or len(values) != len(LOCALES):
            raise ValueError(f"invalid generated catalog row: {line}")
        rows.append(tuple(values))
    return rows


def validate_generated(catalog: dict, output: Path) -> None:
    entries = validate(catalog)
    rows = generated_rows(output)
    expected = [tuple(entry[locale] for locale in LOCALES) for entry in entries]
    if len(rows) != len(expected):
        raise ValueError(
            f"generated catalog rows={len(rows)} catalog entries={len(expected)}"
        )
    if rows != expected:
        for index, (actual, wanted) in enumerate(zip(rows, expected)):
            if actual != wanted:
                raise ValueError(
                    f"generated catalog row {index} does not match catalog entry "
                    f"{entries[index]['id']}"
                )
        raise ValueError("generated catalog rows do not match catalog order")


def generate(catalog_path: Path, output: Path) -> None:
    catalog = json.loads(catalog_path.read_text(encoding="utf-8"))
    entries = validate(catalog)
    output.parent.mkdir(parents=True, exist_ok=True)
    lines = []
    for entry in entries:
        values = [json.dumps(entry[locale], ensure_ascii=False) for locale in LOCALES]
        lines.append(f"UI_CATALOG({', '.join(values)}),")
    output.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
    validate_generated(catalog, output)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--extract", action="store_true")
    parser.add_argument("--source", type=Path, default=Path("main/ui/ui_i18n.cpp"))
    parser.add_argument("--header", type=Path, default=Path("main/ui/ui_i18n.h"))
    parser.add_argument("--catalog", type=Path, default=Path("tools/ui_text_catalog.json"))
    parser.add_argument("--output", type=Path, default=Path("main/ui/generated/ui_catalog_entries.inc"))
    args = parser.parse_args()

    if args.extract:
        args.catalog.parent.mkdir(parents=True, exist_ok=True)
        args.catalog.write_text(
            json.dumps(extract(args.source, args.header), ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
            newline="\n",
        )
    catalog = json.loads(args.catalog.read_text(encoding="utf-8"))
    validate_against_header(catalog, args.header)
    generate(args.catalog, args.output)


if __name__ == "__main__":
    main()
