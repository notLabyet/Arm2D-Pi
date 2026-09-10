/* Hardware configuration for carlk3/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico. */

#include <stddef.h>

#include "hardware/irq.h"
#include "hw_config.h"
#include "rp2040_sdio.pio.h"
#include "rp2040_sdcard_config.h"

static sd_sdio_if_t s_rp2040_sdio_if = {
    /*
     * SDIO pin layout is defined in rp2040_sdcard_config.h. The upstream RP2040
     * SDIO PIO code derives CLK/D1/D2/D3 from D0, so keep those constraints in
     * mind when changing board routing.
     */
    .CMD_gpio = RP2040_SDIO_CMD_GPIO,
    .D0_gpio = RP2040_SDIO_D0_GPIO,
    .DMA_IRQ_num = DMA_IRQ_1,
    .use_exclusive_DMA_IRQ_handler = true,
    .baud_rate = RP2040_SDIO_BAUD_RATE,
};

static sd_card_t s_rp2040_sd_card = {
    .type = SD_IF_SDIO,
    .sdio_if_p = &s_rp2040_sdio_if,
};

size_t sd_get_num(void)
{
    /* This board exposes one SD card slot. */
    return 1u;
}

sd_card_t *sd_get_by_num(size_t num)
{
    /* FatFs glue asks for card 0 during disk_initialize/read/write. */
    if (0u == num) {
        return &s_rp2040_sd_card;
    }

    return NULL;
}
