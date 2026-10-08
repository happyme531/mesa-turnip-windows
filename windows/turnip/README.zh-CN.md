# 使用与构建

本移植在 Windows ARM64、Snapdragon X Elite / Adreno X1-85、高通驱动
`31.0.170.0` 上实测。系统需要保留高通显示驱动、Vulkan loader 和
ARM64 Visual C++ 运行库。GSL 从系统驱动目录加载。

## 运行

解压驱动包，在包目录用 PowerShell 7 运行 ARM64 Vulkan 程序：

```powershell
.\Run.ps1 -Program 'C:\VulkanSDK\1.4.328.1\Bin\vkcube.exe'
.\Run.ps1 -Program 'C:\Apps\example-arm64.exe' -ProgramArguments @('--example')
```

默认 `auto` 使用 Turnip 的渲染路径选择，启用 UBWC。`-Mode sysmem`
保留 UBWC 并禁用 GMEM；`-Mode gmem` 强制 GMEM；`-Mode safe` 使用
`sysmem,noubwc`。设置只作用于本次程序进程。

需要比较 pending 同步和呈现线程时：

```powershell
.\Run.ps1 -Program 'C:\Apps\example-arm64.exe' -PendingSync -ThreadedWsi
```

Minecraft 需要支持原生 Vulkan 的版本、ARM64 Java 和 ARM64 LWJGL，
并由原启动器选择 Vulkan。运行 VS preamble 对照：

```powershell
$env:IR3_SHADER_DEBUG = 'novspreamble'
.\Run.ps1 -Program 'C:\Minecraft\start.bat' -PendingSync -ThreadedWsi
Remove-Item Env:IR3_SHADER_DEBUG
```

## 构建

需要 Windows ARM64、PowerShell 7、Python 3.12/3.13、Git、Ninja、
Visual Studio 2026 的 ARM64 C++、Windows SDK 和 Clang 工具。
在源码目录运行：

```powershell
pwsh -NoProfile -File .\windows\turnip\Build.ps1
```

脚本准备固定版本的 Python 依赖、WinFlexBison、glslang、zlib 和
DirectX-Headers，编译原生 ARM64 ICD，运行 IR3、XML 和格式 CPU 检查。
首次构建需要联网。glslang 使用 Khronos 的 Windows x64 构建工具。

输出为 `dist-turnip-windows` 和 `dist-turnip-windows.zip`。
Vulkan SDK 可提供设备验证所需的 vkcube 和 validation layer。

## 已知限制

程序需要能够加载 ARM64 ICD；普通 x64 游戏尚不支持。窗口经过 GPU
读回与 GDI/DWM 呈现。pending、呈现线程及分阶段 preamble 控制为可选
设置，长期稳定性和 Vulkan CTS 尚未验证。现有测试结果见
[移植说明](NOTES.md)。
