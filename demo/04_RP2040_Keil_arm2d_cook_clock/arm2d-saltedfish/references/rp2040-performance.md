# RP2040 / Tufty2040 性能与内存笔记

## 基本判断

RP2040 跑 ARM-2D 可以做出很漂亮的小屏 UI，但不要把它当桌面 GPU。

Keil 编译报告里：

```text
Program Size: Code=... RO-data=... RW-data=... ZI-data=...
```

其中 ZI/RW 只是静态内存视角。运行时还要留给：

- stack
- heap
- Arm-2D scratch memory
- PFB block buffer
- generic loader 内部资源
- 外设/DMA/SDK 运行时结构

所以 ZI 接近上限时，即使编译通过，也可能运行时黑屏或卡死。

## 高风险写法

在 per-pixel 路径里尽量避免：

- 64-bit division
- 64-bit multiplication 太密集
- 复杂迭代
- 每像素 sin/exp/log，即使是 LUT 也要小心周边计算
- 每帧全屏 240x240 动态计算
- 大尺寸双缓存

## 推荐写法

### 低分辨率缓存 + 放大

例如：

```text
80x80 -> 240x240
60x60 -> 240x240
```

优点：

- 计算量显著下降。
- ROI 输出简单。
- 可配合双缓存。

缺点：

- 画面会糊。
- 适合抽象特效，不适合精细 UI。

### 预计算 map 放 Flash

例如黑洞/旋涡/极坐标效果：

- radius map
- angle map
- distortion map
- palette index map

运行时只做：

- frame offset
- palette lookup
- 简单亮度变化

### Palette animation

老派 demo-scene 技法，非常适合 MCU。

特点：

- SRAM 低。
- CPU 低。
- 视觉动态强。

### Tile/block 更新

如果资源是压缩动画，优先只更新变化 tile。

## 双核注意事项

RP2040 有双核，但在 Keil/CMSIS-Pack 工程中要谨慎。

潜在问题：

- `multicore_launch_core1()` 依赖 SDK runtime。
- core1 stack 配置可能不够。
- FIFO 可能和其他 SDK 逻辑冲突。
- 调试器/启动代码可能导致时序差异。
- core1 重计算可能暴露 IRQ/PIO/DMA 时序问题。

建议：

- 先做单核稳定版本。
- 双核用编译开关控制。
- 如果双核导致黑屏，先关闭验证主链路。

推荐开关：

```c
#ifndef FEATURE_USE_CORE1
#   define FEATURE_USE_CORE1 0
#endif
```

## 判断瓶颈

如果显示能跑但帧率低：

1. 降低缓存分辨率。
2. 降低迭代次数。
3. 去掉除法。
4. 用查表代替计算。
5. 减少每帧更新区域。
6. 用预计算资源代替实时计算。

如果直接黑屏/跑不起来：

1. 降低 ZI。
2. 关闭双核。
3. 用色条/渐变替代复杂算法验证 loader。
4. 检查 Arm-2D scratch/PFB 是否申请失败。
5. 检查 scene 是否正确 append。
