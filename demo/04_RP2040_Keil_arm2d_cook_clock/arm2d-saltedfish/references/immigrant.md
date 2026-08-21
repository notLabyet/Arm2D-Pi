---
name: arm2d-mdk-rte
description: Use when deploying, configuring, or debugging Arm-2D in embedded projects, especially Keil/MDK projects that use Arm CMSIS-Pack and RTE. Covers Arm-2D Pack/RTE integration, display adapter setup, PFB configuration, dirty-region refresh, synchronous/asynchronous flushing, DMA/display porting, demo/scene integration, and build-download-runtime verification. 用于在嵌入式工程中部署、配置、调试 Arm-2D，尤其适合通过 Keil/MDK、CMSIS-Pack、RTE 接入 Arm-2D 的项目，覆盖 Pack/RTE 部署、显示适配器、PFB、脏矩形、同步/异步刷新、DMA/显示端口、demo/scene 接入和编译下载运行验证。
---

# Arm-2D Deployment With MDK/RTE

本 skill 用于把 Arm-2D 以 Pack/RTE 的方式部署到嵌入式工程中，并完成显示适配、刷新路径、demo 接入和性能验证。内容保持工程无关，不绑定具体芯片、屏幕、下载器、目录或项目参数。

## 中文版

### 适用场景

使用本 skill 处理这些任务：

- 在 Keil/MDK 工程里通过 CMSIS-Pack/RTE 添加 Arm-2D。
- 配置 `arm_2d_cfg.h` 和 display adapter。
- 为 LCD/OLED/RGB/MIPI/Framebuffer 等显示设备接入 Arm-2D。
- 配置 PFB、dirty region、同步/异步 flushing。
- 将 Arm-2D benchmark、官方 demo 或自定义 scene 接入应用。
- 分析 Arm-2D 帧率、刷新耗时、DMA/display port 性能瓶颈。

### 部署原则

优先使用 Pack/RTE 集成 Arm-2D，而不是手动复制整套 Arm-2D 源码。

- 保持 Arm-2D 组件可在 MDK 的 Manage Run-Time Environment 里勾选和升级。
- 不直接修改 Pack 安装目录里的文件。
- 工程定制代码放在工程本地，例如 `RTE/`、`board/`、`bsp/`、`drivers/`、`applications/`。
- 把显示硬件相关逻辑隔离到 display port/display adapter，应用层只调用 Arm-2D scene/demo 接口。

### Pack/RTE 接入步骤

1. 安装 Arm-2D Pack。
2. 如果是重新部署或切换 Arm-2D Pack/RTE 版本，先清空工程里的 `RTE/Acceleration` 目录，再让 MDK/RTE 重新生成。
3. 在 MDK 的 RTE 配置界面勾选 Arm-2D 相关组件。移植 Arm-2D 工程时，除非用户明确要求最小化组件，否则 Arm-2D 组件可以全部加入；Arm-2D/编译器/链接器会在编译和链接阶段自行裁剪未使用代码，不要为了“看起来干净”过早少选组件导致后续缺头文件、缺 helper 或缺 scene/demo 支持。
4. 如果用户工程明显使用了操作系统/RTOS（例如存在 RT-Thread、FreeRTOS、CMSIS-RTOS、线程、信号量、互斥锁、定时器任务等痕迹），在 RTE 中勾选 Arm-2D 的 RTOS Helper。
5. 确认工程里生成或引用了 Arm-2D 配置文件。
6. 确认 include path 包含 RTE 生成目录、Arm-2D 配置目录、显示适配层目录和应用目录。
7. 在工程本地维护 Arm-2D 配置，不修改 Pack 原始文件。

清空 `RTE/Acceleration` 的目的是避免旧版本 RTE 生成文件、旧配置头文件、旧组件引用和新 Pack 混在一起。清空前如果里面有手写的工程配置，先迁移或备份到工程自己的 board/application 配置位置。

常见工程文件：

```text
RTE/Acceleration/arm_2d_cfg.h
RTE/Acceleration/arm_2d_disp_adapter_0.h
board/arm2d/
applications/
```

Arm-2D 基础适配参数主要集中在：

- `arm_2d_disp_adapter_0.h`：display adapter、屏幕尺寸、PFB/display 刷新相关配置通常在这里。
- `arm_2d_cfg.h`：Arm-2D 全局配置、helper、dirty region、PFB/内存策略等通常在这里。

