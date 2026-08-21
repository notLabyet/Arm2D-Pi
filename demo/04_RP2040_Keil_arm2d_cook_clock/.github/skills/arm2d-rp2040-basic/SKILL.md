---
name: arm2d-rp2040-basic
description: "用于理解、构建、调试或扩展本 RP2040 Keil Arm-2D 工程。该工程是 Arm-2D 默认显示适配器在 ST7789 屏幕上成功移植的最基础案例，包含 PIO/DMA 异步刷新。"
argument-hint: "描述要验证的 Arm-2D、显示适配器、ST7789 或 DMA 刷新问题"
user-invocable: true
---

# RP2040 基础 Arm-2D 移植案例

本工程是原生 Arm-2D 默认显示适配器成功移植到 RP2040 与 ST7789 屏幕的最基础案例。

不要将它视为 LDGUI、GuiEasyEditor 或业务 GUI 工程。它的用途是作为后续 Arm-2D 场景、控件和板级功能开发的最小稳定基线。

## 最小运行链路

`main.c` 中的顺序不可改变：

1. `platform_init()` 初始化 RP2040 时钟、性能计数器和 ST7789。
2. `arm_2d_init()` 初始化 Arm-2D 核心。
3. `disp_adapter0_init()` 初始化 Arm-2D 默认显示适配器。
4. 主循环持续调用 `disp_adapter0_task()`。

默认首屏由 `project/mdk/RTE/Acceleration/arm_2d_disp_adapter_0.c` 内置的 busy-wheel 测试流程绘制，不依赖业务场景文件。

## 关键文件

- `main.c`：最小 Arm-2D 入口与主循环。
- `platform/pi_platform.c`：平台初始化及 Arm-2D 到屏幕的回调。
- `platform/st7789_simple.c`：ST7789 的并口 PIO/DMA 底层驱动。
- `project/mdk/RTE/Acceleration/arm_2d_cfg.h`：Arm-2D 核心配置。
- `project/mdk/RTE/Acceleration/arm_2d_disp_adapter_0.h`：显示适配器配置。
- `project/mdk/RTE/Acceleration/arm_2d_disp_adapter_0.c`：默认适配器和 PFB 绘制流程。
- `project/mdk/template.uvprojx`：Keil 工程与 Arm-2D RTE 组件配置。

## 已启用的异步刷新

本工程使用 PIO/DMA 异步刷新，配置项为：

```c
#define __DISP0_CFG_ENABLE_ASYNC_FLUSHING__ 1
```

完整调用链：

1. Arm-2D 显示适配器调用 `__disp_adapter0_request_async_flushing()`。
2. 平台层调用 `st7789_draw_bitmap_async()` 发起 PIO/DMA 传输。
3. DMA IRQ 完成后，`st7789_simple.c` 调用 `st7789_insert_async_flush_cpl_evt_handler()`。
4. 平台层调用 `disp_adapter0_insert_async_flushing_complete_event_handler()`，通知 Arm-2D 可以继续渲染。

不要只修改 `__DISP0_CFG_ENABLE_ASYNC_FLUSHING__`。若开启它，以上两个平台回调必须同时存在，否则会出现链接错误。

## 开发规则

1. 优先保持本工程的启动链路可构建，再添加新场景或业务代码。
2. 新建原生场景时，遵循 Arm-2D 命名风格：`arm_2d_scene_<name>.c/.h`、`user_scene_<name>_t`、`__arm_2d_scene_<name>_init` 和 `__pfb_draw_scene_<name>_handler`。
3. 通过 `RTE/Acceleration` 管理 Arm-2D 显示适配器。不要在 Keil 的手工文件组中再次加入 `arm_2d_disp_adapter_0.c`，否则会导致 `DISP0_ADAPTER` 重复定义。
4. 默认不使用虚拟资源：保持 `__DISP0_CFG_VIRTUAL_RESOURCE_HELPER__` 为 `0`，除非同时实现对应的资源地址和读取回调。
5. 保持 `Disp0_DrawBitmap()` 可用，作为同步刷新和调试时的基础屏幕回调。

## Keil 构建

工程文件：`project/mdk/template.uvprojx`。

本机 Keil 路径：

```powershell
& 'E:\Keil_v5\UV4\UV4.exe' -b '.\project\mdk\template.uvprojx' -j0
```

需要修改显示适配器配置后，执行全量重建：

```powershell
& 'E:\Keil_v5\UV4\UV4.exe' -r '.\project\mdk\template.uvprojx' -j0
```

检查 `project/mdk/Objects/template.build_log.htm` 中是否有：

```text
".\Objects\template.axf" - 0 Error(s), 0 Warning(s).
```

成功构建后，生成的固件为 `project/mdk/template.uf2`。

## 完成检查

- `main.c` 仍调用 `arm_2d_init()`、`disp_adapter0_init()` 和 `disp_adapter0_task()`。
- `__DISP0_CFG_ENABLE_ASYNC_FLUSHING__` 与平台异步回调保持一致。
- `template.uvprojx` 没有重复编译 `arm_2d_disp_adapter_0.c`。
- Keil 构建日志为 `0 Error(s), 0 Warning(s)`。