# RP2040 Arm-2D 3D Demo 说明

本文档说明 `02_RP2040_Keil-arm2d_3d` 中的轻量级 3D 引擎、模型数据接口和网格转换工具。渲染器面向 RP2040 的 Arm-2D PFB（Partial Frame Buffer）路径，使用 Q16 定点坐标、Q31 角度和 RGB565 帧缓冲，不依赖 FPU。

## 1. 渲染流程

`ThD_sim_on_frame_start` 每帧读取姿态/时钟状态，组合模型旋转矩阵并把顶点变换到相机空间；随后完成透视投影、视口映射和背面剔除。`__ThD_sim_draw` 是 Arm-2D generic loader 的解码回调，按 ROI 清空局部 Z 缓冲，再用边函数扫描转换三角形，按需执行平面光照、平滑光照、线框和深度雾，最后写入 RGB565 PFB。`ThD_sim_show` 将 loader tile 复制到目标 tile。

引擎的主要数据流如下：

```text
模型 C 数组（Q16 顶点、tri_t 索引、Q14 法线）
        -> 模型实例配置（缩放、偏移、初始角度、颜色）
        -> 姿态/时钟矩阵
        -> 透视投影与背面剔除
        -> ROI 光栅化 + Z 缓冲 + 光照/雾
        -> Arm-2D RGB565 PFB
```

启用 3D loader 时，工程需要同时启用 `RTE_Acceleration_Arm_2D_Helper_PFB` 和 `RTE_Acceleration_Arm_2D_Extra_Loader`，并把 `3d` 目录中的引擎、模型 C 文件加入 MDK 工程。

## 2. 3D 引擎源码和接口

底层原理可以参考 https://www.bilibili.com/video/BV13Fjn6QEyn/?spm_id_from=333.337.search-card.all.click  这也是demo第一个版本的来源

### 2.1 核心渲染器

| 文件 | 作用 | 对外接口/关键配置 |
| --- | --- | --- |
| `3d/user_generic_loader_3d.c` | 3D generic loader 实现。维护模型实例表、模型/相机矩阵、投影顶点、可见面排序和局部 Z 缓冲；提供 RGB565 三角形填充、深度测试、平面/平滑光照、线框和深度雾。当前默认实例是 `rose_vertices/rose_tris`，其余模型实例可在 `s_tModelInstances` 中切换或添加。 | `ThD_sim_init`、`ThD_sim_depose`、`ThD_sim_on_load`、`ThD_sim_on_frame_start`、`ThD_sim_on_frame_complete`、`ThD_sim_set_depth_fog_strength`、`ThD_sim_show`。`ThD_sim_cfg_t` 包含输出尺寸、PFB 临时资源是否使用堆、深度雾强度、屏幕偏移、Arm-2D IO 和 scene 指针。 |
| `3d/user_generic_loader_3d.h` | 3D loader 的公共类型和声明；`ThD_sim_t` 继承 `arm_generic_loader_t`，并保存姿态、时钟、视差和交互矩阵。未启用对应 RTE 组件时，接口宏展开为 `ARM_2D_ERR_NOT_AVAILABLE`/空操作。 | 见上行接口；应用层典型顺序为 `init -> on_load -> 每帧 on_frame_start/show/on_frame_complete -> depose`。 |
| `3d/ThD_test.c` | Q16/Q31 数学和姿态处理：旋转矩阵、近似平方根/反正切、透视投影、Q16 屏幕映射；包含基于加速度计/陀螺仪的简化姿态滤波、校准、偏航回中.文件末尾还提供 8 顶点测试立方体。 | `build_rot_matrix`、`sqrt_q16`、`apply_rot`、`projection`、`to_screen`、`imu_update_euler`、`imu_holo_update_raw`、`imu_holo_step`、`imu_holo_angle_to_q31`、`mahony_update`、`quat_to_matrix`。 |
| `3d/ThD_test.h` | 定义 `Thd_point_t`（三个 Q16 坐标）、`point_t`、`tri_t`（三个 `uint16_t` 顶点索引）、`mat3_t`、`quat_t` 和 `attitude_t` 等基础类型。`Q16(x)` 将浮点常量转为 16.16 定点，`Q31(x)` 将一圈比例转为 Q31。 | 数学函数声明、`cube_vertices/cube_tris` 测试数据；可通过 `CANVA_WIDTH`、`CANVA_HEIGHT`、`DEPTH` 覆盖默认 240x240 画布和投影深度。 |
| `3d/thd_clock_hands.c` | 将毫秒时间转换为 Q31 的时针/分针旋转角，供模型实例绑定 `tClockHand` 后做 Z 轴动画；处理计时器回绕。 | `thd_clock_hands_init`、`thd_clock_hands_update`、`thd_clock_hands_get_angle`。 |
| `3d/thd_clock_hands.h` | 时钟动画状态和句柄枚举。`THD_CLOCK_HANDS_MINUTE_PERIOD_MS` 默认 60 秒，小时周期为 12 分钟周期。 | `thd_clock_hand_t`（`NONE/HOUR/MINUTE`）、`thd_clock_hands_t`。 |

