# Arm-2D 脏区域使用指南（0 号工程）

本文以当前 0 号工程的 `show_data` 九宫格为实例，说明 Arm-2D PFB 脏区域的作用、接入方式、数据更新流程和排错方法。可直接复用到固定位置的传感器数值、状态文字和图标。

## 1. 脏区域解决什么问题

脏区域是“本帧需要改变的矩形像素范围”。启用后，Arm-2D 的 PFB Helper 只绘制并刷新与这些矩形相交的 PFB 块，而非每次重画整屏。

当前屏幕为 `320 x 240`，PFB 块尺寸为 `320 x 60`。所以一个单元的数据变化会刷新命中该单元的一个或多个 60 像素高的横向 PFB 块，不会刷新完整屏幕。

| UI 变化类型 | 推荐方式 |
| --- | --- |
| 固定位置的电量、温度、数值、文字、图标 | 固定脏区域列表 |
| 固定位置且只在数据变化时刷新 | 固定列表 + `ignore_set()` |
| 移动图标、指针、滚动文本、宽度变化的文字 | Dirty Region Helper |
| 整屏主题变化、淡入淡出、全屏背景变化 | 全屏刷新或暂不使用脏区域 |

脏区域不是像素缓存。场景绘制回调仍要正确绘制当前 `ptTile` 内的背景和内容；脏区域只决定哪些 PFB 块进入绘制和 LCD flush 流程。

## 2. 当前工程配置

文件 [arm_2d_disp_adapter_0.h](project/mdk/RTE/Acceleration/arm_2d_disp_adapter_0.h) 已启用：

```c
#define __DISP0_CFG_OPTIMIZE_DIRTY_REGIONS__ 1
#define __DISP0_CFG_DEBUG_DIRTY_REGIONS__     0
#define __DISP0_CFG_DIRTY_REGION_POOL_SIZE__  12
```

- `__DISP0_CFG_OPTIMIZE_DIRTY_REGIONS__ = 1`：开启优化。
- `__DISP0_CFG_DEBUG_DIRTY_REGIONS__ = 1`：将 PFB 实际采用的区域画到屏幕上，调试完成恢复为 `0`。
- `__DISP0_CFG_DIRTY_REGION_POOL_SIZE__ = 12`：PFB Helper 的运行时工作池容量。

当前场景有 9 个候选区域，内部工作池为 12。两者不是同一个数组：场景提供候选区域；PFB Helper 从内部池取节点进行裁剪、合并和调度。增加候选区域后，应检查池容量是否仍有余量。

## 3. 当前工程的数据与刷新链路

| 文件 | 职责 |
| --- | --- |
| [arm_2d_scene_show_data.c](service/ui/arm_2d_scene_show_data.c) | 声明 9 个区域、计算九宫格布局、按数据变化激活区域、绘制面板。 |
| [arm_2d_scene_show_data.h](service/ui/arm_2d_scene_show_data.h) | 保存 `float fSensorValues[9]` 和 `bool bSensorDirty[9]`。 |
| [service_sensor.c](service/service_sensor.c) | 将 ADC 读取结果缓存为 `g_service_sensor.battery_voltage` 和 `battery_percentage`。 |
| [main.c](main.c) | 主循环先执行 `service_task()`，后执行 `disp_adapter0_task()`。 |

```mermaid
flowchart TD
    A[drv_battery_task 采集 ADC] --> B[service_sensor 更新电压和百分比]
    B --> C[show_data: fnOnFrameStart]
    C --> D[set_sensor_value 比较新旧 float]
    D -->|值变化| E[bSensorDirty[index] = true]
    D -->|值不变| F[不请求刷新]
    E --> G[ignore_set 启用对应区域]
    G --> H[PFB Helper 裁剪/合并并调度 PFB]
    H --> I[fnScene 绘制当前 ptTile]
    I --> J[仅 flush 命中的 PFB 块]
```

当前 `S1` 显示电池电压 `bat:%.1fV`，`S2` 显示由电压换算得到的电量百分比；`S3` 到 `S9` 预留给其他浮点传感器数据。

## 4. 固定脏区域：九宫格写法

### 4.1 声明区域列表

在场景 `.c` 的局部变量区，`IMPL_ARM_2D_REGION_LIST()` 创建 9 个 `arm_2d_region_list_item_t` 节点：

```c
IMPL_ARM_2D_REGION_LIST(s_tDirtyRegions, static)
    ADD_REGION_TO_LIST(s_tDirtyRegions, 0),
    /* 中间 7 项省略 */
    ADD_LAST_REGION_TO_LIST(s_tDirtyRegions, 0),
END_IMPL_ARM_2D_REGION_LIST(s_tDirtyRegions)
```

