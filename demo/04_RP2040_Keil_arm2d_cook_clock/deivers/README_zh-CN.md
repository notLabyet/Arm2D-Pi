# 底层设备驱动

此目录包含可复用的芯片级驱动。由于 Keil 工程已引用目录名，
`deivers` 保持原样。

应用程序代码通常应优先使用已有的 `application/` 封装层。这里的文件提供
更底层的寄存器和总线操作。

## 驱动映射

### `drv_QMI8658.c/.h`

QMI8658 六轴 IMU 驱动。

- 总线：`I2C_PORT`（默认值为 `i2c0`）
- 地址：`Device_Address`（`0x6B`）
- 公共初始化函数：`QMI8658A_Init()`
- 原始数据读取：`QMI8658A_ReadData(int16_t data[6])`
- 转换后数据读取：`QMI8658A_Get_G_DPS(float data[6])`

转换后输出顺序：

```text
data[0..2] = 单位为 g 的加速度
data[3..5] = 单位为 dps 的角速度
```

为缩短启动时间，启动自检、按需校准和静止校准默认关闭。可通过以下配置启用：

```c
#define QMI8658_STARTUP_SELF_TEST          1
#define QMI8658_STARTUP_COD                1
#define QMI8658_STARTUP_STILL_CALIBRATION  1
```

`application/qmi8658c_task.c` 中的开发板封装层会配置 RP2040 的 I2C 引脚，
然后调用此驱动。

### `bm8563.c/.h`

硬件无关的 BM8563 RTC 驱动。

- 总线地址：`BM8563_ADDRESS`（`0x51`）
- 驱动基于 HAL：在 `bm8563_t` 中提供 `read` 和 `write` 回调。
- 主要 API：`bm8563_init()`、`bm8563_read()`、`bm8563_write()` 和
  `bm8563_ioctl()`。

`application/bm8563_task.c` 中的开发板封装层会将这些回调连接至共享的 I2C
读写辅助函数。

### `drv_paj7620.c/.h`

PAJ7620 手势传感器驱动。

- 使用分组寄存器访问。
- 主要 API：`paj7620Init()`、`paj7620ReadReg()`、`paj7620WriteReg()` 和
  `paj7620SelectBank()`。
- 手势标志由 `GES_enum` 定义。

### `drv_ir.c/.h`

Crystal Mouse 红外收发驱动。

- 发送端：`GPIO28`
- 接收端：`GPIO22`
- 默认载波：`38 kHz`
- 公共 API：`drv_ir_init()`、`drv_ir_set_carrier()`、
  `drv_ir_send_byte_start()`、`drv_ir_receive_snapshot()` 和
  `drv_ir_decode_capture()`。

发送采用非阻塞方式。`drv_ir_send_byte_start()` 启动一帧发送，Pico alarm 回调会
推进每个标记/空闲时间间隔。

### `drv_light.c/.h`

GL5528 光敏传感器驱动。

- ADC：`GPIO26 / ADC0`
- 分压电路：`3V3 -> 10k -> ADC 节点 -> GL5528 -> GND`
- 公共 API：`drv_light_init()`、`drv_light_read()`、
  `drv_light_ldr_ohm_from_mv()` 和 `drv_light_lux_x100_from_ohm()`。

照度值为估算结果。测量已知参考照度后，请调整 `DRV_LIGHT_GL5528_R10_OHM`。

### `drv_buzzer.c/.h`

DET402 无源蜂鸣器 PWM/PCM 驱动。

- 输出：`GPIO23`
- 音调 API：`drv_buzzer_set_tone()`
- 音符表 API：`drv_buzzer_score_start()` 和 `drv_buzzer_score_task()`
- PCM API：`drv_buzzer_pcm_start()`、`drv_buzzer_pcm_stop()` 和
  `drv_buzzer_pcm_is_active()`

PCM 播放使用 PWM 载波和 PWM 中断采样时钟。应用程序代码只需按需轮询状态或重新
启动播放。

## 编辑说明

- 芯片寄存器名称应尽量与数据手册保持一致。
- 将开发板专用的引脚选择放在 `application/` 封装层或配置头文件中。
- 应优先在公共 API 和不直观的寄存器操作序列附近添加简短注释，而非到处重写
  第三方驱动的代码风格。