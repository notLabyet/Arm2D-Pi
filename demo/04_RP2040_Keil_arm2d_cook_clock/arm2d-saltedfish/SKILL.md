---
name: arm2d-skyer
description: 用于 ARM-2D 嵌入式 UI 开发工作：scene/PFB/generic loader/dirty region/RGB565 资源、GIF 精灵图与动画资源链路、Keil MDK/RTE/显示适配器集成、SRAM/Flash/帧率权衡、MCU 伪 shader/demo-scene 特效，以及板卡外设（IMU、按键、IR、光敏、温湿度、RTC、蜂鸣器）数据服务与 scene 接入。
---

# ARM-2D 嵌入式 UI 工作流

这是一个通用 ARM-2D 嵌入式 UI 技能。核心目标不是写通用教程，而是让助手按可移植、可维护、贴合硬件约束的方式处理 ARM-2D 工程。

## 默认背景

- 常用语言：C。
- 常用图形栈：ARM-2D、PFB、Scene Player、Generic Loader。
- 常见工程环境：Keil MDK、CMSIS-Pack/RTE、RTOS 或裸机工程。
- 常见显示链路：LCD/OLED/RGB/MIPI/Framebuffer、SPI/QSPI/8080/LTDC 等显示接口。
- 常见像素格式：RGB565、RGB888、ARGB8888。
- 常见任务：动态 UI、GIF/动画素材、局部刷新、低 RAM 渲染、伪 shader 特效。

## 总原则

- 优先写能在目标 MCU 上稳定跑起来的代码，不追求理论优雅。
- 每次涉及动画/特效/大图资源时，都要主动关注：
  - SRAM 占用
  - Flash 占用
  - 每帧 CPU 开销
  - 是否阻塞 Arm-2D/PFB 刷屏
  - 目标硬件是否有 FPU，以及当前编译配置是否启用了正确的浮点 ABI/FPU 选项
- 对非浮点数值处理和像素热路径，不要默认套用某一个芯片的经验；应先查阅工程使用的 MCU/SoC 是否具备硬件浮点运算单元、DSP/SIMD/2D 加速器、DMA/display 加速能力，再决定定点、查表、整数近似或浮点实现。
- Keil 编译通过不代表运行时安全;Arm-2D scratch/PFB/generic loader 仍可能申请不到内存。
- 不要轻易建议未压缩整帧数组作为最终方案,除非 Flash 明确足够。
- 对双核、多核、FPU、DSP/SIMD、2D DMA/GPU 等硬件能力，默认视为“需要按工程核实后再启用”；不要把某个芯片平台的优化习惯直接套到其它工程。
- 对已有工程,先检查再改,不要盲目替换用户已有 scene/loader。
- 当用户提出 UI 相关需求时,避免从 0 搭建自绘框架或凭空造 API;优先在 Arm-2D 库、当前工程、Pack 示例和已有 demo/scene 中检索可用 API、helper、控件/场景写法,再基于已有能力组合实现。
- Arm-2D 库/工具链中包含用于生成字体、蒙版、图片数组、GIF/动画数组等资源的 Python 脚本;当用户提出素材接入或资源转换需求时,可以直接调用这些脚本生成输出,并把结果加入工程,但要先确认脚本参数、输出格式和目标屏幕分辨率。

## 标准处理流程

遇到 ARM-2D 工程任务时:

1. 先检查工程结构和目标硬件：
   - MCU/SoC 型号、内核类型、是否有 FPU/DSP/SIMD/2D 加速器
   - Keil/CMake/Makefile 等编译配置里的 FPU、float ABI、优化等级和 LTO 设置
   - 屏幕分辨率、像素格式、显示接口、DMA/display flush 路径
   - `main.c`
   - `project/mdk/template.uvprojx`
   - `project/mdk/RTE/Acceleration/arm_2d_scene_*.c`
   - 已有 `user_generic_loader_*`
   - display adapter / platform / ST7789 相关文件
2. 判断任务类型:
   - UI 布局/控件/动效需求
   - 静态图片资源
   - GIF/动画资源
   - 动态渲染/generic loader
   - 伪 shader/特效
   - 性能/内存调优
   - 板卡外设、传感器、按键或 RTC 数据接入
