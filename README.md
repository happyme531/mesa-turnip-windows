# Mesa Turnip for Windows ARM64

[![Proudly Vibe Coded - Plasma Mix](https://vibecoded.fyi/badges/flat/main/proudly-vibe-coded-plasma-mix.svg)](https://vibecoded.fyi/)
[![Coded with Codex](https://vibecoded.fyi/badges/flat/agents/codex.svg)](https://vibecoded.fyi/)

本 fork 的 Windows 移植、修复、构建脚本与文档改动全部由 Codex AI 编写。上游 Mesa 代码归原作者；AI 标记仅说明本 fork 的新增改动。

实验性的 Snapdragon / Adreno Vulkan 驱动移植，开发分支为 `turnip-windows-gsl-dev`。

Turnip 负责 Vulkan 和 GPU 命令生成；新增的 Windows GSL 后端使用系统已有的高通 `libgsluser.dll` 与内核驱动。它不是完整开源的 Windows 显卡驱动，也没有替换系统驱动。

[使用与构建教程](windows/turnip/README.zh-CN.md) · [移植说明与验证记录](windows/turnip/NOTES.md) · [CI 构建](https://github.com/happyme531/mesa-turnip-windows/actions/workflows/turnip-windows-arm64.yml)

目前已在 Snapdragon X Elite / Adreno X1-85 上验证 GPU fill、compute、Win32 立方体、GMEM、UBWC 和 Minecraft Java 26.3 原生 Vulkan。Minecraft 画面正常，但最新实测仍未观察到明显性能改善。自动离屏用例的提升不能直接换算成游戏帧率。

GitHub CI 生成原生 ARM64 驱动包并运行 CPU 回归；没有在托管 runner 上验证 Adreno GPU。未通过 Vulkan CTS，纯 x64 应用与 ARM64EC/ARM64X ICD 尚未实现。

基线为 Mesa `f3c7ecbfc6d66527f83ab1383cfbcd250e488a22`。上游说明见 [README.rst](README.rst)。这是个人实验分支，与 Mesa 官方支持渠道独立。
