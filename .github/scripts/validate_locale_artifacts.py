#!/usr/bin/env python3
"""Validate locale image names, size limits, hashes, and embedded metadata."""

from __future__ import annotations

import argparse
import hashlib
import struct
from pathlib import Path


LOCALES = {
    "zh-TW": (0, "zh-TW"),
    "zh-CN": (1, "zh-CN"),
    "en": (2, "en"),
    "ja": (3, "ja"),
}
OTA_SLOT_SIZE = 0x6C0000
METADATA_MAGIC = 0x57434C31
METADATA_FORMAT_VERSION = 1
METADATA_SIZE = 15


def parse_image(value: str) -> tuple[str, Path]:
    locale, separator, filename = value.partition("=")
    if not separator or locale not in LOCALES or not filename:
        raise ValueError(f"image must use supported locale=path syntax: {value}")
    return locale, Path(filename)


def find_metadata(data: bytes, locale: str) -> bool:
    expected_id, expected_tag = LOCALES[locale]
    needle = struct.pack("<I", METADATA_MAGIC)
    offset = 0
    while True:
        offset = data.find(needle, offset)
        if offset < 0:
            return False
        if offset + METADATA_SIZE <= len(data):
            magic, version, locale_id = struct.unpack_from("<IHB", data, offset)
            raw_tag = data[offset + 7 : offset + 15]
            tag = raw_tag.split(b"\0", 1)[0].decode("ascii", errors="ignore")
            if (magic == METADATA_MAGIC and version == METADATA_FORMAT_VERSION and
                    locale_id == expected_id and tag == expected_tag):
                return True
        offset += 1


def validate_image(locale: str, path: Path, version: str) -> None:
    if not path.is_file() or path.stat().st_size <= 0:
        raise ValueError(f"missing or empty image: {path}")
    if path.stat().st_size >= OTA_SLOT_SIZE:
        raise ValueError(f"image exceeds OTA slot: {path}")
    suffix = "" if locale == "zh-TW" else f"_{locale}"
    expected_name = f"weather_clock_{version}{suffix}.bin"
    if path.name != expected_name:
        raise ValueError(f"{locale} image has unexpected filename: {path.name} != {expected_name}")
    data = path.read_bytes()
    if not find_metadata(data, locale):
        raise ValueError(f"{locale} image metadata does not match its locale: {path}")
    digest = hashlib.sha256(data).hexdigest()
    print(f"{locale} {path} size={len(data)} sha256={digest}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--version", required=True)
    parser.add_argument("--image", action="append", required=True)
    args = parser.parse_args()

    parsed = [parse_image(value) for value in args.image]
    images = dict(parsed)
    if len(parsed) != len(LOCALES) or len(images) != len(parsed) or set(images) != set(LOCALES):
        raise ValueError("exactly one image is required for every supported locale")
    for locale, path in images.items():
        validate_image(locale, path, args.version)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as exc:
        raise SystemExit(f"error: {exc}")
