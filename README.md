# Mesa Turnip for Windows ARM64

[![Proudly Vibe Coded - Plasma Mix](https://vibecoded.fyi/badges/flat/main/proudly-vibe-coded-plasma-mix.svg)](https://vibecoded.fyi/)
[![Coded with Codex](https://vibecoded.fyi/badges/flat/agents/codex.svg)](https://vibecoded.fyi/)

将 Mesa Turnip 移植到 Windows ARM64，在 Snapdragon / Adreno GPU 上运行
Vulkan 程序。Turnip 编译 shader 并生成 GPU 命令，Windows GSL 后端通过
系统已有的高通 `libgsluser.dll` 和内核驱动提交工作。

[下载驱动](https://github.com/happyme531/mesa-turnip-windows/releases) ·
[CI 构建](https://github.com/happyme531/mesa-turnip-windows/actions/workflows/turnip-windows-arm64.yml) ·
[使用教程](windows/turnip/README.zh-CN.md) ·
[移植说明](windows/turnip/NOTES.md)

## 下载与运行

从 Releases 下载驱动 ZIP，或下载成功的 **Turnip Windows ARM64** Actions
构建产物。解压后在包目录使用 PowerShell 7 启动 ARM64 Vulkan 程序：

```powershell
.\Run.ps1 -Program 'C:\VulkanSDK\1.4.328.1\Bin\vkcube.exe'
.\Run.ps1 -Program 'C:\Apps\example-arm64.exe' -ProgramArguments @('--example')
```

启动脚本只为本次程序选择 ICD，默认启用 UBWC 并自动选择渲染路径。
`-Mode sysmem`、`-Mode gmem` 和 `-Mode safe` 可用于对照。系统需要保留
高通显示驱动、Vulkan loader 和 ARM64 Visual C++ 运行库。

## 从源码构建

需要 Windows ARM64、PowerShell 7、Python 3.12/3.13、Git、Ninja，以及
Visual Studio 2026 的 ARM64 C++、Windows SDK 和 Clang 工具。在源码目录
运行：

```powershell
pwsh -NoProfile -File .\windows\turnip\Build.ps1
```

脚本准备固定版本的构建依赖，编译原生 ARM64 ICD，运行 Mesa CPU 测试和
256 个硬件格式值检查。输出为 `dist-turnip-windows` 和
`dist-turnip-windows.zip`。首次构建需要联网。

推送 `v*` 版本标签会运行构建，并生成附有驱动包的预发布草稿；在
Adreno 设备验证产物后发布草稿。

## 已测范围

实测环境为 Snapdragon X Elite / Adreno X1-85、高通驱动 `31.0.170.0`。

| 范围 | 结果 |
| --- | --- |
| CPU | 30 项 Mesa 测试及 256 个格式值检查通过 |
| GPU | Fill、compute、triangle、GMEM、UBWC 和同步压力测试通过 |
| 窗口 | Win32 swapchain、900 帧纹理立方体及实际 resize 通过 validation |
| 游戏 | Minecraft Java 26.3 的原生 Vulkan 后端已进入实际世界 |

CI 执行构建与 CPU 测试，GPU 和窗口结果来自本机实测。性能数据、可选
同步及 shader 控制见[移植说明](windows/turnip/NOTES.md)。

## 已知限制

这是实验性的用户态移植，依赖已安装的高通 GSL 和内核驱动。
窗口通过 GPU 读回及 GDI/DWM 呈现。普通 x64 应用、ARM64EC/ARM64X ICD、
直接 DXGI 共享呈现、广泛应用兼容性和 Vulkan CTS 尚未验证。

Mesa 基线为 `f3c7ecbfc6d66527f83ab1383cfbcd250e488a22`，版本为
26.3.0-devel。上游项目说明见 [README.rst](README.rst)。
