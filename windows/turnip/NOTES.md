# 移植与验证记录

更新：2026-10-08。目标为 Windows ARM64、Snapdragon X Elite / Adreno X1-85，当前实测的高通驱动是 `31.0.170.0`。芯片 ID `0x43050c01`，GMEM 3 MiB。上游基线为 `f3c7ecbfc6d66527f83ab1383cfbcd250e488a22`，Mesa 26.3.0-devel。

## 已实现

- `tu_knl_gsl.cc`：Windows GSL 设备、上下文、内存、队列和 Windows event 同步。时间线信号量使用 Mesa 的已有仿真实现。
- `tu_gsl.h`：通过导出函数加载系统 GSL，构造驱动私有分配与提交数据，等待并核实 GPU 完成标记。
- `tu_win32_compat.h` 及相关平台分支：Windows 线程、文件、时间与构建兼容。
- Win32 WSI：GPU 图像读回至 CPU 可见缓冲区，GDI 呈现，FIFO 使用 `GdiFlush` / `DwmFlush`。呈现 ID、等待、时间统计功能未开放。
- MSVC ABI 修复：IR3 shuffle 的三位枚举位域改为无符号整数字段；硬件图像格式使用完整 enum，避免 8 位有符号截断。

## GMEM 提交修复

此前把所有顶层命令放入额外的 `CP_INDIRECT_BUFFER` 封装，改变了 Turnip 原本的 IB 层级。小图 GMEM 可以通过；大图关闭硬件分箱后第一帧通过、重复提交失败。取消分箱不能独立解决问题。

诊断复制顶层命令、消除额外层级后，重复 GMEM 和硬件分箱均通过。但复制会影响 GPU 动态更新原命令地址，因此没有保留为正式实现。

离线检查当前 Windows 内核驱动，确认私有命令项的步长为 **24 字节（6 DWORD）**，此前多项试验使用了错误的 16 字节步长。修复 `81c50cf5038ae9141318570343b550f9a6393d91` 按正确的列表格式直接提交原顶层命令，末尾追加独立完成标记命令项。原 GPU 地址、动态更新机制和硬件命令层级得以保留。

私有提交 blob 总长为 `148 + 24*N` 字节；DWORD 25 是项数，第 i 项的 DWORD 数在 `36+6*i`、地址在 `37+6*i` / `38+6*i`。GSL 导出函数接收的 32 字节 command descriptor 与这份 24 字节 Windows 私有项是两种结构。只有一项时 blob 仍是 172 字节。

GSL wait 曾在未完成执行时返回 0，所以当前仍检查实际 GPU 写入的完成序号，缺失则报告 device lost。这些 ABI 观察仅针对已测驱动版本；没有在内部固定地址打补丁，也没有修改内核驱动。

## 当前验证

| 用例 | 结果 |
| --- | --- |
| Vulkan fill / compute | 64 KiB，16384 个输出零差异；compute 含共享内存和 barrier |
| 时间线信号量 | 主机/GPU signal、重复 GPU wait 与 counter 正确 |
| 小图三角形 | 256×256，61420 个非边缘像素零差异 |
| 大图 GMEM | 2944×1699、16 次 draw、500 个测量帧，加至少 1500 ms 预热；4688048 个像素零差异 |
| 多份命令 | `SIMULTANEOUS_USE`，一次提交四份，100 帧及预热通过 |
| 格式 / shader | 奇数尺寸、RGBA32F、128 次整数 ALU shader，100 帧及预热通过 |
| Win32 cube | 纹理和深度，强制 GMEM / UBWC，900 帧；1312×1038 放大至 1574×1246，通过 validation |
| Minecraft Java 26.3 / Fabric | 已进入实际世界、画面正常；恢复 UBWC 和 GMEM 后，用户仍报告性能没有明显改善 |

以上专项验证使用了 Vulkan validation，所述用例未报告错误；Minecraft 本身未开启 validation。尚无 Vulkan CTS 合格结论。

## 性能调查结论

2944×1699、16 个不透明三角形 draw 的固定离屏程序，预热后测量 500 帧，中位数如下。时间戳区间包含布局转换、缓存处理和可能的调度时间，不是纯 shader 活跃时间。

| 条件 | GPU 渲染 ms | GPU 读回 ms | CPU 提交到完成 ms |
| --- | ---: | ---: | ---: |
| 修复前 Turnip sysmem、UBWC 关闭 | 1.341250 | 0.447135 | 1.961700 |
| 修复前 Turnip sysmem、UBWC 开启 | 0.441406 | 0.324271 | 0.893800 |
| 提交修复后 Turnip sysmem、UBWC 开启 | 0.441510 | 0.319219 | 0.901200 |
| 提交修复后 Turnip GMEM、UBWC 开启 | 0.453073 | 0.305417 | 0.896100 |
| 同轮系统高通驱动 | 0.426510 | 0.313854 | 0.797500 |

恢复 UBWC 确实消除了这项用例的大部分 GPU 渲染差距。这个简单场景没有显示 GMEM 额外提速。经 QPC 对齐的另一轮频率采样，两种驱动在测量阶段都观测到 1.25 GHz；这不支持把那轮差距归因于降频，也不代表所有游戏帧始终锁频。

**Minecraft 的问题仍未解决。** 最新实测恢复开关后，用户报告没有明显改善。它与该离屏用例的负载、纹理、深度、shader、几何和提交节奏不同。不得把上述约三倍的离屏改善宣称为游戏改善。

早期的线性 WSI、独立 GDI 呈现线程和完成标记轮询没有观察到明显游戏收益，未放入这个发布分支。提交仍同步等待；异步提交、完整驻留管理、直接 DXGI 共享呈现、GMEM input attachment / 自定义 resolve、多队列和更广泛的应用验证仍待完成。

两处静态初始化写入 `TPL1_DBG_ECO_CNTL1` (`0xb602`) 与 `RB_UNKNOWN_8E79` (`0x8e79`) 会使当前 Windows 命令流中断，GSL 后端仅过滤自己的配置副本。其精确内核限制尚未确认。此前失败测试伴随用户观察到的短暂停顿，未找到标准 Display 4101 事件；不能直接认定 TDR。

## CI 范围

CI 使用 GitHub 的 Windows 11 / VS2026 ARM64 runner，编译原生 ARM64 ICD，运行 IR3 与格式 CPU 回归并上传可移动的驱动包。没有高通 GPU / GSL 硬件测试、游戏测试或 CTS。高通专有 DLL、内核文件、账号数据、游戏文件、原始跟踪和反编译代码没有包含在发布内容里。

本分支为个人实验；AI 生成的代码提交带 `Generated-by: LLM`。biblioklept.
