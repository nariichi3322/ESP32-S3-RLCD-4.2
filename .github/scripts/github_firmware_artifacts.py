#!/usr/bin/env python3
# 统一 GitHub 源码构建与 OTA 镜像使用的双固件产物契约。
"""Define the firmware artifacts shared by GitHub build and mirror automation."""

from __future__ import annotations

from typing import NamedTuple


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