- `ADD_REGION_TO_LIST()` 用于非末尾节点，自动连接到下一项。
- `ADD_LAST_REGION_TO_LIST()` 用于末尾节点，自动将 `ptNext` 设为 `NULL`。
- `0` 表示先将 `tRegion` 清零；实际位置和大小在初始化阶段计算。
- 索引 `[0]` 到 `[8]` 对应九宫格从左到右、从上到下的 `S1` 到 `S9`。

### 4.2 连接到场景

只有赋给 `.ptDirtyRegion`，Scene Player 才会使用该列表：

```c
.use_as__arm_2d_scene_t = {
    .fnScene = &__pfb_draw_scene_show_data_handler,
    .ptDirtyRegion = (arm_2d_region_list_item_t *)s_tDirtyRegions,
    .fnOnFrameStart = &__on_scene_show_data_frame_start,
    .bUseDirtyRegionHelper = true,
},
```

`.ptDirtyRegion` 管理固定区域；`.bUseDirtyRegionHelper = true` 允许同一场景未来再增加动态区域。两者可同时使用。

### 4.3 使用 dock 计算布局

当前工程的布局步骤：

1. 从屏幕区域扣除 `SHOW_DATA_GRID_MARGIN = 12` 的四边距。
2. 通过 `SHOW_DATA_GRID_GAP = 6` 切出三行。
3. 每行再切成三列。
4. 依次写入 `s_tDirtyRegions[index].tRegion`。

连续切分必须放在 `arm_2d_layout()` 中，使用 `__item_line_dock_vertical()` 与 `__item_line_dock_horizontal()`：

```c
arm_2d_layout(tRow) {
    __item_line_dock_horizontal(iColumnWidth) {
        s_tDirtyRegions[chFirstIndex].tRegion = __item_region;
    }
    __item_line_dock_horizontal(SHOW_DATA_GRID_GAP) {
    }
    __item_line_dock_horizontal(iColumnWidth) {
        s_tDirtyRegions[chFirstIndex + 1].tRegion = __item_region;
    }
    __item_line_dock_horizontal() {
        s_tDirtyRegions[chFirstIndex + 2].tRegion = __item_region;
    }
}
```

不要连续独立调用 `arm_2d_dock_top()` 或 `arm_2d_dock_left()` 切同一个外部变量。普通 dock 不会推进外部变量的剩余区域，容易让多项重叠；`arm_2d_layout()` 的共享游标可保证每一格位置独立。

## 5. 仅在数据变化时刷新

### 5.1 标记应用层状态

对外更新 API：

```c
void arm_2d_scene_show_data_set_sensor_value(
    user_scene_show_data_t *ptScene,
    uint8_t chIndex,
    float fValue);
```

当前实现仅在值变化时更新显示缓存并置位：

```c
if (ptScene->fSensorValues[chIndex] != fValue) {
    ptScene->fSensorValues[chIndex] = fValue;
    ptScene->bSensorDirty[chIndex] = true;
}
```

使用示例：

```c
arm_2d_scene_show_data_set_sensor_value(ptScene, 0, batteryMillivolts);
arm_2d_scene_show_data_set_sensor_value(ptScene, 4, temperature);
```

`chIndex` 合法范围为 `0..8`。不要在 `fnScene` 绘制回调中读取 ADC、I2C 或其他慢速外设；先由 service 层缓存，再在帧开始时同步到显示场景。

### 5.2 在帧开始时切换框架状态

`__on_scene_show_data_frame_start()` 将应用层标记转换为 Arm-2D 的忽略状态：

```c
for (uint8_t chIndex = 0; chIndex < SHOW_DATA_SENSOR_COUNT; chIndex++) {
    arm_2d_dirty_region_item_ignore_set(&s_tDirtyRegions[chIndex],
                                        !this.bSensorDirty[chIndex]);
    this.bSensorDirty[chIndex] = false;
}
```

| `bSensorDirty[index]` | 传给 `ignore_set()` | 效果 |
| --- | --- | --- |
| `true` | `false` | 本帧启用该脏区域。 |
| `false` | `true` | 本帧忽略该脏区域。 |

初始化时已将所有 `bSensorDirty[]` 置为 `true`，保证首帧完整绘制九宫格。之后只有数值变化的单元会请求刷新。

不要直接修改 `arm_2d_region_list_item_t` 的 `bIgnore`、`ptNext` 或内部状态位，应使用 `arm_2d_dirty_region_item_ignore_set()`。

