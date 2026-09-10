#!/usr/bin/env python3
"""Publish the backward-compatible four-locale OTA manifest."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

try:
    from ota_release_notes import compact_ota_notes
    from github_firmware_artifacts import (
        LATEST_MAX_BYTES,
        VERSIONS_KEEP,
        atomic_write,
        encode_json,
        version_key,
    )
except ModuleNotFoundError:
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from ota_release_notes import compact_ota_notes
    from github_firmware_artifacts import (
        LATEST_MAX_BYTES,
        VERSIONS_KEEP,
        atomic_write,
        encode_json,
        version_key,
    )


LOCALES = ("zh-TW", "zh-CN", "en", "ja")
LOCALE_RE = re.compile(r"^(zh-TW|zh-CN|en|ja)=(.+)$")


def file_metadata(path: Path) -> tuple[str, int]:
    if not path.is_file() or path.stat().st_size <= 0:
        raise ValueError(f"firmware file is missing or empty: {path}")
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest(), path.stat().st_size


def parse_image(value: str) -> tuple[str, Path]:
    match = LOCALE_RE.fullmatch(value)
    if not match:
        raise ValueError(f"image must use locale=path syntax: {value}")
    return match.group(1), Path(match.group(2))


def read_notes(path: Path, version: str) -> str:
    text = path.read_text(encoding="utf-8").strip()
    if not text.startswith(f"{version}："):
        raise ValueError("OTA release notes version does not match build version")
    return compact_ota_notes(version, text)


def read_existing(path: Path) -> list[dict[str, object]]:
    if not path.is_file():
        return []
    data = json.loads(path.read_text(encoding="utf-8"))
    items = data.get("items") if isinstance(data, dict) else None
    if not isinstance(items, list):
        raise ValueError("versions manifest does not contain an items list")
    return [item for item in items if isinstance(item, dict)]


def fit_latest_notes(manifest: dict[str, object], notes: str) -> bytes:
    manifest["notes"] = notes
    encoded = encode_json(manifest)
    if len(encoded) <= LATEST_MAX_BYTES:
        return encoded
    suffix = "…"
    low, high, best = 0, len(notes), ""
    while low <= high:
        middle = (low + high) // 2
        candidate = notes[:middle].rstrip() + suffix
        manifest["notes"] = candidate
        encoded = encode_json(manifest)
        if len(encoded) <= LATEST_MAX_BYTES:
            best = candidate
            low = middle + 1
        else:
            high = middle - 1
    manifest["notes"] = best
    encoded = encode_json(manifest)
    if len(encoded) > LATEST_MAX_BYTES:
        raise ValueError("latest locale manifest exceeds size limit")
    return encoded


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--version", required=True)
    parser.add_argument("--artifact-base", required=True)
    parser.add_argument("--notes-file", required=True, type=Path)
    parser.add_argument("--latest", required=True, type=Path)
    parser.add_argument("--versions", required=True, type=Path)
    parser.add_argument("--image", action="append", required=True)
    parser.add_argument("--merged", required=True, type=Path)
    args = parser.parse_args()

    version_key(args.version)
    base = args.artifact_base.rstrip("/")
    images: dict[str, dict[str, object]] = {}
    for raw in args.image:
        locale, path = parse_image(raw)
        if locale in images:
            raise ValueError(f"duplicate image locale: {locale}")
        digest, size = file_metadata(path)
        suffix = "" if locale == "zh-TW" else f"_{locale}"
        images[locale] = {
            "url": f"{base}/weather_clock_{args.version}{suffix}.bin",
            "sha256": digest,
            "size": size,
            "locale": locale,
        }
    if set(images) != set(LOCALES):
        raise ValueError("exactly one image is required for every supported locale")

    notes = read_notes(args.notes_file, args.version)
    current = {"version": args.version, "notes": notes, "images": images}
    merged_sha, merged_size = file_metadata(args.merged)
    # Keep the historical app/merged shape for release tooling and consumers
    # that have not migrated to the locale image map yet.
    current["app"] = images["zh-TW"]
    current["merged"] = {
        "url": f"{base}/weather_clock_{args.version}_merged.bin",
        "sha256": merged_sha,
        "size": merged_size,
    }
    existing = [
        item for item in read_existing(args.versions)
        if item.get("version") != args.version
    ]
    for item in existing:
        value = str(item.get("version", ""))
        version_key(value)
        item["notes"] = compact_ota_notes(value, str(item.get("notes", "")))
    items = sorted([current, *existing], key=lambda item: version_key(str(item["version"])), reverse=True)

    latest = {
        "version": args.version,
        "url": images["zh-TW"]["url"],
        "sha256": images["zh-TW"]["sha256"],
        "size": images["zh-TW"]["size"],
        "images": images,
    }
    atomic_write(args.latest, fit_latest_notes(latest, notes))
    atomic_write(args.versions, encode_json({"latest": args.version, "items": items[:VERSIONS_KEEP]}))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1)