移植时必须重点核对 PFB 大小与屏幕分辨率是否匹配。PFB block 过小会导致切块和 flush 次数过多，调度开销变大；PFB block 过大则会占用过多 SRAM，并可能削弱 dirty region 的灵活性。不要照搬原工程的 PFB 参数，必须结合目标屏幕分辨率、像素格式、SRAM 容量、DMA/display 接口效率重新确认。

实际路径可以不同，关键是区分：

- Pack 提供通用 Arm-2D 源码和组件。
- RTE/工程本地文件提供配置。
- board/driver 层提供屏幕 flush、DMA、cache、同步回调。
- application 层提供 scene/demo。

### 初始化流程

典型启动顺序：

```c
arm_2d_init();
disp_adapter0_init();
```

之后在主循环或 RTOS 线程中持续驱动 display adapter：

```c
while (1) {
    (void)__disp_adapter0_task();
}
```

如果使用 RTOS，benchmark 或渲染线程里不要随手加入额外 delay/yield，除非它是调度策略的一部分。测性能时应先让渲染循环保持清晰、稳定。

### Display Adapter 配置

display adapter 至少需要明确：

- 屏幕宽高。
- 像素格式，例如 RGB565、RGB888、ARGB8888。
- PFB block 宽高。
- PFB 数量或 heap size。
- dirty region 是否启用。
- flush 是同步还是异步。
- 是否需要 RGB565 高低字节交换。
- cache clean/invalidate 策略。

配置时先保证正确显示，再优化性能。

### PFB 配置

PFB 是 Arm-2D 的分块渲染缓冲。调 PFB 时关注这些点：

- PFB 太小：切块数量多，调度和 flush 开销变大。
- PFB 太大：内存占用大，dirty region 裁剪可能不够灵活。
- PFB 尺寸应结合屏幕分辨率、缓存、DMA 对齐、总线效率和 scene 类型实测。
- 如果配置项支持动态 PFB，需要确认手动设置是否真的生效。
- PFB 对齐参数可能改善 DMA/display 写入效率，也可能扩大实际渲染区域，需要实测。

不要只看理论，PFB 最终以硬件运行数据为准。

### Dirty Region

dirty region 适合 UI、仪表、动画元素较少或局部变化的 scene。

- 开启后，Arm-2D 可以只刷新变化区域，降低 CPU 渲染和 display flush 工作量。
- 对全屏 shader、全屏视频、整屏滚动等场景，dirty region 优势可能较小。
- 不要假设关闭 dirty region 一定更快；应通过帧率、flush 次数和实际刷新面积验证。

验证 dirty region 时要同时看：

- FPS/frame time。
- 每帧 flush 次数。
- 实际 flush 像素数量或区域大小。
- 是否出现区域不刷新、残影或误判静止。

### 同步与异步刷新

同步 flush：

- 实现简单，行为确定。
- 适合单 PFB 或 flush 很快的场景。
- 如果 display flush 时间很短，同步方式可能更高效。

异步 flush：

- 目标是让 CPU 准备下一块 PFB，同时 DMA/display 外设刷新上一块 PFB。
- 通常需要至少两个 PFB buffer。
- 必须正确处理 flush complete、buffer release、cache 和并发状态。
- 如果只有一个 PFB，异步刷新通常无法真正并行，反而可能增加回调/中断/状态机开销。

是否开启异步刷新，应通过硬件数据判断，而不是按名字判断。

### 显示 Flush Port

display flush port 是 Arm-2D 和硬件屏幕之间的关键层。

常见实现：

- CPU copy 到 framebuffer。
- DMA2D/2D accelerator copy 到 framebuffer。
- SPI/QSPI/MIPI/8080 等接口发送区域数据。
- LTDC/RGB 屏幕使用 framebuffer，flush 只更新 framebuffer 对应区域。

实现 flush port 时要确认：

- 源 PFB 地址、目标 framebuffer 地址和 stride 正确。
- 像素格式匹配。
- RGB565 高低字节顺序正确。
- cache clean/invalidate 正确。
- DMA completion callback 或同步等待逻辑可靠。
- 错误标志可以被统计或打印。

如果有 2D DMA/GPU/accelerator，应优先让硬件做大块搬运或格式转换，CPU 负责 scene 逻辑和必要的软件渲染。

### Demo 和 Scene 接入