`user_generic_loader_3d.c` 中的模型实例字段含义：`ptVertices/ptTris` 指向模型数组；`pi16FaceNormalsQ14` 可选的面法线；`hwVertexCount/hwTriCount` 是容量；`hwColour/hwWireframeColour` 为 RGB565 颜色；`tClockHand` 绑定时针或分针；`q16Scale` 和 `tOffset` 控制模型大小/位置；`tInitialAngleOffset` 是 Q31 初始欧拉角。

## 3. 模型数据文件和接口

模型文件通常由 `mesh_to_c` 生成，顶点使用 `Q16(...)` 存储，三角形索引使用 `tri_t`，法线使用 Q14 的 `int16_t[3]`。下表列出当前 `3d` 目录中的每个模型/资源 C 文件：

| 文件 | 数据内容和使用方式 |
| --- | --- |
| `3d/cube.c` | J20/测试模型数据，导出 `j20_vertices`、`j20_tris`，并定义 `J20_VERTEX_COUNT=261`、`J20_TRI_COUNT=514`。包含 `ThD_test.h`，没有函数接口。 |
| `3d/j20_model.c` | 另一份 J20 模型数据，导出 `j20_model_vertices`、`j20_model_tris`，规模为 1032 顶点/2056 三角形；没有函数接口。 |
| `3d/earth_model.c` | 地球模型数据，导出 `earth_model_vertices` 及配套三角形数组，定义 `MODEL_VERTEX_COUNT=2504`、`MODEL_TRI_COUNT=5004`；没有函数接口。 |
| `3d/rose.c` | 玫瑰模型，导出 `rose_vertices`、`rose_normals`、`rose_tris`、`rose_tri_normal_idx`，并包含 `rose_face_normals.inc` 中的 843 个面法线；当前 843 顶点、1726 三角形。渲染器默认使用它。 |
| `3d/rose_face_normals.inc` | 由 `generate_face_normals.py` 生成的玫瑰模型 Q14 面法线初始化器，不应手工编辑；修改 `rose.c` 后重新生成。 |
| `3d/hp_oram.c` / `3d/hp_oram.h` | `hp oram.stl` 的转换结果。头文件声明 `hp_oram_vertices[305]`、`hp_oram_tris[634]`、面法线和顶点法线 Q14 数组，并提供 `HP_ORAM_VERTEX_COUNT/HP_ORAM_TRI_COUNT`。模型原点已平移到生成注释中的 shaft/origin。 |
| `3d/hp_oram1.c` / `3d/hp_oram1.h` | `hp oram1.stl` 的低面数转换结果。头文件声明 116 顶点、232 三角形及两类 Q14 法线数组，并提供对应计数宏。 |
| `3d/logo.c` | Arm-2D 图像资源（多个 alpha、灰度、RGB565/CCCA8888 tile），不是 3D 网格或渲染器代码；只有资源符号，没有函数接口。 |

