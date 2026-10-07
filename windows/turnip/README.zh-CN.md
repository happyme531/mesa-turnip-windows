# 简单使用教程

本驱动目前只在 Windows ARM64、Snapdragon X Elite / Adreno X1-85、高通驱动 `31.0.170.0` 上实测。程序也需要是 ARM64，或能够加载 ARM64 ICD 的 ARM64X 程序。普通 x64 游戏不能据此视为支持。

系统应保留原有高通显示驱动、Vulkan loader 和 ARM64 Visual C++ 运行库。驱动包不包含高通 DLL；GSL 从已安装驱动目录加载。

## 下载并运行

1. 在仓库 Actions 中打开成功的 **Turnip Windows ARM64** 构建。
2. 下载 `turnip-windows-arm64-<commit>` artifact，解压后再解压里面的驱动 ZIP。
3. 在解压目录用 PowerShell 7 运行以下命令。

```powershell
# Vulkan SDK 的 ARM64X 立方体，按自己的 SDK 路径修改
.\Run.ps1 -Program 'C:\VulkanSDK\1.4.328.1\Bin\vkcube.exe'

# 运行自己的 ARM64 Vulkan 程序
.\Run.ps1 -Program 'C:\Apps\example-arm64.exe' -ProgramArguments @('--example')

# 强制 GMEM，或退回早期的无压缩模式
.\Run.ps1 -Program 'C:\Apps\example-arm64.exe' -Mode gmem
.\Run.ps1 -Program 'C:\Apps\example-arm64.exe' -Mode safe
```

默认 `auto` 使用 Turnip 自己的渲染路径选择，UBWC 开启。`sysmem` 保留 UBWC 但禁用 GMEM；`safe` 对应 `TU_DEBUG=sysmem,noubwc`。这些设置只作用于当前程序进程，不注册全局 ICD。关闭后按平常方式启动程序即可使用系统驱动。

Minecraft 需要支持原生 Vulkan 的版本、ARM64 Java 和相应 ARM64 LWJGL 库，并通过原有启动器选择 Vulkan。可以用 `Run.ps1 -Program 'C:\Minecraft\start.bat'` 包装一个**尚未固定其他 ICD** 的原始启动脚本。不要复制账号参数或世界文件进本仓库。

## 从源码构建

需要 Windows ARM64、PowerShell 7、Python 3.12/3.13、Git、Ninja，以及 Visual Studio 2026 的 ARM64 C++、Windows SDK 和 Clang 工具。脚本自动创建专用 Python 环境、安装固定版本的构建依赖，下载并校验 WinFlexBison、glslang、zlib 和 Microsoft DirectX-Headers。glslang 使用 Khronos 的 Windows x64 构建工具，在 ARM64 Windows 上经系统兼容层运行；输出驱动和 CPU 测试仍是原生 ARM64。首次构建需要联网。

```powershell
git clone --depth 1 --branch turnip-windows-gsl-dev https://github.com/happyme531/mesa-turnip-windows.git
cd mesa-turnip-windows
pwsh -NoProfile -File .\windows\turnip\Build.ps1
```

输出在 `dist-turnip-windows` 和 `dist-turnip-windows.zip`。构建脚本运行 IR3 汇编/反汇编、延迟模型及 256 个格式值的 CPU 检查；这些检查不会提交 GPU 工作。Vulkan SDK 不是构建驱动的必需依赖，但其中的 `vkcube`、validation layer 和工具有助于设备实测。

该移植的提交仍然同步等待，窗口经过 GPU 读回与 GDI/DWM 呈现。不要把它当作系统驱动替代品或已完成的性能优化。完整验证范围和已知限制见仓库的 `windows/turnip/NOTES.md`。

AI provenance: Generated-by: LLM (biblioklept).