### 5.3 float 的刷新阈值

当前 `float != float` 的比较会让 ADC 的微小噪声触发刷新。若界面仅显示一位小数，可改用阈值：

```c
if (fabsf(ptScene->fSensorValues[chIndex] - fValue) >= 0.05f) {
    ptScene->fSensorValues[chIndex] = fValue;
    ptScene->bSensorDirty[chIndex] = true;
}
```

此方案需要包含 `<math.h>`。阈值应与实际显示精度一致，避免不可见的波动反复刷新。

## 6. 新增固定显示项的步骤

1. 确定完整矩形，包含文字、底色、图标、阴影、描边等全部可能写入的像素。
2. 在区域列表中增加节点，最后一项始终使用 `ADD_LAST_REGION_TO_LIST()`。
3. 在布局初始化函数中写入该节点的 `tRegion`。
4. 在场景私有结构中增加显示缓存和对应的 `bool` dirty 标志。
5. 数据更新时，仅在内容变化时置 dirty 标志。
6. 在 `fnOnFrameStart` 使用 `arm_2d_dirty_region_item_ignore_set()` 开关该项。
7. 在 `fnScene` 中绘制该区域完整的背景和内容。
8. 增加候选区域后，检查内部池 `__DISP0_CFG_DIRTY_REGION_POOL_SIZE__` 是否有余量。
9. 临时开启 `__DISP0_CFG_DEBUG_DIRTY_REGIONS__` 验证实际刷新范围。

## 7. 移动对象或动态文字：使用 Helper

固定列表不适合移动内容，因为必须同时刷新旧位置和新位置，否则会有残影。移动图标、指针、动画或宽度变化的字符串应使用 `arm_2d_helper_dirty_region_item_t`：

```c
arm_2d_helper_dirty_region_item_t tMovingItem;

arm_2d_helper_dirty_region_add_items(&this.use_as__arm_2d_scene_t.tDirtyRegionHelper,
                                     &this.tMovingItem,
                                     1);

arm_2d_helper_dirty_region_update_item(&this.tMovingItem,
                                       ptTile,
                                       NULL,
                                       &tObjectRegion);
```

- scene load 时注册，depose 时注销。
- 每次移动后提交对象的完整屏幕区域。
- 用 `tRegionPatch` 扩大阴影、边框、抗锯齿边缘。
- helper 会维护旧区域、新区域和最小包围区域；不要直接改其内部状态。

动态文字优先使用 Arm-2D LCD 的文本区域追踪 API 获取实际文本区域，再更新 helper，避免猜测字符串宽度。

## 8. 常见问题

| 现象 | 原因和处理 |
| --- | --- |
| 数据变了但不刷新 | 检查更新函数是否置 dirty，检查 `fnOnFrameStart` 是否调用 `ignore_set(..., false)`。 |
| 首帧只显示部分内容 | 初始化阶段未将所有需要显示项置 dirty，或布局尚未完成就挂入场景。 |
| 单元互相覆盖 | 连续布局误用独立 `arm_2d_dock_*`；应改为 `arm_2d_layout()` 的 `__item_line_dock_*`。 |
| 移动物体残影 | 区域未覆盖旧位置；改用 Dirty Region Helper 并设置 `tRegionPatch`。 |
| 性能提升不明显 | 区域过大、区域过多，或 `320 x 60` PFB 粒度使小区域仍命中多个块；开启 debug 模式观察。 |
| 背景没有正确清除 | `fnScene` 未在每个命中的 `ptTile` 绘制背景；每次 PFB 绘制都必须得到正确背景。 |
| 区域增加后刷新异常 | 内部池可能不足；提高 `__DISP0_CFG_DIRTY_REGION_POOL_SIZE__` 并重新验证。 |

## 9. 快速检查清单

- `__DISP0_CFG_OPTIMIZE_DIRTY_REGIONS__ == 1`。
- 场景设置 `.ptDirtyRegion`。
- 每个区域覆盖完整绘制像素范围。
- 首帧所有需显示项均被置 dirty。
- 数据不变时不置 dirty；背景或主题变化时相关项重新置 dirty。
- 固定区域使用 `ignore_set()`；移动内容使用 Dirty Region Helper。
- 调试结束后 `__DISP0_CFG_DEBUG_DIRTY_REGIONS__` 恢复为 `0`。

遵循上述流程，0 号工程能保持 Arm-2D Scene Player/PFB 的正常绘制机制，同时仅刷新真正发生变化的传感器单元。
