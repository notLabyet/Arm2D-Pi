/* SDIO pin configuration for the RP2040 FatFs SD driver.
 *
 * The PIO program requires:
 *   CLK = (D0 - 2) modulo 32
 *   D1  = D0 + 1
 *   D2  = D0 + 2
 *   D3  = D0 + 3
 *
 * These defaults avoid the pins already used by the current RP2040 demo
 * project. Change them to match your board wiring before using real hardware.
 */
#ifndef RP2040_SDCARD_CONFIG_H
#define RP2040_SDCARD_CONFIG_H

#define RP2040_SDIO_CMD_GPIO      4u
#define RP2040_SDIO_D0_GPIO       5u
#define RP2040_SDIO_BAUD_RATE     (125u * 1000u * 1000u / 6u)

#endif