Arm-2D demo 应放在应用层，和显示端口解耦。

常见文件组织：

```text
applications/arm2d_demo.c
applications/arm2d_demo.h
applications/arm2d_scene_xxx.c
applications/arm2d_scene_xxx.h
```

scene 代码通常包含：

```c
#include "arm_2d.h"
#include "arm_2d_helper.h"
#include "arm_2d_disp_adapter_0.h"
```

Arm-2D scene 渲染回调常见形式：

```c
arm_fsm_rt_t fnScene(void *pTarget,
                     const arm_2d_tile_t *ptTile,
                     bool bIsNewFrame);
```

自定义 scene 要注意：

- 用 Arm-2D 的 tile/region API 获取当前 PFB 的有效区域。
- 不要假设每次都渲染整屏。
- 需要全屏动画时，显式启用 full-frame refresh 或每帧标记 dirty。
- benchmark、官方 demo、自定义 demo 应能互相替换，不要把显示驱动写死在 demo 里。

### Arm-2D API 理解与避坑

不要凭直觉猜 Arm-2D API。先确认当前 Pack 版本里的头文件和 demo，再写适配代码。

核心概念：

- `arm_2d_tile_t` 表示一块二维像素区域，可以是 framebuffer、PFB、子图、图片资源或任意渲染目标。
- `arm_2d_region_t` 表示 tile 内的矩形区域，包含 location 和 size。
- `arm_2d_location_t` 表示二维坐标。
- `arm_fsm_rt_t` 是 Arm-2D 常见的异步/状态机返回值，不能简单当作 `void` 或 `bool`。
- `arm_2d_scene_t` 是 scene player 使用的场景基类，用户 scene 通常通过 `implement(arm_2d_scene_t);` 嵌入它。

scene 回调常见签名是：

```c
arm_fsm_rt_t fnScene(void *pTarget,
                     const arm_2d_tile_t *ptTile,
                     bool bIsNewFrame);
```

注意第一个参数通常是 `void *pTarget`，不是 `arm_2d_scene_t *`。如果需要访问自定义 scene 对象，要按当前 demo/框架写法从 target 或容器关系中取得，不要硬猜。

从 tile 取得当前 PFB 有效区域和像素地址时，优先使用 Arm-2D 提供的 helper/API，不要自己用裸指针猜偏移：

```c
arm_2d_location_t tOffset;
arm_2d_region_t tValidRegion;
uint16_t *phwBuffer;

arm_2d_get_address_and_region_from_tile(ptTile,
                                        &tValidRegion,
                                        &tOffset,
                                        uint16_t,
                                        phwBuffer);
```

写像素时要分清：

- 当前 PFB 的有效区域 `tValidRegion`。
- 当前 PFB 在完整屏幕里的偏移 `tOffset`。
- 目标 tile 的 stride。
- 像素格式和像素类型，例如 `uint16_t` 对 RGB565。

常见错误：

- 误以为每次 scene 回调拿到的都是整屏 framebuffer。
- 忽略 PFB offset，导致每个 block 都从 `(0,0)` 的逻辑坐标开始画。
- 用 region 宽度当 stride，但实际 stride 应按目标 tile/framebuffer 的行宽计算。
- 没有处理 `bIsNewFrame`，导致动画时间、状态更新或 dirty 标记异常。
- 没有 full-frame refresh，过程动画或 shader 只画一帧后不再刷新。
- 把 Arm-2D 的异步返回值当同步函数处理。

对全屏动画、shader、实时仪表这类每帧变化的内容，应显式启用 full-frame refresh 或确保每帧都有 dirty region：

```c
arm_2d_helper_pfb_full_frame_refresh_mode(&DISP_ADAPTER.use_as__arm_2d_helper_pfb_t,
                                          true);
```

其中 `DISP_ADAPTER` 应替换为当前工程实际的 display adapter 对象。

对 scene/player/demo 的建议：

- 先参考 Pack 自带 demo 的 scene 初始化和 append 流程。
- 不要绕过 scene player 直接乱调内部成员，除非当前版本文档或 demo 明确这样做。
- 每次换 Arm-2D Pack 版本，都重新核对函数签名和结构体字段。
- 自定义 demo 要和 display flush port 解耦，避免 demo 直接依赖某个屏幕驱动。

### 性能验证

性能优化前先建立可观测数据。

