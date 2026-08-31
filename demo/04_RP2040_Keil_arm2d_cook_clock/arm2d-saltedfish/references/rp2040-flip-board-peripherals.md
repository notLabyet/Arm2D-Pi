# RP2040 Flip 板卡外设接入规范

本文用于当前 RP2040 Flip / Crystal Mouse 类工程的板级外设整理，以及这些
外设与 Arm-2D scene 的协作。它记录仓库中已经存在的真实接口，同时定义后续
封装应遵守的统一约定。

不要把本文中的引脚、I2C 地址或任务周期直接套用到其它板卡。开始修改前仍需
核对原理图、`application/bsp_cfg.h`、Keil 工程文件和当前分支代码。

## 目录

- [适用范围与代码分层](#适用范围与代码分层)
- [当前资源总表](#当前资源总表)
- [统一服务层合同](#统一服务层合同)
- [共享 I2C 总线](#共享-i2c-总线)
- [QMI8658 六轴 IMU](#qmi8658-六轴-imu)
- [电源按键](#电源按键)
- [IR 发射与接收](#ir-发射与接收)
- [GL5528 光敏传感器](#gl5528-光敏传感器)
- [温湿度传感器](#温湿度传感器)
- [BM8563 RTC](#bm8563-rtc)
- [PAJ7620 手势传感器](#paj7620-手势传感器)
- [蜂鸣器](#蜂鸣器)
- [推荐调度周期](#推荐调度周期)
- [Arm-2D scene 接入](#arm-2d-scene-接入)
- [当前缺口与修改原则](#当前缺口与修改原则)
- [验证清单](#验证清单)

## 适用范围与代码分层

当前工程主要按以下层次组织：

```text
deivers/                 芯片/寄存器级驱动，目录名按现有 Keil 工程保留
application/             板级初始化、周期任务、缓存和应用语义
main.c                   外设服务调度
arm_2d_scene_*.c/.h      只消费应用层快照，不直接访问总线
```

遵守以下边界：

- `deivers/` 负责寄存器、GPIO、ADC、PWM 或总线事务，不包含 scene 逻辑。
- `application/` 负责板卡引脚、初始化顺序、采样周期、去抖、缓存和事件。
- `main.c` 或独立调度器持续调用非阻塞 `poll/task`。
- Arm-2D scene 只读取缓存快照。PFB 绘制回调中禁止 I2C、ADC 多次采样、
  `sleep_ms()`、`sleep_us()` 和高频 `printf()`。

## 当前资源总表

| 外设 | 当前实现 | 总线/引脚 | 当前主要入口 | 成功约定 |
| --- | --- | --- | --- | --- |
| QMI8658 六轴 IMU | `deivers/drv_QMI8658.*`、`application/qmi8658c_task.*`、`application/qmi8658_motion.*` | I2C0，GPIO0/1，地址 `0x6B` | `qmi8658c_init()`、`qmi8658_motion_poll()` | 初始化和底层读写以 `1` 表示成功 |
| 电源按键 | 当前位于 `main.c::power_task()` | 按键 GPIO9，低有效；电源保持 GPIO2 | `power_task()` | 返回 `fsm_rt_on_going` |
| IR 发射/接收 | `deivers/drv_ir.*`、`application/ir_task.*` | TX GPIO28，RX GPIO22，默认 38 kHz | `drv_ir_*()`、`ir_task()` | `bool`，`true` 表示操作接受或数据有效 |
| GL5528 光敏 | `deivers/drv_light.*`、`application/light_task.*` | GPIO26 / ADC0 | `drv_light_read()`、`light_task()` | 结构体直接返回；任务层当前只打印 |
| 温湿度传感器 | 当前仓库未找到专用 `.c/.h` | 必须按原理图确认 | 尚无可引用 API | 不得虚构 |
| BM8563 RTC | `deivers/bm8563.*`、`application/bm8563_task.*` | 共享 I2C0，地址 `0x51` | `bm8563_*()`、`bm8563_hander_init()` | 底层以 `BM8563_OK == 0` 表示成功 |
| PAJ7620 手势 | `deivers/drv_paj7620.*` | 共享 I2C0，地址 `0x73` | `paj7620Init()`、`paj7620ReadReg()` | `0` 成功，`0xFF` 表示芯片 ID 不匹配 |
| DET402 蜂鸣器 | `deivers/drv_buzzer.*`、`application/buzzer_task.*` | GPIO23 / PWM | `drv_buzzer_*()`、`buzzer_task()` | 启动类接口通常为 `bool` |

SD 卡、TinyUSB MSC/HID 和显示适配器属于其它子系统，不在本文的传感器服务
合同内；涉及它们时分别查工程现有模块和其它 skill 参考。

## 统一服务层合同

当前驱动的命名、返回值和数据暴露方式并不统一。不要为了统一而直接改写第三方
芯片驱动；应在 `application/` 新增薄服务层，把旧接口适配为一致合同。

推荐的新服务遵守以下形态。下列名称是后续封装规范，不代表当前仓库已经存在：

```c
typedef enum board_peripheral_state_t {
    BOARD_PERIPHERAL_UNINITIALIZED = 0,
    BOARD_PERIPHERAL_READY,
    BOARD_PERIPHERAL_STALE,
    BOARD_PERIPHERAL_ERROR,
} board_peripheral_state_t;

typedef struct board_sample_meta_t {
    uint32_t timestamp_ms;
    uint32_t sequence;
    bool valid;
} board_sample_meta_t;

bool board_xxx_init(void);
bool board_xxx_poll(uint32_t current_ms);
bool board_xxx_get_latest(board_xxx_sample_t *sample);
board_peripheral_state_t board_xxx_get_state(void);
```

统一语义：

- `init()` 只做一次资源初始化和芯片探测，`true` 表示服务可用。
- `poll()` 必须可重复调用且不长时间阻塞；仅在生成新样本或新事件时返回
  `true`。
- `get_latest()` 不访问硬件，只复制最近一次完整快照；无有效数据时返回
  `false`。
- 每份样本带毫秒时间戳、递增序号和有效状态。消费者通过 `sequence` 判断
  数据是否变化。
- 连续失败不能继续把旧值伪装成新值。超过服务定义的时限后状态转为
  `STALE`，总线或芯片错误转为 `ERROR`。
- 输入指针为 `NULL` 时不得解引用；除非旧驱动合同明确允许，否则返回失败。
- ISR 只做采集、置位或推进短状态机，不做 I2C、格式化输出和 Arm-2D 绘制。

### 单位和字段命名

接口中必须从字段名看出单位，避免只写 `value`、`temperature` 或
`light`：

| 量 | 推荐单位/命名 |
| --- | --- |
| 加速度 | 原始值明确写 `raw`；工程定点快照优先 `accel_mg[3]` 或明确的 Q15 |
| 角速度 | `gyro_mdps[3]`，或现有浮点接口注明单位为 dps |
| 环境温度 | `temperature_centi_c`，即摄氏度乘 100 |
| 相对湿度 | `humidity_centi_percent`，即 %RH 乘 100 |
| 照度 | 沿用 `lux_x100` / `wLuxX100` |
| 电压 | `millivolt` / `wMilliVolt` |
| 时间 | `timestamp_ms`；RTC 日历使用 `struct tm` |
| 持续时间 | 后缀 `_ms` 或 `_us`，不可混用 |

RP2040 没有硬件 FPU。UI 高频路径优先使用已有整数/Q15 结果，不要为了显示一个
数值在每帧重复进行浮点换算。

## 共享 I2C 总线

当前传感器总线配置为：

```text
I2C controller: i2c0
SDA: GPIO0
SCL: GPIO1
Default speed in qmi8658c_init(): 400 kHz
```

现状中 `iic0_read_bytes()` 和 `iic0_write_bytes()` 实现在
`deivers/drv_QMI8658.c`，RTC 和 PAJ7620 也依赖它们。这意味着只要移除 QMI
驱动源文件，另外两个设备的传输层也会失效。

后续整理时应把共享传输移动到独立的板级 I2C 模块，例如
`application/board_sensor_i2c.c/.h`，由它唯一负责：

- I2C0 和 GPIO0/1 初始化。
- 统一 `read_reg/write_reg` 返回值。
- 超时、重试和可选总线恢复。
- 多核或多任务环境中的互斥。

不要让每个传感器重复调用 `i2c_init()` 或重新配置同一组引脚。裸机单循环下
也要保证一次事务完整结束后再访问下一个设备；不要在 GPIO/ADC/PWM ISR 中发起
I2C 事务。

## QMI8658 六轴 IMU

### 当前接口

```c
uint8_t qmi8658c_init(void);                 /* 1 success, 0 failure */
int QMI8658A_ReadData(int16_t data[6]);      /* [AX, AY, AZ, GX, GY, GZ] */
void QMI8658A_Get_G_DPS(float data[6]);      /* accel: g, gyro: dps */

void qmi8658_motion_init(void);
bool qmi8658_motion_poll(uint32_t current_ms);
bool qmi8658_motion_get_position(int16_t *x, int16_t *y);
bool qmi8658_motion_get_gyro_offset_xyz(int16_t *x, int16_t *y, int16_t *z);
bool qmi8658_motion_get_fire_motion(qmi8658_fire_motion_t *motion);
bool qmi8658_motion_consume_z_tap(void);
bool qmi8658_motion_is_ready(void);
```

标准启动顺序：

```c
bool imu_ready = qmi8658c_init() != 0u;

if (imu_ready) {
    qmi8658_motion_init();
}

/* main loop */
if (imu_ready) {
    (void)qmi8658_motion_poll(get_system_ms());
}
```

`qmi8658_motion_poll()` 默认 20 ms 采样一次，并在成功读到新数据时返回
`true`。运动映射需要累计校准样本，`qmi8658_motion_is_ready()` 为真后再把
位置或偏移用于交互。

注意：

- `QMI8658A_Get_G_DPS()` 当前忽略底层读失败；需要可靠错误处理时先调用
  `QMI8658A_ReadData()`，成功后再转换。
- 原始数组顺序固定为加速度 XYZ 后角速度 XYZ。
- scene 优先读取 `qmi8658_motion_*get*` 的缓存结果，不直接调用 I2C 读取。
- 坐标轴和正负号由 `qmi8658_motion.h` 的映射宏控制，换安装方向时先改映射，
  不要在多个 scene 中分别交换轴。

## 电源按键

当前按键不是通用 UI key 驱动，而是 `main.c` 内的电源状态机：

```text
POWER_UP_CHECK_PIN = GPIO9, input pull-up, active low
POWER_KEEP_PIN     = GPIO2, output high keeps board powered
debounce           = 20 ms
power-off hold     = 1000 ms
```

`power_task()` 必须在主循环持续调用。它处理上电来源、松手确认、再次长按和
拉低保持脚关机。

规范要求：

- 不要在 scene 中直接读取 GPIO9，否则会与电源状态机产生重复去抖和事件竞争。
- UI 需要按下、释放、短按或长按事件时，应先把状态机迁移到独立
  `application/power_key_service.c/.h`，再暴露缓存事件。
- 电源保持逻辑和普通按键事件必须仍由同一个服务拥有；scene 只能消费事件。
- 若增加其它按键，每个按键都要声明有效电平、上下拉、去抖时间、长按阈值和
  事件是否锁存。

## IR 发射与接收

### 当前接口

```c
void drv_ir_init(uint32_t carrier_hz, uint16_t duty_permille);
bool drv_ir_send_byte_start(uint8_t data);
bool drv_ir_send_task(void);
bool drv_ir_is_sending(void);
void drv_ir_reset_rx(void);
bool drv_ir_receive_snapshot(drv_ir_rx_capture_t *capture);
bool drv_ir_decode_capture(const drv_ir_rx_capture_t *capture, uint8_t *data);
```

`drv_ir_send_byte_start()` 只装载发送序列并开始第一步。当前实现依赖主循环频繁
调用 `drv_ir_send_task()` 推进微秒级 mark/space，不是后台 alarm 自动完成。
调用间隔若接近或超过最短脉冲宽度，发送波形会失真。

`ir_task_init()` + `ir_task(period_ms)` 是环回自检任务，会周期发送字节、等待
接收并打印 PASS/FAIL。产品功能应根据协议直接封装 `drv_ir_*()`，不要把自检
打印任务当成通用 IR 服务。

接收 GPIO IRQ 只积累边沿数据；主循环通过 `drv_ir_receive_snapshot()` 获取
一次完整快照，再调用解码器。scene 只使用已经解码的事件，不读取捕获数组。

## GL5528 光敏传感器

### 当前接口

```c
void drv_light_init(void);
drv_light_sample_t drv_light_read(uint16_t sample_count);
```

`drv_light_sample_t` 包含原始 ADC、毫伏、LDR 电阻和 `wLuxX100`。当前
`light_task()` 默认每 500 ms 读取并打印一次，没有对 scene 暴露缓存 getter。

默认一次读取平均 32 个样本，每个样本间隔 50 us，至少会占用约 1.6 ms，且照度
换算使用浮点 `pow`。因此：

- 不得在 PFB draw handler 中调用 `drv_light_read()`。
- 应在应用服务中低频采样并缓存整数 `lux_x100`。
- `DRV_LIGHT_GL5528_R10_OHM` 和 `DRV_LIGHT_GL5528_GAMMA` 是估算参数；
  需要绝对照度时必须用已知光源标定。
- `wLdrOhm == UINT32_MAX` 表示接近开路，不应显示为一个普通电阻值。

## 温湿度传感器

本次盘点没有在 `deivers/`、`application/` 或 Keil 工程文件中找到独立的
AHT、SHT、DHT 或其它温湿度驱动。QMI8658 的内部温度寄存器不是环境温湿度
传感器，不能代替。

新增驱动前必须先确认：

- 实际芯片型号、7 位 I2C 地址或单总线引脚。
- 供电、上拉和总线速率。
- 测量命令后的典型/最大转换时间。
- CRC、校验和、量程以及上电稳定时间。

应用层快照至少包含：

```c
typedef struct board_environment_sample_t {
    board_sample_meta_t meta;
    int32_t temperature_centi_c;
    uint32_t humidity_centi_percent;
} board_environment_sample_t;
```

如果芯片需要“启动测量 -> 等待 -> 读取结果”，必须用非阻塞状态机跨多次
`poll()` 完成。禁止在 scene 或高频主循环路径中用长时间 `sleep_ms()` 等待
传感器转换。

## BM8563 RTC

### 底层接口

```c
bm8563_err_t bm8563_init(const bm8563_t *rtc);
bm8563_err_t bm8563_read(const bm8563_t *rtc, struct tm *time);
bm8563_err_t bm8563_write(const bm8563_t *rtc, const struct tm *time);
bm8563_err_t bm8563_ioctl(const bm8563_t *rtc, int16_t command, void *buffer);
```

`BM8563_OK` 为 `0`。读取可能返回 `BM8563_ERR_LOW_VOLTAGE`，表示备份电源
异常，时间值不能无条件当作可信。

`struct tm` 使用 POSIX 约定：

- `tm_year = 实际年份 - 1900`，2026 年应写 `126`，不是 `26`。
- `tm_mon = 0..11`，3 月是 `2`，4 月才是 `3`。
- `tm_wday = 0..6`。

当前 `bm8563_hander_init()` 是 bring-up 包装：

- 每次启动都会写入固定时间，破坏 RTC 跨复位保持的意义。
- 固定值中的 `tm_year = 26` 会按 POSIX 语义表示 1926 年。
- 它忽略底层初始化/写入错误并固定返回 `0`。
- 函数名中的 `hander` 是历史拼写。

因此生产逻辑不应直接沿用其“启动即写时间”行为。应用层应分离
`board_rtc_init()`、`board_rtc_get_latest()` 和显式的
`board_rtc_set_time()`；只有用户设置时间、首次无效或明确恢复默认值时才写
RTC。

## PAJ7620 手势传感器

当前只有芯片层驱动，`main.c` 虽包含头文件但没有初始化或周期读取。

```c
uint8_t paj7620Init(void);                  /* 0 success */
uint8_t paj7620ReadReg(uint8_t reg, uint8_t qty, uint8_t data[]);
void paj7620SelectBank(bank_e bank);
```

`paj7620Init()` 假设共享 I2C 已经初始化。手势结果位于 bank 0 的
`PAJ7620_ADDR_GES_PS_DET_FLAG_0/1`。新增应用服务时，应把寄存器位转换为稳定
的手势枚举或锁存事件，并处理多个 bit 同时出现、重复手势和事件过期；scene
不应直接读寄存器。

## 蜂鸣器

蜂鸣器不是输入传感器，但属于当前板级外设。应用可使用：

```c
void drv_buzzer_init(void);
void drv_buzzer_set_tone(uint16_t freq_hz, uint16_t duty_permille);
bool drv_buzzer_score_start(const drv_buzzer_score_t *score);
bool drv_buzzer_score_task(void);
bool drv_buzzer_pcm_start(const drv_buzzer_pcm_t *pcm);
void drv_buzzer_pcm_stop(void);
```

`buzzer_task()` 当前是循环播放示例。UI 需要按键音或告警音时，应发送短命令到
蜂鸣器服务，由服务管理 PWM/PCM 状态；不要在 draw handler 中启动、停止或轮询
PCM。

## 推荐调度周期

以下是应用层起始建议，不代替芯片数据手册：

| 服务 | 建议调用/采样 |
| --- | --- |
| 电源按键 | 每次主循环调用状态机，去抖使用时间戳 |
| QMI8658 motion | 每次主循环调用 `poll`，内部默认 20 ms 采样 |
| IR TX | 发送期间尽可能高频调用 `drv_ir_send_task()` |
| IR RX | 每次主循环检查完成快照 |
| PAJ7620 | 20-100 ms，按交互需求和芯片模式调整 |
| 光敏 | 200-1000 ms |
| 温湿度 | 500-2000 ms，遵守转换时间 |
| RTC | UI 秒钟通常 1000 ms；闹钟/定时器按需求 |
| 蜂鸣器 score | 每次主循环调用；PCM 由 IRQ 推进 |

所有周期判断都使用无符号差值或显式的有符号 deadline 比较，保证
`uint32_t` 毫秒计数回绕后仍可工作。

## Arm-2D scene 接入

推荐数据流：

```text
main loop poll
    -> application service updates complete snapshot + sequence
        -> scene frame-start reads snapshot once
            -> scene private state
                -> PFB draw handler only renders private state
```

具体规则：

- 在 scene 初始化前完成必要的板级服务初始化，但设备失败不能阻止整个 UI
  启动；scene 应有 unavailable/stale 状态。
- 在 scene 的 frame-start 或独立更新回调中读取一次快照，不要在一个 PFB 帧的
  多个 tile 绘制回调中重复读取。
- 数据 `sequence` 变化后更新 scene 私有字段，并只标记实际变化的 dirty
  region。
- 绘制逻辑使用同一份快照，避免一帧中温度、湿度或时间来自不同采样时刻。
- scene depose 时只释放 scene 自己的资源；共享传感器服务由板级生命周期拥有，
  不要随 scene 切换反复关闭和初始化。
- 按键、手势和 IR 解码结果属于事件。需要锁存或队列，防止 scene 帧率低于事件
  产生速度时丢失。

## 当前缺口与修改原则

本仓库目前还没有真正统一的外设服务层：

- I2C 传输实现与 QMI8658 驱动耦合。
- 电源按键仍在 `main.c`，没有公开事件接口。
- 光敏 task 只打印，没有缓存 getter。
- 温湿度驱动未找到。
- RTC bring-up 包装会覆盖时间且不传播错误。
- PAJ7620 没有应用层服务。
- 各模块成功返回值分别使用 `1`、`0` 和 `bool`。

处理新的 UI/外设需求时，优先做最小薄封装，不要顺手重写所有芯片驱动。若用户
明确要求代码层统一，再按“共享 I2C -> 单个外设服务 -> main 调度 -> scene
快照消费”的顺序逐步迁移，并为每一步保留可构建状态。

## 验证清单

- 用 `rg` 确认驱动声明、实现、Keil 工程项和实际调用点一致。
- 核对 I2C 地址是 7 位地址，且总线只初始化一次。
- 核对每个接口的成功返回值，不按函数名猜测。
- 确认按键有效电平、去抖和长按阈值。
- 确认 RTC 不会在正常启动时被默认时间覆盖。
- 确认温湿度芯片真实存在后再写具体 API。
- 确认 scene 绘制回调没有硬件访问、阻塞等待或高频日志。
- 新增 `.c/.h` 时更新 `project/mdk/template.uvprojx`。
- 代码改动后使用仓库约定的 Keil MDK 构建，并报告错误、警告与
  Code/RO/RW/ZI 变化。
