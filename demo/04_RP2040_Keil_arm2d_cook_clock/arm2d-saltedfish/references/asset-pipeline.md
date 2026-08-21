# 素材与动画资源链路

## 目标

资源处理目标不是“能显示就行”，而是要在目标 MCU/SoC 的实际资源约束下平衡：

- Flash 占用
- SRAM 占用
- 解码速度
- 透明/遮罩效果
- 局部刷新
- 工具链可维护性

## 常见格式

### 未压缩 RGB565 帧序列

优点：

- MCU 端播放最简单。
- 不需要 decoder。
- 可以直接构造 `arm_2d_tile_t`。

缺点：

- 极吃 Flash。

尺寸估算：

```text
width * height * 2 * frame_count
```

例如 240x240x80：

```text
240 * 240 * 2 * 80 = 9.216 MB
```

除非 Flash 足够，否则不要作为最终方案。

### 精灵图 Sprite Sheet

优点：

- PC 侧容易预览。
- 工具链中间态方便。

缺点：

- 进入固件后通常仍然会变成未压缩像素或某种压缩格式。
- 不等于省 Flash。

### Mask / Alpha

如果动画需要透明背景，要明确：

- 使用 1-bit mask？
- 使用 A2/A4/A8 alpha？
- 是否需要边缘抗锯齿？
- 是否能接受硬边透明？

1-bit mask 省空间，但边缘可能有锯齿。

### zhRGB565 / QOI

适合压缩图片资源。

需要确认工程里是否启用了对应 Arm-2D loader/component。当前 skill 内已有 zhRGB565 相关辅助脚本，但是否能在目标工程中直接使用，仍要以工程端 loader/component 为准。

## 动画平铺与存储连续性

对于“整张 sprite sheet 按扫描线编码，再由文件或 ROM loader 按帧读取 ROI”的动画链路，平铺布局会直接影响播放性能：

- 多列平铺时，单帧的一条扫描线通常连续，但相邻扫描线之间夹有其他帧的数据；读取一帧需要跨越多个不相邻区间。
- 纵向单列平铺时，单帧占满图集行宽，帧内扫描线相邻，适合顺序读取、块缓存和预取。
- 常见生成器用 `columns=1` 表示纵向单列。不要把“横向单行”和“纵向单列”混淆；可靠判据是 `frame_width == sheet_width`。

这项优化主要减少 seek、分段读取和缓存失效，并不代表压缩算法本身更快。收益通常按以下顺序递减：

```text
SD + FatFS > SPI/QSPI XIP Flash > 真正片内 Flash > SRAM
```

实施时：

1. 先检查 loader 是否按整张图的行偏移读取，以及每帧会触发多少次 seek/read/cache refill。
2. 对 SD/FatFS 或 XIP 流式播放，优先生成 `columns=1` 的纵向单列资源，并让读取块对齐扇区或 Cache line。
3. 同时记录压缩后大小、平均/最差帧耗时、seek/read 次数和缓存命中情况；不要只比较文件大小或目测帧率。
4. 如果格式将每帧独立封装为连续数据块，则以实际索引和 I/O 跟踪为准，不要机械套用 sprite sheet 结论。

## 决策建议

### 用户只想验证画面

可以临时用：

- RGB565 frame array
- sprite sheet 转数组

### 用户明确要省 Flash

优先考虑：

- 降尺寸 / 限帧率 / 减帧数
- QOI
- zhRGB565
- tile/block 局部更新
- palette animation
- 背景固定时只存前景变化区域

### 用户要透明 GIF

先确认：

- 背景是否固定？
- 透明边缘是否需要抗锯齿？
- 是否可以用 1-bit mask？
- 目标尺寸和帧数是多少？

### 用户要实时动态效果

优先考虑：

- 低分辨率缓存 + 放大
- palette animation
- 预计算 map 放 Flash


## Python 工具

当前 skill 内置脚本目录：

```text
python/
```

主要脚本：

- `gif2png.py`：GIF 转精灵图。
- `img2c.py`：图片转 C/ARM-2D tile 数据。
- `ttf2c.py`：字体转换。
- `jinja2c.py`：模板生成 C。
- `__img2c_lmsk.py`：LMSK 辅助模块。
- `__img2c_zhRGB565.py`：zhRGB565 辅助模块。


使用前先运行 help 或直接读脚本参数。

## 当前不支持项

- 未提供格式文档、PC 编码/解码器或 MCU generic loader 的动画容器，不要写成可直接使用的方案。

## 待完善

- Keil 工程自动添加资源脚本。
