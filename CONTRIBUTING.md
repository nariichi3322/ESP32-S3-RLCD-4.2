# 贡献指南 / Contributing Guide

## 中文

感谢你愿意为 ESP32-S3 RLCD 4.2 天气时钟贡献代码、文档或测试。本仓库是 `RLCD_CLOCK` 固件的公开源码镜像，提交内容应围绕设备固件本身，并以稳定性、可验证性和向后兼容为优先。

### 开始之前

- 请先搜索现有 Issue，避免重复报告或重复实现。
- 缺陷报告请说明固件版本、硬件版本、复现步骤、预期结果和实际结果。
- 涉及崩溃、重启、联网、音频或显示异常时，请附经过脱敏的串口日志；不要上传 Wi-Fi 密码、QWeather API Key、API Host 私有配置、Token、NVS 镜像或本机绝对路径。
- 较大的功能、协议、分区、NVS 格式或 UI 变化，请先创建 Issue 说明目标、兼容性和验证方案，再开始实现。

### 开发环境

- 目标芯片：ESP32-S3。
- 推荐 ESP-IDF：`v5.5.3`，与公开仓库 GitHub Actions 使用的版本一致。
- 从仓库根目录执行固件构建：

```bash
source "$HOME/esp/esp-idf/export.sh"
idf.py set-target esp32s3
idf.py build
```

请勿提交 `build/`、固件二进制、个人配置、密钥、编辑器缓存或其它本地产物。

### 修改原则

- 一个 Pull Request 只处理一个清晰主题，避免同时混入无关重构。
- 优先复用现有模块和接口；只有职责确实过重或边界混乱时才拆分，不为拆分而拆分。
- 不得无意改变现有 UI、交互、OTA 协议、NVS 数据、分区表、资源格式或用户使用方式。
- 小智 AI 相关修改应尽量保持与上游 [`78/xiaozhi-esp32`](https://github.com/78/xiaozhi-esp32) 一致，避免无必要的拆分、合并和协议偏离。
- 用户可见功能发生变化时，请同步更新 `docs/User_zh.md` 和 `docs/User.md`。
- 公开项目介绍发生变化时，请同步更新中文 `README.md` 与英文 `README_EN.md`，并保持两份主页的功能、链接和许可说明一致。
- 新增日志不得输出完整凭据、鉴权头或用户隐私数据。

### 提交与 Pull Request

- 提交信息应简短说明修改目的。
- Pull Request 描述至少包含：问题背景、修改内容、影响范围、验证结果和剩余风险。
- UI 修改请附截图；硬件相关修改请说明实机型号和测试条件。
- 至少确保 ESP-IDF 固件编译通过。涉及运行逻辑的修改还应补充可重复的测试或明确的实机验证步骤。
- 维护者可能要求缩小修改范围、补充测试或更新文档。审核通过前请不要把未验证的行为描述为已解决。

### 许可

提交贡献即表示你有权提供这些内容，并同意你的贡献按照仓库中适用于相应代码的许可条款发布。项目维护者原创部分的非商业限制不会覆盖第三方组件原许可证已经授予的权利。

## English

Thank you for contributing code, documentation, or tests to the ESP32-S3 RLCD 4.2 Weather Clock. This repository is the public source mirror of the `RLCD_CLOCK` firmware. Contributions should focus on the device firmware and prioritize stability, verifiability, and backward compatibility.

### Before You Start

- Search existing issues before opening a new one.
- Bug reports should include the firmware version, hardware revision, reproduction steps, expected behavior, and actual behavior.
- For crashes, reboots, networking, audio, or display problems, attach sanitized serial logs. Never upload Wi-Fi passwords, QWeather API keys, private API Host configuration, tokens, NVS images, or absolute local paths.
- For larger feature, protocol, partition, NVS format, or UI changes, open an issue first and describe the goal, compatibility impact, and validation plan.

### Development Environment

- Target chip: ESP32-S3.
- Recommended ESP-IDF version: `v5.5.3`, matching the public GitHub Actions build.
- Build the firmware from the repository root:

```bash
source "$HOME/esp/esp-idf/export.sh"
idf.py set-target esp32s3
idf.py build
```

Do not commit `build/`, firmware binaries, personal configuration, secrets, editor caches, or other local artifacts.

### Change Guidelines

- Keep each pull request focused on one clear topic.
- Reuse existing modules and interfaces. Split code only when responsibilities are genuinely overloaded or unclear.
- Do not unintentionally change the current UI, interaction model, OTA protocol, NVS data, partition table, resource format, or user workflow.
- Xiaozhi AI changes should remain close to upstream [`78/xiaozhi-esp32`](https://github.com/78/xiaozhi-esp32) and avoid unnecessary splitting, merging, or protocol divergence.
- Update both `docs/User_zh.md` and `docs/User.md` when user-visible behavior changes.
- Update both the Chinese `README.md` and English `README_EN.md` when the public project overview changes, keeping features, links, and licensing information aligned.
- New logs must never expose complete credentials, authorization headers, or private user data.

### Commits and Pull Requests

- Use concise commit messages that explain the purpose of the change.
- Pull request descriptions should cover the background, changes, affected scope, validation results, and remaining risks.
- Include screenshots for UI changes and describe the device and test conditions for hardware-related changes.
- At minimum, ensure the ESP-IDF firmware builds successfully. Runtime changes should also include repeatable tests or explicit device validation steps.
- Maintainers may request a smaller scope, more tests, or updated documentation. Do not claim an unverified behavior is fixed before review is complete.

### Licensing

By contributing, you confirm that you have the right to provide the contribution and agree that it may be distributed under the license terms applicable to the corresponding code in this repository. The non-commercial restriction on maintainer-owned original work does not override rights granted by third-party licenses.
