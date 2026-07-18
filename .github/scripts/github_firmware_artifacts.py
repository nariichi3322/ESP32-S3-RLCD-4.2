#!/usr/bin/env python3
# 统一 GitHub 源码构建与 OTA 镜像使用的固件及清单契约。
"""Define firmware and manifest contracts shared by GitHub automation."""

from __future__ import annotations

from typing import NamedTuple


FIELD_LATEST = "latest"
FIELD_ITEMS = "items"
FIELD_VERSION = "version"
FIELD_NOTES = "notes"
FIELD_URL = "url"
FIELD_SHA256 = "sha256"
FIELD_SIZE = "size"
LATEST_MAX_BYTES = 1800
VERSIONS_KEEP = 10


class FirmwareArtifactSpec(NamedTuple):
    manifest_key: str
    filename_suffix: str
    release_description: str


FIRMWARE_ARTIFACTS = (
    FirmwareArtifactSpec("app", ".bin", "设备 OTA 升级固件。"),
    FirmwareArtifactSpec("merged", "_merged.bin", "包含分区表的完整刷写固件。"),
)


def firmware_artifact_name(version: str, artifact: FirmwareArtifactSpec) -> str:
    return f"weather_clock_{version}{artifact.filename_suffix}"
