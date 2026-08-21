---
name: board-hardware-0805
description: "Crystal_Mouse 2026-08-05 原理图硬件资源总览（模块、供电、GPIO/总线映射）"
argument-hint: "查询板子包含的模块、对应硬件资源、引脚和总线分配"
user-invocable: true
---

# Crystal_Mouse 0805 硬件总览 Skill

本 Skill 基于以下资料整理：
- 原理图：原理图/SCH_Crystal_Mouse_2026-08-05.pdf（4 页）
- 交叉校验：demo/04_RP2040_Keil_arm2d_cook_clock 中现有驱动与配置（如 `bsp_cfg.h`、`drv_*.h`）

用途：快速回答“当前板子有哪些模块、用了哪些硬件资源、对应哪些引脚/总线”。

## 1. 主控与时钟

- 主控：RP2040
- 主时钟晶振：12MHz（X2）
- 调试接口：SWD（SWDIO / SWCLK，CN2）

## 2. 存储与可移除介质

- 外部 Flash：W25Q128JVPIM（QSPI）
  - QSPI_CS, QSPI_SCLK, QSPI_SD0~SD3
- TF / MicroSD：ZDSD01GLGEAG（4-bit SDIO 风格连接）
  - SD_CLK, SD_CMD, SD0, SD1, SD2, SD3
  - 具上拉电阻网络（R34~R37 10k）

## 3. 显示与背光

- LCD 接口：FPC 8080 并口（页名 lcd_8080）
- 数据线：LCD_DB0~LCD_DB7
- 控制线：LCD_CS, LCD_DC, LCD_WR, LCD_RD
- 其他：LCD_BK（背光控制），RST（复位相关）

## 4. 传感器与外设模块

- IMU：QMI8658A（IIC0）
  - 网名：IIC0_SDA / IIC0_SCL
  - INT 引脚在图中可见（INT1/INT2）
- RTC：BM8563ESA（IIC0）
  - 32.768kHz 晶振（X1）
  - IIC0_SDA / IIC0_SCL
- 温湿度：SHT40A-AD1B-R2（IIC0）
- 光感：LIGHT_SENSE（分压采样到 ADC）
- 蜂鸣器：BUZZER1（DET402 路径，MOS 管驱动）
- 红外发射：INFRARED_PWM（IR LED + MOS 驱动）

## 5. 电源与充电管理

- 输入：USB Type-C（16P）
- 电池接口：V_BAT（含连接器与保护/滤波）
- 充电芯片：ETA6002E8A（页名 bat_dcdc）
- Buck 转换：SM8082AAAC（生成 +3V3）
- 关键电源网：+5V, V_BAT, V_BAT_O, SYS_V_OUT, +3V3, 1V1
- 上电自保持/按键保持电路：POWER_KEEP_PIN, POWER_UP_CHECK, POWER_UP_KEY
- 电量/充电反馈采样：BAT_VOLTAGE, CHGING_FB（ADC）

## 6. RP2040 资源映射（按原理图网名整理）

以下映射以 0805 原理图文本与工程命名综合整理：

- GPIO0  -> IIC0_SDA
- GPIO1  -> IIC0_SCL
- GPIO2  -> POWER_KEEP_PIN
- GPIO3  -> SD_CLK
- GPIO4  -> SD_CMD
- GPIO5  -> SD0
- GPIO6  -> SD1
- GPIO7  -> SD2
- GPIO8  -> SD3
- GPIO9  -> POWER_UP_CHECK
- GPIO10 -> LCD_CS
- GPIO11 -> LCD_DC
- GPIO12 -> LCD_WR
- GPIO13 -> LCD_RD
- GPIO14 -> LCD_DB0
- GPIO15 -> LCD_DB1
- GPIO16 -> LCD_DB2
- GPIO17 -> LCD_DB3
- GPIO18 -> LCD_DB4
- GPIO19 -> LCD_DB5
- GPIO20 -> LCD_DB6
- GPIO21 -> LCD_DB7
- GPIO22 -> LED_RUN
- GPIO23 -> BEEF_EN（蜂鸣器使能/驱动控制）
- GPIO24 -> 网名 GPIO9（通用/保留，建议以最新 PCB/源码再确认）
- GPIO25 -> LCD_BK
- GPIO26 (ADC0) -> LIGHT_SENSE
- GPIO27 (ADC1) -> BAT_VOLTAGE
- GPIO28 (ADC2) -> INFRARED_PWM
- GPIO29 (ADC3) -> CHGING_FB

## 7. 与当前工程代码的对应关系

- I2C 总线：`application/bsp_cfg.h`
  - `I2C_PORT = i2c0`
  - `I2C_SDA = GPIO0`
  - `I2C_SCL = GPIO1`
- 蜂鸣器：`deivers/drv_buzzer.h`
  - `DRV_BUZZER_PIN = GPIO23`
- 光感：`deivers/drv_light.h`
  - LIGHT_SENSE 使用 ADC0（GPIO26）
- 红外：`deivers/drv_ir.h`
  - INFRARED_PWM 使用 GPIO28

## 8. “模块存在性”说明（避免误判）

- 在 2026-08-05 这版原理图中，能明确看到：RP2040、W25Q128、BM8563、QMI8658、SHT40、LCD 并口、TF 卡、Type-C、电池充电与 Buck、电源保持、红外、蜂鸣器、光感。
- 某些驱动文件中可能出现的器件（例如手势传感器 PAJ7620）未在该版原理图中明确出现；如需确认请以最新原理图/PDF 或 PCB 网表为准。

## 9. 维护规则（两份 Skill 一致性）

本文件需要与以下路径保持逐字一致：
- 原理图/board-hardware-0805/SKILL.md
- demo/04_RP2040_Keil_arm2d_cook_clock/.github/skills/board-hardware-0805/SKILL.md

每次更新请同时改动两处，提交前建议进行文本 diff 校验。
