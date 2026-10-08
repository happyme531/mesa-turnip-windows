Native Windows ARM64 Turnip for Snapdragon X Elite / Adreno X1-85.

- GSL submission uses Mesa's worker with CPU signals ordered after timeline
  point installation.
- Optional pending synchronization preserves binary semaphore payloads
  through moves. An optional GDI worker processes FIFO presentation.
- IR3 adds vertex and fragment preamble debug controls without changing
  normal compiler defaults.
- Windows threading uses Mesa's C11 helpers. The driver package includes a
  relative ICD manifest, a process-local launcher and usage notes.

CPU validation covers 30 Mesa tests and all 256 hardware format values.
On-device validation covers fill, compute, triangle, timeline signals,
2000-submit and binary semaphore stress, and 900 cube frames with resize.
The fixed Minecraft replay passes validation; the vertex control preserves
the default Turnip image.

## Known limitations

The tested Qualcomm driver is `31.0.170.0`. This port relies on the installed
GSL library and kernel driver. Presentation includes GPU readback and GDI/DWM.
Pure x64 applications, an ARM64EC/ARM64X ICD and Vulkan CTS remain unverified.
The optional pending, GDI-worker and shader controls default off. CI runs
CPU tests; GPU validation requires an Adreno device.

[Usage and build instructions](https://github.com/happyme531/mesa-turnip-windows/blob/turnip-windows-gsl-dev/windows/turnip/README.zh-CN.md)
