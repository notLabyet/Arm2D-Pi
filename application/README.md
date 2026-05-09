# Application Layer Drivers

This directory contains board-level modules that are meant to be called by
`main.c` or by Arm-2D scenes. Files here usually wrap lower-level drivers into
small test tasks or application services.

## Peripheral Task Modules

### `ir_task.c/.h`

Application-level infrared transmitter/receiver self-test task.

- Driver: `deivers/drv_ir.c/.h`
- TX pin: `GPIO28` (`DRV_IR_TX_PIN`)
- RX pin: `GPIO22` (`DRV_IR_RX_PIN`)
- Default carrier: `38 kHz`
- Default duty: `33.3%`

Typical use:

```c
ir_task_init();

while (true) {
    ir_task(1000);   /* Run one loopback send/check every 1000 ms. */
}
```

`ir_task()` periodically starts one non-blocking loopback frame and later checks
the captured receiver envelope. The driver uses a Pico alarm callback to keep
the IR burst timing out of the main loop.

### `light_task.c/.h`

Application-level GL5528 light sensor ADC task.

- Driver: `deivers/drv_light.c/.h`
- ADC pin: `GPIO26 / ADC0`
- Divider model: `3V3 -> 10k pull-up -> ADC node -> GL5528 -> GND`
- Output unit: lux multiplied by 100 (`wLuxX100`)

Typical use:

```c
light_task_init();

while (true) {
    light_task(500);     /* Print one sample every 500 ms. */
}
```

The lux conversion is an estimate based on the GL5528 resistance curve. Calibrate
`DRV_LIGHT_GL5528_R10_OHM` with a known 10 lux reference if you need better
absolute accuracy.

### `buzzer_task.c/.h`

Application-level DET402 passive buzzer playback task.

- Driver: `deivers/drv_buzzer.c/.h`
- Output pin: `GPIO23`
- Default mode: 8-bit PCM playback generated from `Keil.fc`
- PCM sample rate: defined by `KEIL_FC14_PCM_SAMPLE_RATE_HZ`
- PWM carrier: `DRV_BUZZER_PCM_CARRIER_HZ`

Typical use:

```c
buzzer_task_init();

while (true) {
    buzzer_task(900);    /* Loop playback with a 900 ms pause. */
}
```

`buzzer_task()` restarts playback after the configured pause. The actual
PCM sample updates are driven by `PWM_IRQ_WRAP`, so the main loop does not need
to run at the audio sample rate.

To write your own application logic, call the lower-level `drv_buzzer_*()` APIs
directly from another task file.

## SD Card and Arm-2D Loading

### `tufty_sdcard.c/.h`

Small FatFs helper layer used by tests and content loading:

- `tufty_sdcard_mount()`
- `tufty_sdcard_read_file()`
- `tufty_sdcard_write_file()`
- `tufty_sdcard_perf_test()`

This layer owns the FatFs mount state and prints extra diagnostics if mounting
fails.

### `arm_loader_io_fatfs.c/.h`

Arm-2D loader I/O adapter for reading QOI/LMSK resources from FatFs files. Use
`arm_loader_io_fatfs_init()` with a path, then pass `ARM_LOADER_IO_FATFS` to the
Arm-2D loader code.

The optional read cache is controlled by:

- `ARM_LOADER_IO_FATFS_CACHE_SIZE`
- `ARM_LOADER_IO_FATFS_CACHE_WAYS`
- `ARM_LOADER_IO_FATFS_USE_CACHE`

Set cache size or ways to zero to force direct `f_lseek()` + `f_read()` reads.

## Other Application Modules

- `qmi8658c_task.c/.h`: board task wrapper for the QMI8658 IMU.
- `bm8563_task.c/.h`: board task wrapper for the BM8563 RTC.
- `usb_mouse.c/.h`: USB HID mouse behavior.
- `usb_msc_sd.c/.h`: USB mass-storage bridge for the SD card.
- `tufty_qoi_scene.c/.h`, `tufty_lmsk_scene.c/.h`: Arm-2D scene loading from
  SD card resources.