建议统计：

- FPS。
- frame time。
- flush time。
- flush count。
- flush pixel count 或区域大小。
- CPU 占用。
- DMA/display 错误数。
- 是否发生 cache、bus、alignment 相关异常。

判断瓶颈：

- flush time 很低但 frame time 很高：通常是 CPU 渲染瓶颈。
- flush time 很高：检查 display 总线、DMA、framebuffer、cache、区域大小。
- FPS 高但 flush count 不增长：可能是假数据或 scene 没有继续刷新。
- 画面残影或局部不更新：检查 dirty region、tile region、full-frame refresh 逻辑。

### 编译下载验证

通用流程：

1. 编译工程。
2. 下载到目标板。
3. 确认程序复位运行。
4. 读取或打印 Arm-2D 运行统计。
5. 对比不同 PFB、dirty、async、DMA/cache 配置。
6. 每次启用 LTO 或改变优化等级后，重新确认符号地址和运行状态。

在 MDK 命令行环境中可以使用 UV4 自动编译下载；在有调试器/探针工具时，可以读取 RAM 统计变量辅助判断。

### 常见问题

- 重新部署 Arm-2D Pack/RTE 前没有清空 `RTE/Acceleration`，导致旧版本生成文件和新组件混用。
- Pack/RTE 组件添加了，但 include path 没有包含工程本地配置目录。
- 修改了 PFB 配置，但动态 PFB 或其他宏覆盖了手动设置。
- RGB565 颜色异常，可能是 BGR/RGB 顺序或高低字节问题。
- DCache 开启后没有 clean/invalidate，导致 DMA/display 读到旧数据。
- 单 PFB 下打开 async flush，以为会自动并行，实际没有收益。
- dirty region 误判导致 scene 跑一帧后停住。
- benchmark 分数和真实应用 FPS 不一致，因为 scene 负载完全不同。
- LTO 后符号地址变化，调试工具读取了旧地址。

---

## English Version

### When To Use

Use this skill for tasks such as:

- Adding Arm-2D to a Keil/MDK project through CMSIS-Pack/RTE.
- Configuring `arm_2d_cfg.h` and a display adapter.
- Connecting Arm-2D to LCD/OLED/RGB/MIPI/framebuffer displays.
- Tuning PFB, dirty regions, synchronous flushing, and asynchronous flushing.
- Integrating Arm-2D benchmarks, official demos, or custom scenes.
- Debugging Arm-2D frame rate, flush time, DMA/display port behavior, and rendering bottlenecks.

### Deployment Principles

Prefer Pack/RTE integration instead of manually copying the whole Arm-2D source tree into the project.

- Keep Arm-2D selectable and upgradable from MDK's Manage Run-Time Environment.
- Do not edit files inside the installed Pack directory.
- Keep project-specific code in local project folders such as `RTE/`, `board/`, `bsp/`, `drivers/`, or `applications/`.
- Isolate display-hardware logic in the display port/display adapter. Application code should use Arm-2D scene/demo APIs.

### Pack/RTE Integration Steps

1. Install the Arm-2D Pack.
2. If redeploying Arm-2D or switching Pack/RTE versions, clear the project's `RTE/Acceleration` directory first, then let MDK/RTE regenerate it.
3. Select the required Arm-2D components in MDK's RTE configuration.
4. Confirm the project has Arm-2D configuration files.
5. Confirm include paths cover the generated RTE folder, Arm-2D configuration folder, display adapter/port folder, and application folder.
6. Keep Arm-2D configuration project-local. Do not patch the Pack source itself.

Clearing `RTE/Acceleration` prevents stale generated files, old configuration headers, and old component references from being mixed with the new Pack version. If the directory contains hand-written project configuration, migrate or back it up into the project's own board/application configuration area first.

Common project files:

```text
RTE/Acceleration/arm_2d_cfg.h
RTE/Acceleration/arm_2d_disp_adapter_0.h
board/arm2d/
applications/
```

The exact paths may differ. The important split is:

- Pack files provide generic Arm-2D source and components.
- RTE/project-local files provide configuration.
- board/driver code provides screen flush, DMA, cache, and synchronization hooks.
- application code provides scenes and demos.

### Initialization Flow

Typical startup sequence:

```c
arm_2d_init();
disp_adapter0_init();
```

Then drive the display adapter from the main loop or an RTOS thread:

