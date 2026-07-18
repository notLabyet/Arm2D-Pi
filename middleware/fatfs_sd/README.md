# FatFs SD Middleware

This directory contains the SD card middleware used by the RP2040 Keil
project. It combines ChaN FatFs with the RP2040 SDIO/SPI block driver from
`carlk3/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico`.

Most files in this tree are third-party middleware or low-level port code. Avoid
large style-only edits unless you are fixing a real bug.

## Directory Map

- `ff15/source/`
  ChaN FatFs source files. `ff.c`, `ff.h`, `ffconf.h`, `diskio.h`,
  `ffsystem.c`, and `ffunicode.c` are the filesystem core.

- `include/`
  Public middleware headers used by the application and SD driver.

- `port/`
  Board-specific wiring and FatFs hardware configuration.

- `sd_driver/`
  RP2040 SD card block driver. This includes common card logic, DMA helpers,
  SPI mode, and SDIO mode.

- `src/`
  Utility glue between FatFs, board timing, debug output, and the block driver.

## Board Configuration

The board-specific SDIO configuration is in:

```text
port/rp2040_sdcard_config.h
port/hw_config.c
```

Current default SDIO pins:

```text
CMD  = GPIO4
D0   = GPIO5
CLK  = D0 - 2 = GPIO3
D1   = GPIO6
D2   = GPIO7
D3   = GPIO8
```

The SDIO PIO program requires `CLK = D0 - 2` and `D1..D3 = D0 + 1..3`. If the
schematic changes, update `RP2040_SDIO_CMD_GPIO` and `RP2040_SDIO_D0_GPIO`.

## Application Entry Points

Most application code should not call low-level SD driver functions directly.
Use the wrapper in `application/rp2040_sdcard.c`:

```c
FRESULT fr = rp2040_sdcard_mount();
```

For resource streaming into Arm-2D, use:

```c
arm_loader_io_fatfs_init(&io, "asset.lmsk");
```

## Debugging Mount Failures

`rp2040_sdcard_mount()` prints extra diagnostics if `f_mount()` fails:

- disk status before/after init
- card sector count
- LBA0 boot-sector-like fields
- MBR partition entries
- first partition boot-sector-like fields

This is intentionally verbose because SD card failures are often caused by pin
wiring, card format, or an out-of-range partition table.

## Notes

- `DMA_IRQ_1` is used by the SDIO configuration in this project.
- `ffconf.h` controls FatFs features such as long filename support and codepage.
- Keep middleware files close to upstream where practical; put board behavior in
  `port/` or the application wrapper instead.
