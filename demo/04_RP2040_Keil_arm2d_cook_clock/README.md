# RP2040 Arm-2D Template

基于 Arm-2D 的 RP2040 Keil 工程，使用 ST7789 屏幕驱动和原生 Arm-2D 显示适配器。

Git 仓库根目录：[../../](../../)

## 参考来源

- Arm-2D 官方仓库：[ARM-software/Arm-2D](https://github.com/ARM-software/Arm-2D)。
	本工程的 Arm-2D 场景风格、显示适配器和示例实现主要参考该仓库。

## 运行方式

`main.c` 完成平台、Arm-2D 核心和显示适配器初始化，然后持续调用 `disp_adapter0_task()`。
默认首屏由 `project/mdk/RTE/Acceleration/arm_2d_disp_adapter_0.c` 的原生 busy-wheel 测试绘制流程提供。

## 开发入口

- 屏幕驱动：`platform/st7789_simple.c`
- 平台初始化：`platform/pi_platform.c`
- Arm-2D 配置：`project/mdk/RTE/Acceleration/arm_2d_cfg.h`
- 显示适配器：`project/mdk/RTE/Acceleration/arm_2d_disp_adapter_0.c`

使用 Keil 打开 `project/mdk/template.uvprojx` 构建和烧录。