```c
while (1) {
    (void)__disp_adapter0_task();
}
```

When measuring benchmark/demo performance, avoid adding unnecessary delay/yield calls unless they are part of the intended scheduling model.

### Display Adapter Configuration

A display adapter should define:

- Screen width and height.
- Pixel format, such as RGB565, RGB888, or ARGB8888.
- PFB block width and height.
- PFB count or heap size.
- Whether dirty-region optimization is enabled.
- Whether flushing is synchronous or asynchronous.
- Whether RGB565 byte swapping is needed.
- Cache clean/invalidate policy.

First make the display correct, then tune performance.

### PFB Configuration

PFB is Arm-2D's partial framebuffer/block rendering buffer. Tune it with these tradeoffs in mind:

- Too small: too many blocks, more scheduling and flush overhead.
- Too large: higher memory usage and sometimes less effective dirty-region clipping.
- PFB size should be measured against screen resolution, cache behavior, DMA alignment, bus efficiency, and scene type.
- If dynamic PFB is enabled, confirm that manual settings are actually taking effect.
- Alignment settings can improve DMA/display efficiency or increase the actual rendered area. Measure both.

Use hardware data as the final judge.

### Dirty Regions

Dirty regions are useful for UI, gauges, small animations, and scenes where only part of the screen changes.

- They can reduce CPU rendering and display flush workload.
- They may help less for fullscreen shaders, video-like content, or full-screen scrolling.
- Do not assume disabling dirty regions is faster. Measure FPS, flush count, and actual flushed area.

When validating dirty regions, check:

- FPS/frame time.
- Flush count per frame.
- Flush pixel count or region sizes.
- Missing updates, ghosting, or a scene that stops after one frame.

### Synchronous And Asynchronous Flushing

Synchronous flushing:

- Is simple and deterministic.
- Works well with one PFB or very fast display flushes.
- Can be faster when flush time is already low.

Asynchronous flushing:

- Tries to let the CPU prepare the next PFB while DMA/display hardware flushes the previous one.
- Usually requires at least two PFB buffers.
- Requires correct flush-complete, buffer-release, cache, and concurrency handling.
- With only one PFB, async flushing usually cannot provide real overlap and may add callback/ISR/state-machine overhead.

Enable async flushing because measurements prove it helps, not because the name sounds faster.

### Display Flush Port

The display flush port is the key bridge between Arm-2D and the physical display.

Common implementations:

- CPU copy into a framebuffer.
- 2D DMA/accelerator copy into a framebuffer.
- SPI/QSPI/MIPI/8080 transfer of the updated region.
- LTDC/RGB display with a framebuffer where flush updates the matching region.

Verify:

- Source PFB address, target framebuffer address, and stride.
- Pixel format compatibility.
- RGB565 byte order.
- Cache clean/invalidate behavior.
- DMA completion callback or synchronous wait logic.
- Error flags or counters.

If a 2D DMA/GPU/accelerator is available, prefer using it for large copies or format conversion while the CPU handles scene logic and required software rendering.

### Demo And Scene Integration

Keep Arm-2D demos in the application layer and decoupled from the display port.

Common organization:

```text
applications/arm2d_demo.c
applications/arm2d_demo.h
applications/arm2d_scene_xxx.c
applications/arm2d_scene_xxx.h
```

Scene files often include:

```c
#include "arm_2d.h"
#include "arm_2d_helper.h"
#include "arm_2d_disp_adapter_0.h"
```

Common Arm-2D scene render callback form:

```c
arm_fsm_rt_t fnScene(void *pTarget,
                     const arm_2d_tile_t *ptTile,
                     bool bIsNewFrame);
```

For custom scenes:

- Use Arm-2D tile/region APIs to get the valid area of the current PFB.
- Do not assume every render pass covers the full screen.
- For fullscreen animation, explicitly enable full-frame refresh or mark the frame dirty every frame.
- Benchmarks, official demos, and custom demos should be replaceable without hardwiring display-driver code into the demo.

### Arm-2D API Notes And Pitfalls

Do not guess Arm-2D APIs from intuition. Check the headers and demos from the active Pack version before writing adapter or scene code.

Core concepts:

