# Xiaozhi protocol reference

This directory adapts the activation and WebSocket/Opus protocol documented by
[`78/xiaozhi-esp32`](https://github.com/78/xiaozhi-esp32), revision
`7b190b78e4f8dfef14126f6cd478c134b3cd3cd8`.

The upstream project is MIT licensed. This firmware does not copy its board,
Wi-Fi provisioning, display, OTA, MCP, remote-control or task framework. It
implements the interoperable activation, binding and audio session messages
against the existing RLCD services. Official `llm.emotion` names are accepted
and rendered as lightweight monochrome faces by the native RLCD UI; upstream
emoji image assets and display code are not copied. The small wake acknowledgement asset
`assets/common/popup.ogg` is reused from that revision and converted at build
time to 16 kHz mono PCM as `components/port_bsp/pcm/xiaozhi_binding/popup.pcm`.

The Xiaozhi-only codec path also follows the upstream Waveshare
ESP32-S3-RLCD-4.2 topology: standard TX, four-slot TDM RX with microphone and
playback-reference selection, device-side AEC, and realtime listening. The
existing RLCD network, power, audio ownership and non-AI playback services
remain authoritative.