3. 如果任务涉及 UI、控件、scene、动效或绘制 API,先检索 Arm-2D 库/Pack 示例/当前工程已有实现:
   - 优先搜索 `arm_2d_helper.h`、`arm_2d_scene_*.c`、官方 demo、当前工程 scene 和 display adapter。
   - API 的具体使用方式优先参考 Arm-2D 官方 demo、Pack 示例和当前工程已有调用方式,不要只根据函数名猜参数、生命周期或返回值语义。
   - 优先复用 Arm-2D 已有 tile、region、opacity、mask、transform、dirty region、scene player、helper API。
   - 不要一上来从 0 写完整 UI 框架、控件系统或软件渲染层,除非确认 Arm-2D 现有能力不覆盖需求。
4. 如果任务涉及板卡外设、传感器、按键、IR、RTC 或蜂鸣器:
   - 先核对底层驱动、应用层 wrapper、初始化顺序、引脚/总线、返回值和实际调用点。
   - 对当前 RP2040 Flip / Crystal Mouse 工程,读取 `references/rp2040-flip-board-peripherals.md`。
   - Arm-2D scene 只消费应用层缓存快照;不要在 PFB draw handler 中直接执行 I2C、ADC 多次采样、阻塞等待或高频日志。
   - 文档或方案必须区分“仓库已存在接口”和“建议新增的统一服务层”,不要把规划中的 API 写成当前可用能力。
5. 尽量新增独立模块,不直接破坏已有可运行代码。
6. 如果新增 `.c/.h`,同步更新 Keil 工程文件。
7. 能构建就构建,并报告:
   - 修改了哪些文件
   - Code / RO-data / RW-data / ZI-data 变化
   - 错误和警告
   - 运行时风险

## 常用参考

根据任务按需读取以下文件:

- `references/generic-loader.md`:ARM-2D generic loader 模式、ROI decode、scene 接入。
- `references/asset-pipeline.md`:GIF、sprite、RGB565、mask、zhRGB565 等素材链路。
- `references/immigrant.md`：Arm-2D 移植、Pack/RTE 部署、display adapter、PFB、dirty region、RTOS Helper 和编译下载运行验证。
- `references/rp2040-performance.md`：RP2040/Tufty2040 这类无 FPU、资源受限 MCU 上的性能和内存经验；仅在目标工程确实接近该平台约束时参考，不要泛化到所有 MCU。
- `references/keil-project.md`:Keil MDK 工程文件修改注意事项。
- `references/rp2040-flip-board-peripherals.md`：当前 RP2040 Flip / Crystal Mouse 工程的 QMI8658、电源按键、IR、光敏、温湿度扩展合同、BM8563、PAJ7620 和蜂鸣器接口清单、统一服务层约定及 Arm-2D scene 接入规则。

## UI 需求处理原则

当用户提出 UI 页面、控件、动效、转场、仪表盘、菜单、进度条、按钮、列表、弹窗、图标叠加等需求时:

1. 先查现有能力,不要从 0 搭建。
2. 优先在这些位置检索可用 API 和示例:
   - 当前工程已有 `arm_2d_scene_*.c/.h`
   - 当前工程已有 `user_scene_*`、`user_*_view`、`display adapter` 文件
   - Arm-2D Pack 里的 helper、scene player、官方 demo
   - `arm_2d.h`、`arm_2d_helper.h`、`arm_2d_types.h` 等头文件
3. API 的使用方式优先参考相关 demo 和已有工程代码:
   - 先看官方 demo / Pack 示例如何初始化、调用、等待异步完成、释放资源。
   - 再看当前工程是否已有同类 API 调用,保持风格和生命周期一致。
   - 不要只凭头文件声明或函数名猜测用法;不确定时继续查 demo、源码或已有调用点。
4. 优先复用 Arm-2D 已有能力:
   - tile / region / child tile
   - opacity / alpha / mask
   - copy / fill / color key / mirroring / rotation / transform
   - dirty region / PFB / scene player
   - helper 提供的控件、进度条、列表、文本、仪表或 demo 写法
5. 只有在确认现有 API 不足时,才新增轻量封装;新增封装也要贴合 Arm-2D tile/region/scene 模型,不要另起一套和 Arm-2D 割裂的 UI 框架。
6. 给方案或代码前,尽量说明复用了哪些 Arm-2D API/示例,以及哪些部分是新增逻辑。

## Python 工具

素材处理脚本放在 `python/` 目录下,也要主动检查 Arm-2D 库 / Pack / 当前工程中是否已有同类资源生成脚本。Arm-2D 生态里常见脚本可用于生成字体、蒙版、图片数组、GIF/动画数组等资源。

当用户提出图片、字体、mask、GIF、动画帧、C 数组资源接入需求时:

