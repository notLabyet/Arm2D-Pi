# Keil MDK 工程修改笔记

## 常见文件

嵌入式 ARM-2D 工程常见路径：

```text
project/mdk/template.uvprojx
project/mdk/template.uvoptx
project/mdk/RTE/Acceleration/arm_2d_scene_0.c
project/mdk/RTE/Acceleration/arm_2d_scene_0.h
```

## 添加源文件

新增 `.c/.h` 后，通常要改：

- `.uvprojx`：真正参与构建。
- `.uvoptx`：IDE 文件树显示，非绝对必须，但建议同步。
- IncludePath：如果新增了目录。

例如新增：

```text
project/mdk/shader/user_generic_loader_shader_art.c
project/mdk/shader/user_generic_loader_shader_art.h
```

需要在 `.uvprojx` 增加 Group，并加入：

```xml
<File>
  <FileName>user_generic_loader_shader_art.c</FileName>
  <FileType>1</FileType>
  <FilePath>.\shader\user_generic_loader_shader_art.c</FilePath>
</File>
<File>
  <FileName>user_generic_loader_shader_art.h</FileName>
  <FileType>5</FileType>
  <FilePath>.\shader\user_generic_loader_shader_art.h</FilePath>
</File>
```

如果头文件目录不在 include path，需要加入：

```text
.;..\..\deivers;..\..\application;.\3d;.\fire;.\shader
```

## 构建与优化等级

除非用户明确指定其它优化等级，处理 ARM-2D / Keil MDK 工程时默认使用 `-Ofast` / `Ofast` 优化等级，并默认勾选 / 开启 Link Time Optimization（LTO，链接时优化）。

原因：ARM-2D 场景、PFB 刷新、generic loader、RGB565 像素处理和伪 shader 往往对逐像素循环性能非常敏感；在 MCU 上，默认低优化等级很容易把可运行 demo 变成明显掉帧。LTO 可以进一步跨文件内联和裁剪无用代码，通常对 Flash 体积和热路径性能都有帮助。

执行要求：

- 新建或调整 Keil 工程配置时，优先设置为 `Ofast`。
- 新建或调整 Keil 工程配置时，默认开启 Link Time Optimization / LTO。
- 如果用户指定 `O0` / `O1` / `O2` / `O3` / `Os` / `Oz`，以用户指定为准。
- 如果用户明确要求关闭 LTO，或工程/库在 LTO 下出现链接错误、弱符号覆盖异常、启动文件/中断向量/段保留问题，则按用户要求或实际问题关闭，并说明原因。
- 如果任务目标是调试异常、单步跟踪、定位内存破坏或查看变量，允许临时降低优化等级或关闭 LTO，但必须说明这是为了调试。
- 修改优化等级或 LTO 设置后，要在报告里明确写出当前使用的优化等级，以及 LTO 是否开启。
- 若 `Ofast` 引发浮点、严格别名、未定义行为或时序相关问题，优先提醒风险并建议针对问题文件局部降级，而不是全工程退回低优化。

## 构建

如果本机有 Keil，可以用命令行构建：

```powershell
& 'D:\keil538\UV4\UV4.exe' -b '.\template.uvprojx' -j0 -o '.\Objects\build.log'
```

注意：路径因机器而异，先检查 `UV4.exe` 位置。

## 报告构建结果

每次构建后报告：

```text
Program Size: Code=... RO-data=... RW-data=... ZI-data=...
0 Error(s), 0 Warning(s)
```

如果和上一次相比 RAM/Flash 变化明显，要说明原因。

## 注意事项

- 不要只改 `.uvoptx`，那不一定参与构建。
- 不要忘记 include path。
- 不要把大体积生成 `.c` 盲目加入工程，先评估 Flash。
- 如果用户已经能显示素材，不要绕回验证链路，优先解决压缩/运行时问题。
