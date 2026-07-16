#!/usr/bin/env python3
"""根据 GitHub Actions 的实际构建产物生成 OTA latest/versions 清单。"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import sys
import tempfile
from pathlib import Path
from urllib.parse import urlparse

try:
    from ota_release_notes import compact_ota_notes
except ModuleNotFoundError:
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
    from ota_release_notes import compact_ota_notes


VERSION_RE = re.compile(r"^v(\d+)\.(\d+)\.(\d+)$")
LATEST_MAX_BYTES = 1800
VERSIONS_KEEP = 10


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--version", required=True)
    parser.add_argument("--app", required=True, type=Path)
    parser.add_argument("--merged", required=True, type=Path)
    parser.add_argument("--public-base", required=True)
    parser.add_argument("--notes-file", required=True, type=Path)
    parser.add_argument("--latest", required=True, type=Path)
    parser.add_argument("--versions", required=True, type=Path)
    return parser.parse_args()


def require_version(value: str) -> tuple[int, int, int]:
    match = VERSION_RE.fullmatch(value)
    if not match:
        raise ValueError(f"invalid version: {value}")
    return tuple(int(part) for part in match.groups())


def normalize_public_base(value: str) -> str:
    normalized = value.strip().rstrip("/")
    parsed = urlparse(normalized)
    if parsed.scheme != "https" or not parsed.netloc or parsed.query or parsed.fragment:
        raise ValueError("public base must be an HTTPS origin or path without query/fragment")
    return normalized


def load_release_notes(path: Path, version: str) -> str:
    try:
        source = path.read_text(encoding="utf-8").strip()
    except (OSError, UnicodeDecodeError) as exc:
        raise ValueError(f"unable to read OTA release notes: {exc}") from exc
    if not source.startswith(f"{version}："):
        raise ValueError("OTA release notes version does not match the build version")
    notes = compact_ota_notes(version, source)
    if not notes.startswith(f"{version}："):
        raise ValueError("compacted OTA release notes lost their version header")
    return notes


def file_metadata(path: Path) -> tuple[str, int]:
    if not path.is_file() or path.stat().st_size <= 0:
        raise ValueError(f"firmware file is missing or empty: {path}")
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest(), path.stat().st_size


def encode_json(data: object) -> bytes:
    return (json.dumps(data, ensure_ascii=False, indent=2) + "\n").encode("utf-8")


def fit_latest_notes(manifest: dict[str, object], notes: str) -> bytes:
    manifest["notes"] = notes
    encoded = encode_json(manifest)
    if len(encoded) <= LATEST_MAX_BYTES:
        return encoded

    suffix = "…"
    low = 0
    high = len(notes)
    best = ""
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
        raise ValueError("latest manifest metadata exceeds size limit")
    return encoded


def load_versions(path: Path) -> list[dict[str, object]]:
    if not path.is_file():
        return []
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ValueError(f"unable to read existing versions manifest: {exc}") from exc
    items = data.get("items") if isinstance(data, dict) else None
    if not isinstance(items, list):
        raise ValueError("existing versions manifest does not contain an items list")
    return [item for item in items if isinstance(item, dict)]


def atomic_write(path: Path, content: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temp_name = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    try:
        with os.fdopen(fd, "wb") as handle:
            handle.write(content)
        os.replace(temp_name, path)
    except Exception:
        try:
            os.unlink(temp_name)
        except FileNotFoundError:
            pass
        raise


def main() -> int:
    args = parse_args()
    require_version(args.version)
    public_base = normalize_public_base(args.public_base)
    notes = load_release_notes(args.notes_file, args.version)
    app_sha, app_size = file_metadata(args.app)
    merged_sha, merged_size = file_metadata(args.merged)

    app_name = f"weather_clock_{args.version}.bin"
    merged_name = f"weather_clock_{args.version}_merged.bin"
    current = {
        "version": args.version,
        "notes": notes,
        "app": {
            "url": f"{public_base}/firmware/{app_name}",
            "sha256": app_sha,
            "size": app_size,
        },
        "merged": {
            "url": f"{public_base}/firmware/{merged_name}",
            "sha256": merged_sha,
            "size": merged_size,
        },
    }

    existing = [item for item in load_versions(args.versions) if item.get("version") != args.version]
    for item in existing:
        version = str(item.get("version", ""))
        if VERSION_RE.fullmatch(version):
            item["notes"] = compact_ota_notes(version, str(item.get("notes", "")))
    sortable = [current, *existing]
    sortable.sort(key=lambda item: require_version(str(item.get("version", ""))), reverse=True)
    versions = {"latest": args.version, "items": sortable[:VERSIONS_KEEP]}

    latest = {
        "version": args.version,
        "url": current["app"]["url"],
        "sha256": app_sha,
        "size": app_size,
    }
    atomic_write(args.latest, fit_latest_notes(latest, notes))
    atomic_write(args.versions, encode_json(versions))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
