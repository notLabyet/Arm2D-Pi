/* Use the generic 03h boot stage for Flash devices without W25Q-specific QE handling. */
#ifndef RP2040_BOOT2_CONFIG_H
#define RP2040_BOOT2_CONFIG_H

#include "pico/config_autogen.h"

#undef PICO_BOOT_STAGE2_CHOOSE_W25Q080
#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 0
#define PICO_BOOT_STAGE2_CHOOSE_GENERIC_03H 1

#undef PICO_FLASH_SPI_CLKDIV
#define PICO_FLASH_SPI_CLKDIV 6

#endif