添加新模型时，建议使用 `hp_oram.h` 的结构：在头文件中声明计数宏和四个数组，在 `thd_model_instance_t` 中填入数组指针/计数/颜色/变换，然后把 C 文件加入 MDK 工程。模型顶点和三角形计数都必须不超过 `uint16_t`，坐标需落在 Q16 可表示范围内。

## 4. MeshToC 工具

### 4.1 已生成 EXE 的路径

当前工作区中的 GUI 可执行文件是：

```text
E:\soft\Tomato1.0\RP2040_Keil-arm2d(1)\02_RP2040_Keil-arm2d_3d\3d\tools\dist\MeshToC.exe
```

`dist/` 在 `3d/tools/.gitignore` 中被忽略  仓库中保留了可复现的 Python 源码和构建脚本。

### 4.2 GUI 基础用法

1. 双击 `MeshToC.exe`，选择输入的 `.stl` 或 `.obj` 文件和输出目录。
2. 设置输出标识（例如 `rose`），工具会生成 `<标识>.c` 和 `<标识>.h`。
3. 根据模型坐标系选择 `keep`、`Z-up -> Y-up` 或 `Y-up -> Z-up`；原点可保持原点、使用包围盒中心，或输入 STL/OBJ 坐标系中的自定义 X/Y/Z。表针模型应把轴心设为自定义原点。
4. 默认将最长尺寸归一化为 2.0（约对应引擎的 `[-1, 1]` 空间）。按需调整目标尺寸/额外缩放、顶点合并精度、绕序翻转和小数精度。
5. 用简化滑块移除 0%～95% 的三角形；勾选“顶点法线”生成平滑光照所需数组，勾选导出 OBJ 可额外得到 `<标识>_processed.obj`。
6. 点击导出后，把生成的 C/H 文件加入工程，并按头文件中的数组名配置 `thd_model_instance_t`。

### 4.3 Python CLI（适合脚本化）

GUI EXE 是窗口程序；需要命令行自动化时使用同目录的 `mesh_to_c.py`：

```powershell
cd E:\soft\Tomato1.0\RP2040_Keil-arm2d(1)\02_RP2040_Keil-arm2d_3d\3d\tools
python -m pip install -r requirements.txt
python mesh_to_c.py input.stl output_folder --symbol demo --export-obj
```

常用参数：`--axis keep|z_up_to_y_up|y_up_to_z_up`、`--origin X Y Z`、`--no-center`、`--no-normalize`、`--target-extent 2.0`、`--scale 1.0`、`--simplify 75`、`--flip-winding`、`--vertex-normals` 和 `--export-obj`。完整参数可运行 `python mesh_to_c.py --help` 查看。

转换器依赖 `numpy`、`trimesh`；使用 `--simplify` 时还需要 `fast-simplification`。它会检查退化三角形、重建面法线/可选顶点法线，并输出适合本引擎的 Q16/Q14 C 数组。

`generate_face_normals.py` 是玫瑰模型的专用辅助脚本：从 `rose.c` 解析顶点和三角形，计算单位面法线并写入 `rose_face_normals.inc`。模型几何改变后可执行：

```powershell
python generate_face_normals.py rose.c rose_face_normals.inc
```

### 4.4 重新构建 EXE

在 `3d\tools` 目录双击 `build_mesh_to_c_exe.bat`，或在 PowerShell 执行：

```powershell
.\build_mesh_to_c_exe.bat
```

脚本把 PyInstaller 和可选依赖安装到 `%TEMP%\mesh_to_c_build_deps`，最终生成 `dist\MeshToC.exe`。若只需要 Python GUI，可运行 `python mesh_to_c_gui.py`。

## 5. 配置和性能提示