- `arm_2d_tile_t` represents a 2D pixel area. It may be a framebuffer, PFB, child tile, image asset, or generic render target.
- `arm_2d_region_t` represents a rectangular area inside a tile, including location and size.
- `arm_2d_location_t` represents a 2D coordinate.
- `arm_fsm_rt_t` is a common Arm-2D async/state-machine return type. Do not treat it as plain `void` or `bool`.
- `arm_2d_scene_t` is the scene base used by the scene player. User scenes commonly embed it with `implement(arm_2d_scene_t);`.

A common scene callback signature is:

```c
arm_fsm_rt_t fnScene(void *pTarget,
                     const arm_2d_tile_t *ptTile,
                     bool bIsNewFrame);
```

The first parameter is usually `void *pTarget`, not `arm_2d_scene_t *`. If custom scene state is needed, follow the current demo/framework pattern to recover it. Do not invent a signature.

When getting the valid PFB area and pixel pointer from a tile, prefer Arm-2D helper/API calls instead of hand-rolled pointer arithmetic:

```c
arm_2d_location_t tOffset;
arm_2d_region_t tValidRegion;
uint16_t *phwBuffer;

arm_2d_get_address_and_region_from_tile(ptTile,
                                        &tValidRegion,
                                        &tOffset,
                                        uint16_t,
                                        phwBuffer);
```

When writing pixels, distinguish:

- The current valid PFB region `tValidRegion`.
- The current PFB offset in full-screen coordinates `tOffset`.
- The destination tile stride.
- The pixel format and pixel type, such as `uint16_t` for RGB565.

Common mistakes:

- Assuming every scene callback receives the full-screen framebuffer.
- Ignoring PFB offset so every block renders as if it starts at logical `(0,0)`.
- Using region width as stride when the actual stride is the destination tile/framebuffer line width.
- Ignoring `bIsNewFrame`, which can break animation timing, state updates, or dirty marking.
- Forgetting full-frame refresh, causing procedural animation or shaders to render one frame and stop.
- Treating Arm-2D async return values as synchronous function results.

For fullscreen animation, shaders, or real-time gauges that change every frame, explicitly enable full-frame refresh or mark dirty regions every frame:

```c
arm_2d_helper_pfb_full_frame_refresh_mode(&DISP_ADAPTER.use_as__arm_2d_helper_pfb_t,
                                          true);
```

Replace `DISP_ADAPTER` with the actual display adapter object used by the project.

Scene/player/demo guidance:

- Start from the Pack-provided demo initialization and scene append flow.
- Do not bypass the scene player and poke internal fields unless the current version's docs or demos show that pattern.
- Re-check function signatures and struct fields after changing Arm-2D Pack versions.
- Keep custom demos decoupled from the display flush port.

### Performance Verification

Establish observability before optimizing.

Recommended metrics:

- FPS.
- Frame time.
- Flush time.
- Flush count.
- Flush pixel count or region size.
- CPU load.
- DMA/display error count.
- Cache, bus, or alignment-related faults.

Bottleneck interpretation:

- Low flush time but high frame time: likely CPU rendering bottleneck.
- High flush time: inspect display bus, DMA, framebuffer, cache, and region size.
- High FPS but no increasing flush count: likely stale/fake data or a scene that is not refreshing.
- Ghosting or partial updates: inspect dirty regions, tile regions, and full-frame refresh logic.

### Build, Download, And Runtime Verification

Generic flow:

1. Build the project.
2. Download it to the target.
3. Confirm the program resets and runs.
4. Read or print Arm-2D runtime statistics.
5. Compare PFB, dirty-region, async, DMA, and cache configurations.
6. After enabling LTO or changing optimization levels, re-check symbol addresses and runtime state.

In MDK command-line workflows, UV4 can automate build/download. With a debugger/probe tool, read RAM statistics to validate runtime behavior.

### Common Pitfalls

- Redeploying Arm-2D Pack/RTE without clearing `RTE/Acceleration`, which can mix stale generated files with new components.
- Arm-2D Pack/RTE components are selected, but include paths miss the project-local config directory.
- PFB settings are changed, but dynamic PFB or other macros override them.
- RGB565 colors are wrong because of RGB/BGR order or byte order.
- DCache is enabled but clean/invalidate is missing, so DMA/display reads stale data.
- Async flush is enabled with only one PFB and no real pipeline overlap happens.
- Dirty-region logic causes a scene to render one frame and then stop.
- Benchmark FPS does not match application FPS because the scene workload is different.
- LTO moves symbols and the debug tool reads old addresses.
