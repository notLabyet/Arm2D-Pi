# ARM2D QOI 场景集成指南

## 概述
新创建的 QOI 场景(`arm_2d_scene_qoi.h/c`)提供了一个标准化的应用层实现，用于在 ARM2D 中加载和显示 QOI 格式的图像文件。

## 功能特性

- ✅ 支持从 SD 卡加载 QOI 图像
- ✅ 支持动态设置图像路径
- ✅ 分部解码模式（渐进式加载）
- ✅ 自动脏区域优化
- ✅ 完整的场景生命周期管理

## 文件清单

| 文件 | 用途 |
|------|------|
| `arm_2d_scene_qoi.h` | 场景头文件（类型定义、宏定义、API 声明） |
| `arm_2d_scene_qoi.c` | 场景实现文件（初始化、绘制、资源管理） |

## 集成步骤

### 1. 添加头文件包含

在你的应用代码中包含：
```c
#include "arm_2d_scene_qoi.h"
```

### 2. 初始化场景

在场景播放器中初始化 QOI 场景：
```c
// 方式1: 自动分配内存（推荐）
user_scene_qoi_t *ptSceneQOI = arm_2d_scene_qoi_init(&g_ptDispAdapter);

// 方式2: 使用预分配的内存
user_scene_qoi_t tSceneQOI;
arm_2d_scene_qoi_init(&g_ptDispAdapter, &tSceneQOI);
```

### 3. 设置图像路径（可选）

默认路径为：`/image.qoi`

如果需要自定义路径，使用：
```c
// 改变图像路径为 SD 卡上的其他文件
user_scene_qoi_set_image_path(ptSceneQOI, "/my_image.qoi");

// 支持相对路径和绝对路径
user_scene_qoi_set_image_path(ptSceneQOI, "0:/image.qoi");  // SD 卡根目录
user_scene_qoi_set_image_path(ptSceneQOI, "0:/images/background.qoi");  // 子文件夹
```

### 4. 将场景加入播放队列

```c
// 添加到场景播放器
arm_2d_scene_player_append_scenes(
    &g_ptDispAdapter,
    (arm_2d_scene_t *)ptSceneQOI,
    1);  // 1 个场景
```

## SD 卡文件准备

1. **将 QOI 文件放到 SD 卡根目录**，例如：
   ```
   /image.qoi
   /background.qoi
   /sprite.qoi
   ```

2. **或者放在子文件夹**：
   ```
   /images/
       ├── background.qoi
       ├── foreground.qoi
       └── overlay.qoi
   ```

3. **修改代码中的路径**以指向正确的文件

## 配置选项

### 宏定义（在 arm_2d_scene_qoi.h 中）

```c
// 默认图像路径
#ifndef ARM_2D_SCENE_QOI_IMAGE_PATH
#   define ARM_2D_SCENE_QOI_IMAGE_PATH           "/image.qoi"
#endif
```

## 完整示例

```c
#include "arm_2d.h"
#include "arm_2d_scene_qoi.h"

// 全局场景指针
user_scene_qoi_t *g_ptSceneQOI = NULL;

void scene_qoi_setup(void)
{
    // 创建场景
    g_ptSceneQOI = arm_2d_scene_qoi_init(&g_tDispAdapter);
    
    // 设置 SD 卡上的 QOI 文件路径
    user_scene_qoi_set_image_path(g_ptSceneQOI, "0:/image.qoi");
    
    // 将场景添加到播放器
    arm_2d_scene_player_append_scenes(
        &g_tDispAdapter,
        (arm_2d_scene_t *)g_ptSceneQOI,
        1);
}
```

## 性能参数

| 参数 | 说明 |
|------|------|
| 解码模式 | `ARM_QOI_MODE_PARTIAL_DECODED`（分部解码） |
| 脏区域优化 | 已启用 |
| 堆内存分配 | 否（栈分配 VRES 缓冲区） |
| 背景色 | 白色（可在初始化中修改） |

## 常见问题

**Q: 图像加载失败怎么办？**  
A: 检查 SD 卡路径是否正确，文件是否存在。可以在调试器中查看 `this.pszImagePath` 的值。

**Q: 如何支持多个 QOI 图像切换？**  
A: 在运行时调用 `user_scene_qoi_set_image_path()` 更新路径，然后重新初始化加载器。

**Q: 如何优化内存使用？**  
A: 调整 `arm_qoi_loader_cfg_t` 中的缓冲区大小参数（见 `arm_2d_example_loaders.h`）。

**Q: 支持其他格式吗？**  
A: 目前仅支持 QOI。如需支持 JPG/PNG，可参考 `arm_2d_scene_histogram.c` 的实现。

## 技术细节

- **解码策略**：使用分部解码模式，在绘制时逐步解码图像数据
- **内存管理**：场景初始化时自动分配内存，销毁时自动释放
- **线程安全**：不支持多线程访问，需要在主线程中使用
- **性能**：QOI 解码速度快于 JPG，解码缓冲区需求低