下列大部分可调宏在 `3d/user_generic_loader_3d.c` 中使用 `#ifndef` 提供默认值，可以在编译器预定义宏或公共配置头中覆盖。`THD_LIGHT_*` 和 `THD_Z_BUFFER_FAR` 当前使用直接 `#define`，如需从工程配置覆盖，应先把源码定义改为 `#ifndef` 形式，或直接修改该文件。开关宏使用 `0` 表示关闭、`1` 表示开启。

### 5.1 描边、填充和光照模式

| 宏 | 默认值 | 作用和实际行为 |
| --- | ---: | --- |
| `THD_CFG_ENABLE_WIREFRAME` | `0` | 描边/线框开关。设为 `1` 后，在三角形表面处理完成后用 `draw_line_fast_rgb565` 绘制三条边，颜色来自模型实例的 `hwWireframeColour`。当前描边函数不读取 Z 缓冲，因此线段自身不做逐像素深度测试。 |
| `THD_CFG_ENABLE_FILL` | `1` | 实体填充开关。设为 `1` 时光栅化三角形内部；非平滑光照路径使用 `fill_triangle_z_rgb565`，会插值深度并执行 Z 测试。设为 `0` 时通常只保留线框，但还要结合 `THD_CFG_ENABLE_OCCLUSION` 判断。 |
| `THD_CFG_ENABLE_OCCLUSION` | `1` | 线框遮挡辅助开关。它只出现在条件 `FILL || (WIREFRAME && OCCLUSION)` 中：当填充关闭而线框和遮挡同时开启时，代码仍会先执行表面填充/Z 缓冲，再叠加描边；当填充已开启时，该宏不改变当前填充路径。若需要真正的纯线框，应使用 `FILL=0`、`WIREFRAME=1`、`OCCLUSION=0`。 |
| `THD_CFG_ENABLE_FLAT_SHADING` | `1` | 平面光照开关。仅在 `SMOOTH_SHADING=0` 时生效；每个三角形用预生成 Q14 面法线（没有时运行时计算）求一次亮度，再整体缩放 RGB565 填充色。设为 `0` 时填充使用模型基础颜色。 |
| `THD_CFG_ENABLE_SMOOTH_SHADING` | `0` | 平滑光照开关。设为 `1` 后为投影顶点累积相邻面的法线和亮度，并在三角形内部插值亮度；它优先于平面光照。当前 `fill_triangle_smooth_rgb565` 不使用 Z 缓冲，因此多个表面交叠时可能出现遮挡错误，同时会增加顶点缓存和 CPU 开销。 |
| `THD_CFG_ENABLE_DEPTH_FOG` | `0` | 深度雾开关。按三角形三个顶点的平均深度计算近色权重，把基础色混合到 `THD_DEPTH_FOG_COLOUR`；之后再进入平面或平滑填充路径。 |

常用组合：

| 目标效果 | `FILL` | `WIREFRAME` | `OCCLUSION` | 光照建议 |
| --- | ---: | ---: | ---: | --- |
| 默认实体模型 | `1` | `0` | `1` | `FLAT=1`、`SMOOTH=0` |
| 实体加描边 | `1` | `1` | `1` | 平面光照或关闭光照均可 |
| 纯线框 | `0` | `1` | `0` | 光照宏对线条无效 |
| 线框加实体遮挡 | `0` | `1` | `1` | 当前实现仍会写入实体填充色，并非透明隐藏线消除 |
| 仅基础色填充 | `1` | `0` | `1` | `FLAT=0`、`SMOOTH=0` |
| 平滑光照填充 | `1` | `0` | `1` | `SMOOTH=1`，注意当前路径不做 Z 测试 |

当 `FILL=0` 且 `WIREFRAME=0` 时不会输出模型几何。`SMOOTH_SHADING=1` 时 `FLAT_SHADING` 会被忽略。

### 5.2 颜色、光照和深度宏