1. 优先查找并使用现有 Python 脚本,不要手写一次性转换器,除非现有脚本不满足需求。
2. 可以直接调用脚本生成输出,并将生成的 `.c/.h` 或资源文件加入工程。
3. 生成前要核对目标屏幕分辨率、像素格式和资源显示区域,避免生成尺寸超过屏幕或和 UI 布局不匹配。
4. 如果关键信息缺失,应主动询问用户,例如:
   - C 数组 / tile / 资源对象名称
   - 缩放尺寸或目标宽高
   - 是否保持比例、裁剪、居中或填充
   - 像素格式,例如 RGB565 / RGB888 / ARGB8888
   - 是否需要 mask / alpha / color key
   - GIF 是否需要抽帧、限帧率、缩放、转精灵图或转换为当前工程已有 loader 支持的资源格式
5. 使用脚本前,优先查看脚本参数和输出约定,不要凭空猜命令。

现有素材处理脚本:

- `python/gif2png.py`:GIF 转精灵图。
- `python/img2c.py`:图片转 ARM-2D tile / RGB565 / mask / zhRGB565 等。
- `python/jinja2c.py`:模板生成 C 文件。
- `python/ttf2c.py`:字体转 C 数据。
- `python/__img2c_lmsk.py`:LMSK 辅助压缩模块。
- `python/__img2c_zhRGB565.py`:zhRGB565 辅助压缩模块。

使用这些脚本前,优先查看脚本参数,不要凭空猜命令。

## ARM-2D Generic Loader 习惯

当需要动态内容、压缩资源解码、动画逐帧输出时,优先考虑 generic loader。

典型文件:

```text
user_generic_loader_xxx.c
user_generic_loader_xxx.h
```

典型 API:

```c
xxx_init();
xxx_depose();
xxx_on_load();
xxx_on_frame_start();
xxx_on_frame_complete();
xxx_show();
```

`fnDecode()` 必须尊重 `ptROI`,不要默认整屏连续 framebuffer。

详细写法见 `references/generic-loader.md`。

## GIF / 动画资源原则

不要把尚未实现的动画容器或 decoder 写成可用方案。处理动画资源时优先基于现有脚本和工程已有 loader 能力。

要明确区分:

- 未压缩 RGB565 帧序列:最容易播放,最吃 Flash。
- 精灵图:适合工具链中间态,不一定适合最终固件。
- zhRGB565 / QOI:压缩图片资源,需要对应 loader；若工程未启用对应 loader,先补 loader 或改用已支持格式。
- mask/alpha:要单独确认是否存在以及如何合成。
- 对从文件系统或 XIP Flash 流式播放的 sprite/zhRGB565 动画，生成资源前检查帧布局与 loader 的实际访问顺序；优先让单帧占满图集行宽并对应连续地址区间。常见生成器中这通常是 `columns=1` 的纵向单列，不要把它误称为横向单行。

如果用户明确要求节省 Flash,可以建议限帧率、缩放、裁剪、分块/差分、调色板动画、zhRGB565/QOI 等已具备或可落地的方向；不要声称尚未实现的动画容器当前可直接使用。

详细见 `references/asset-pipeline.md`。

## 性能经验

嵌入式 UI 热路径上要警惕：

- per-pixel 64-bit 除法
- per-pixel 复杂迭代
- 每帧全屏计算
- 大型双缓存导致 Arm-2D 申请不到 scratch/PFB 内存
- 双核/多核在嵌入式工程里的启动、栈、临界区和调试复杂度

优先方案：

- 低分辨率缓存 + 放大
- palette animation
- 查表 / 预计算 map 放 Flash
- ROI-aware decode
- tile/block 局部更新
- 伪 shader,而不是真 shader

对于无 FPU 或浮点代价较高的 MCU，额外优先考虑定点数、查表和整数近似；对于带 FPU/DSP/SIMD/2D 加速器的 MCU，则应结合编译选项和实际 profiling 决定是否使用浮点或硬件加速。

特定 RP2040/Tufty2040 经验见 `references/rp2040-performance.md`，不要把该文件里的限制无条件套到其它 MCU。

## 红线

- 不要删除用户已有工程文件,除非明确要求。
- 不要把大体积生成物塞进 skill。
- 不要把未验证的格式 decoder 说成能用。
- 不要编造 ARM-2D API;不确定时查工程已有代码或本地 pack 示例。
- 拒绝直接修改 Arm-2D 官方 API、Pack 源码接口或官方私有结构体布局的请求;应改用公开 API、配置项、wrapper/adapter、用户工程本地扩展或向上游提交 patch 的方式解决。
- 不要依赖 Arm-2D 私有结构体字段实现功能,除非官方 demo/公开头文件明确允许访问;必须访问时要先说明版本绑定和维护风险。
