#!/usr/bin/env python3
"""把完整 Release 说明压缩为设备 OTA 清单使用的短摘要。"""

from __future__ import annotations

import re
import sys


MAX_OTA_NOTES_BYTES = 768
MAX_NUMBERED_ITEM_CHARS = 44
MAX_FALLBACK_CHARS = 96
NUMBERED_ITEM_RE = re.compile(r"^\s*\d+\.\s+(.+?)\s*$")


def shorten_text(value: str, limit: int) -> str:
    compact = " ".join(value.split())
    if len(compact) <= limit:
        return compact
    return compact[: max(0, limit - 1)].rstrip() + "…"


def fit_utf8(value: str, limit: int) -> str:
    encoded = value.encode("utf-8")
    if len(encoded) <= limit:
        return value
    suffix = "…"
    suffix_bytes = len(suffix.encode("utf-8"))
    payload = encoded[: max(0, limit - suffix_bytes)]
    while payload:
        try:
            return payload.decode("utf-8").rstrip() + suffix
        except UnicodeDecodeError:
            payload = payload[:-1]
    return suffix if suffix_bytes <= limit else ""


def compact_ota_notes(version: str, release_notes: str) -> str:
    text = release_notes.strip()
    if text.startswith(f"{version}：") and (
        "源码仓库同版本 Release。" in text
        or "完整说明见同版本 Release。" in text
    ):
        return fit_utf8(text, MAX_OTA_NOTES_BYTES)

    items = [
        match.group(1)
        for line in text.splitlines()
        if (match := NUMBERED_ITEM_RE.match(line)) is not None
    ]
    if items:
        header = f"{version}：共 {len(items)} 项更新。"
        selected: list[str] = []
        for index, item in enumerate(items, start=1):
            candidate = [
                *selected,
                f"{index}. {shorten_text(item, MAX_NUMBERED_ITEM_CHARS)}",
            ]
            omitted = len(items) - len(candidate)
            footer = (
                f"其余 {omitted} 项见源码仓库同版本 Release。"
                if omitted
                else "完整说明见源码仓库同版本 Release。"
            )
            result = "\n".join((header, *candidate, footer))
            if len(result.encode("utf-8")) > MAX_OTA_NOTES_BYTES:
                break
            selected = candidate

        omitted = len(items) - len(selected)
        footer = (
            f"其余 {omitted} 项见源码仓库同版本 Release。"
            if omitted
            else "完整说明见源码仓库同版本 Release。"
        )
        return fit_utf8(
            "\n".join((header, *selected, footer)),
            MAX_OTA_NOTES_BYTES,
        )

    detail = text
    header_match = re.match(rf"^`?{re.escape(version)}`?：\s*", detail)
    if header_match:
        detail = detail[header_match.end():]
    detail = shorten_text(detail, MAX_FALLBACK_CHARS)
    if not detail:
        detail = "包含本版本功能修复、稳定性与资源优化。"
    return fit_utf8(
        f"{version}：{detail}\n完整说明见源码仓库同版本 Release。",
        MAX_OTA_NOTES_BYTES,
    )


def main() -> int:
    if len(sys.argv) != 2 or re.fullmatch(r"v\d+\.\d+\.\d+", sys.argv[1]) is None:
        print("usage: ota_release_notes.py vX.Y.Z < release-notes.txt", file=sys.stderr)
        return 2
    print(compact_ota_notes(sys.argv[1], sys.stdin.read()))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