| 宏 | 默认值 | 说明 |
| --- | ---: | --- |
| `THD_MODEL_FILL_COLOUR` | `__RGB(0xAA, 0x00, 0x1A)` | 默认模型填充色。当前 `s_tModelInstances` 中玫瑰实例的 `hwColour` 使用该值。 |
| `THD_MODEL_LINE_COLOUR` | `GLCD_COLOR_YELLOW` | 预留的默认描边色宏；当前活动模型实例没有直接引用它，而是把 `hwWireframeColour` 设为 `THD_MODEL_FILL_COLOUR`。要改变实际描边色，应修改模型实例的 `hwWireframeColour`，或让该字段引用此宏。 |
| `THD_DEPTH_FOG_COLOUR` | `__RGB(0xD6, 0xD8, 0xE0)` | 远处表面逐渐混合到的 RGB565 雾色。 |
| `THD_DEPTH_FOG_START_Q16` | `DEPTH` | 开始计算雾化的相机空间深度；小于等于该值时雾化量为 0（传给颜色插值的近色权重为 1）。 |
| `THD_NEAR_PLANE_Q16` | `1 << 10` | 投影除法使用的最小深度，防止顶点接近/穿过相机时除零或数值爆炸。当前实现是钳位深度，不是几何裁剪近裁面。 |
| `THD_LIGHT_X_Q16/Y_Q16/Z_Q16` | `0.28/-0.40/0.87` | Q16 光源方向分量。改变它们可以调整模型受光方向。 |
| `THD_LIGHT_AMBIENT_Q16` | `0.22` | 环境光强度，即最暗处仍保留的基础亮度。 |
| `THD_LIGHT_DIFFUSE_Q16` | `0.78` | 漫反射强度。当前实现取法线与光线方向点积的绝对值，再乘以该强度并叠加到环境光。 |
| `THD_CFG_DEPTH_BUFFER_SHIFT` | `4` | 将 Q16 深度右移后压缩到 16 位 Z 缓冲。值越大，可表示的远距离越大但深度精度越低；值过小可能更早饱和到最远深度。 |
| `THD_Z_BUFFER_FAR` | `0xFFFF` | Z 缓冲的最远值/清屏值；较小的压缩深度代表更靠近观察者。 |

深度雾的运行时强度来自 `ThD_sim_cfg_t.q16DepthFogStrength`，也可调用 `ThD_sim_set_depth_fog_strength()` 动态调整。只有 `THD_CFG_ENABLE_DEPTH_FOG=1` 时该强度才参与绘制。

### 5.3 缓存容量和视差宏

| 宏 | 默认值 | 说明 |
| --- | ---: | --- |
| `THD_CFG_Z_BUFFER_PIXEL_COUNT` | `CANVA_WIDTH * PFB_BLOCK_HEIGHT`，无 PFB 高度宏时为 `CANVA_WIDTH * 60` | Z 缓冲的 `uint16_t` 元素数，必须覆盖一次 decoder 回调的 ROI 像素数。增加 PFB 块高度时需同步检查 SRAM 占用。 |
| `THD_MAX_PROJECTED_VERTEX_COUNT` | 当前启用模型的总顶点数 | 投影顶点缓存容量；小于实际总数时，超出的顶点不会进入本帧。 |
| `THD_MAX_VISIBLE_FACE_COUNT` | 当前启用模型的总三角形数 | 背面剔除后可记录的最大面数；小于实际需要时，后续可见面会被截断。 |
| `THD_HOLO_PARALLAX_X_GAIN_Q16` | `-0.09` | IMU pitch 映射到屏幕 X 方向视差的增益。 |
| `THD_HOLO_PARALLAX_Y_GAIN_Q16` | `0.07` | IMU roll 映射到屏幕 Y 方向视差的增益。 |

开启平滑光照会扩展每个投影顶点的缓存内容并增加法线累积计算；在 RP2040 上优先使用预生成面法线和平面光照可节省 RAM/CPU。当前测试工具缺少 `fast-simplification` 时，除简化测试外的基础转换测试仍可运行；完整测试请先安装 `3d/tools/requirements.txt`。

模型可以从 MakerWorld 等模型网站获取；导入前需确认模型许可，并使用 MeshToC 简化、调整原点和缩放。
