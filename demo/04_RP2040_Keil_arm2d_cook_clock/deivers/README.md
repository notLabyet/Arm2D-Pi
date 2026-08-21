# Low-Level Device Drivers

This directory contains reusable chip-level drivers. The directory name
`deivers` is kept as-is because the Keil project already references it.

Application code should normally use the wrappers in `application/` when they
exist. The files here expose the lower-level register and bus operations.

## Driver Map

### `drv_QMI8658.c/.h`

QMI8658 six-axis IMU driver.

- Bus: `I2C_PORT` (`i2c0` by default)
- Address: `Device_Address` (`0x6B`)
- Public init: `QMI8658A_Init()`
- Raw read: `QMI8658A_ReadData(int16_t data[6])`
- Converted read: `QMI8658A_Get_G_DPS(float data[6])`

Converted output order:

```text
data[0..2] = acceleration in g
data[3..5] = angular rate in dps
```

Startup self-test, on-demand calibration, and still calibration are disabled by
default to keep boot fast. Enable them with:

```c
#define QMI8658_STARTUP_SELF_TEST          1
#define QMI8658_STARTUP_COD                1
#define QMI8658_STARTUP_STILL_CALIBRATION  1
```

The board wrapper in `application/qmi8658c_task.c` configures the RP2040 I2C
pins and then calls this driver.

### `bm8563.c/.h`

Hardware-independent BM8563 RTC driver.

- Bus address: `BM8563_ADDRESS` (`0x51`)
- The driver is HAL-based: provide `read` and `write` callbacks in `bm8563_t`.
- Main APIs: `bm8563_init()`, `bm8563_read()`, `bm8563_write()`, and
  `bm8563_ioctl()`.

The board wrapper in `application/bm8563_task.c` connects these callbacks to the
shared I2C read/write helpers.

### `drv_paj7620.c/.h`

PAJ7620 gesture sensor driver.

- Uses banked register access.
- Main APIs: `paj7620Init()`, `paj7620ReadReg()`, `paj7620WriteReg()`, and
  `paj7620SelectBank()`.
- Gesture flags are defined by `GES_enum`.

### `drv_ir.c/.h`

Crystal Mouse infrared transmitter/receiver driver.

- TX: `GPIO28`
- RX: `GPIO22`
- Default carrier: `38 kHz`
- Public APIs: `drv_ir_init()`, `drv_ir_set_carrier()`,
  `drv_ir_send_byte_start()`, `drv_ir_receive_snapshot()`, and
  `drv_ir_decode_capture()`.

Transmission is non-blocking. `drv_ir_send_byte_start()` starts a frame and a
Pico alarm callback advances each mark/space interval.

### `drv_light.c/.h`

GL5528 light sensor driver.

- ADC: `GPIO26 / ADC0`
- Divider: `3V3 -> 10k -> ADC node -> GL5528 -> GND`
- Public APIs: `drv_light_init()`, `drv_light_read()`,
  `drv_light_ldr_ohm_from_mv()`, and `drv_light_lux_x100_from_ohm()`.

The lux value is an estimate. Tune `DRV_LIGHT_GL5528_R10_OHM` after measuring a
known reference illuminance.

### `drv_buzzer.c/.h`

DET402 passive buzzer PWM/PCM driver.

- Output: `GPIO23`
- Tone API: `drv_buzzer_set_tone()`
- Note-table API: `drv_buzzer_score_start()` and `drv_buzzer_score_task()`
- PCM API: `drv_buzzer_pcm_start()`, `drv_buzzer_pcm_stop()`, and
  `drv_buzzer_pcm_is_active()`

PCM playback uses a PWM carrier plus a PWM interrupt sample clock. Application
code only needs to poll status or restart playback when desired.

## Editing Notes

- Keep chip-register names close to the datasheet.
- Put board-specific pin choices in `application/` wrappers or config headers.
- Prefer adding short comments near public APIs and non-obvious register
  sequences instead of rewriting third-party driver style everywhere.
