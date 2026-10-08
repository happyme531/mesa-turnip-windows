# Turnip Windows ARM64

Windows ARM64 的 GSL 后端已在 Snapdragon X Elite / Adreno X1-85 上运行
Vulkan fill、compute、GMEM、UBWC、Win32 swapchain 和 Minecraft Java 的
原生 Vulkan 后端。实测高通驱动版本为 `31.0.170.0`，芯片 ID 为
`0x43050c01`，GMEM 为 3 MiB。Mesa 基线为
`f3c7ecbfc6d66527f83ab1383cfbcd250e488a22`，版本为 26.3.0-devel。

## 实现

`tu_knl_gsl.cc` 管理设备、上下文、BO、队列和 Windows event 同步，
时间线信号量复用 Mesa 的仿真实现。主机提交使用 Mesa 既有工作线程；
GPU 退休及完成标记检查在该线程上等待。CPU 信号在时间线 point 安装后
发出。

`tu_gsl.h` 加载系统驱动目录的 `libgsluser.dll`。私有提交数据大小为
`148 + 24*N` 字节，DWORD 25 为项数，第 i 项的长度与地址位于
`36+6*i`、`37+6*i`、`38+6*i`。原始顶层 IB 直接提交，末尾追加独立的
完成标记命令；提交完成需要 GSL wait 和 GPU 写回序号同时满足。

线程同步使用 Mesa 的 C11 和单调时钟条件变量。IR3 shuffle 使用
无符号位域，硬件格式使用完整 enum，避免 MSVC ABI 的符号扩展。

Win32 WSI 将 GPU 图像复制到 CPU 可读缓冲区，再由 GDI 呈现。
FIFO 使用 `GdiFlush` 和 `DwmFlush`。可选呈现线程按序处理图像，
图像 fence 完成后才读回，销毁 swapchain 时排空并等待线程退出。

## 运行控制

| 设置 | 默认 | 用途 |
| --- | --- | --- |
| `TU_GSL_LIBRARY` | 从显示驱动注册表定位 | 指定 GSL DLL 路径 |
| `TU_GSL_ASYNC_SUBMIT` | 1 | 使用 Mesa 提交线程；0 用于同步对照 |
| `TU_GSL_GENERIC_CLEAR` | 1 | 在已测 X1-85 上使用 generic clear |
| `TU_AUTOTUNE_ALGO` | 已测 GSL 芯片使用 profiled | 可指定 bandwidth 对照 |
| `TU_GSL_PENDING_SYNC` | 0 | 提交成功后通知 pending，完成仍等待 GPU |
| `TU_GSL_THREADED_WSI` | 0 | 使用独立的 GDI FIFO 呈现线程 |
| `IR3_SHADER_DEBUG=novspreamble` | 关闭 | 禁用 VS 可选 preamble 优化及预取 |
| `IR3_SHADER_DEBUG=nofspreamble` | 关闭 | 禁用 FS 可选 preamble 优化及预取 |

pending 模式在提交前复制信号句柄，使 binary payload 移动后仍能向
原始对象发出完成信号。分阶段 preamble 控制保留必需的 UBO 和
push constant lowering，不改变正常编译默认值。

## 已有验证

| 用例 | 结果 |
| --- | --- |
| Fill / compute | 16384 个输出正确，compute 含共享内存及 barrier |
| Triangle | 61420 个非边缘像素正确 |
| Timeline / binary | 重复 wait、counter、2000 次提交及 2000 对 GPU fill 正确 |
| GMEM / UBWC | 大图、奇数尺寸、RGBA32F、四份 simultaneous command buffer 正确 |
| Win32 cube | 900 帧及实际窗口放大通过 validation |
| Bonza4X | 1080p 与系统驱动仅 10 个像素差一个 RGB 色阶 |
| Minecraft 固定重放 | VS preamble 控制通过 validation，与默认 Turnip 图像一致 |

同一 DLL 的六段固定重放按系统驱动、默认、VS 控制、VS 控制、默认、
系统驱动排列，帧间隔中位数为
`9.889 / 13.723 / 10.314 / 10.313 / 13.774 / 9.953 ms`。
每段测量 100 帧，采样频率为 1.25 GHz。关闭 VS 可选优化减少约 25%
帧耗时；这些间隔包含重放解码和主机同步。

使用独立 DLL、pending 同步、呈现线程和 VS 控制的实际游戏观察，
用户报告约 75 FPS，系统驱动约 88 FPS。这是相近位置和视角的用户
读数，没有固定天气、时间或锁定频率。

CI 编译原生 ARM64 驱动并运行 CPU 测试。GPU 结果来自本机实测。

## 已知限制

GSL 的私有 ABI 仅在上述驱动版本验证。呈现包含 GPU 读回和 GDI/DWM
等待。纯 x64 应用、ARM64EC/ARM64X ICD、直接 DXGI 共享呈现、完整
驻留管理、GMEM input attachment、自定义 resolve、CTS 和广泛应用
兼容性尚未验证。

两处静态寄存器写入 `TPL1_DBG_ECO_CNTL1` 和 `RB_UNKNOWN_8E79` 在已测
Windows 驱动上使命令流中断，后端过滤自己的设备配置副本。
其具体内核限制尚未确认。

VS preamble 控制改变优化与 uniform/descriptor 处理，现有结果尚未
确定可供所有应用使用的编译器 heuristic。pending、呈现线程和分阶段 preamble 控制默认关闭。
