/* Hardware configuration for carlk3/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico. */

#include <stddef.h>

#include "hardware/irq.h"
#include "hw_config.h"
#include "rp2040_sdio.pio.h"
#include "tufty_sdcard_config.h"

static sd_sdio_if_t s_tufty_sdio_if = {
    .CMD_gpio = TUFTY_SDIO_CMD_GPIO,
    .D0_gpio = TUFTY_SDIO_D0_GPIO,
    .DMA_IRQ_num = DMA_IRQ_1,
    .use_exclusive_DMA_IRQ_handler = true,
    .baud_rate = TUFTY_SDIO_BAUD_RATE,
};

static sd_card_t s_tufty_sd_card = {
    .type = SD_IF_SDIO,
    .sdio_if_p = &s_tufty_sdio_if,
};

size_t sd_get_num(void)
{
    return 1u;
}

sd_card_t *sd_get_by_num(size_t num)
{
    if (0u == num) {
        return &s_tufty_sd_card;
    }

    return NULL;
}
