#include "fal.h"

#include <stdbool.h>
#include <string.h>

#include "fal_cfg.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"
#include "rp2040_flash_layout.h"

#ifndef __not_in_flash_func
#   define __not_in_flash_func(__func)  __func
#endif

#ifndef FLASH_SECTOR_SIZE
#   define FLASH_SECTOR_SIZE            4096u
#endif

#ifndef FLASH_PAGE_SIZE
#   define FLASH_PAGE_SIZE              256u
#endif

#define RP2040_FLASH_ERASE_SIZE         FLASH_SECTOR_SIZE
#define RP2040_FLASH_PAGE_SIZE          FLASH_PAGE_SIZE

static uint8_t s_sector_cache[RP2040_FLASH_ERASE_SIZE];

static bool rp2040_flash_range_valid(long offset, size_t size)
{
    uint32_t u32Offset;

    if (offset < 0) {
        return false;
    }

    u32Offset = (uint32_t)offset;
    if (u32Offset > RP2040_FLASH_TOTAL_SIZE) {
        return false;
    }

    return size <= (RP2040_FLASH_TOTAL_SIZE - u32Offset);
}

static int rp2040_onchip_flash_init(void)
{
    return 0;
}

static int rp2040_onchip_flash_read(long offset, uint8_t *buf, size_t size)
{
    uint32_t u32Offset;

    if ((size > 0u && NULL == buf) ||
        !rp2040_flash_range_valid(offset, size)) {
        return -1;
    }

    if (0u == size) {
        return 0;
    }

    u32Offset = (uint32_t)offset;
    memcpy(buf, (const void *)(uintptr_t)(RP2040_FLASH_XIP_BASE + u32Offset), size);

    return (int)size;
}

static int __not_in_flash_func(rp2040_onchip_flash_erase)(long offset, size_t size)
{
    uint32_t u32Offset;
    uint32_t irqState;

    if (!rp2040_flash_range_valid(offset, size)) {
        return -1;
    }

    u32Offset = (uint32_t)offset;
    if ((u32Offset % RP2040_FLASH_ERASE_SIZE) || (size % RP2040_FLASH_ERASE_SIZE)) {
        return -1;
    }

    if (0u == size) {
        return 0;
    }

    irqState = save_and_disable_interrupts();
    flash_range_erase(u32Offset, size);
    restore_interrupts(irqState);

    return (int)size;
}

static void __not_in_flash_func(rp2040_flash_program_sector)(uint32_t sector_offset)
{
    uint32_t page_offset;

    for (page_offset = 0; page_offset < RP2040_FLASH_ERASE_SIZE; page_offset += RP2040_FLASH_PAGE_SIZE) {
        flash_range_program(sector_offset + page_offset,
                            &s_sector_cache[page_offset],
                            RP2040_FLASH_PAGE_SIZE);
    }
}

static int __not_in_flash_func(rp2040_onchip_flash_write)(long offset,
                                                          const uint8_t *buf,
                                                          size_t size)
{
    uint32_t u32Offset;
    size_t written = 0;

    if ((size > 0u && NULL == buf) ||
        !rp2040_flash_range_valid(offset, size)) {
        return -1;
    }

    if (0u == size) {
        return 0;
    }

    u32Offset = (uint32_t)offset;
    while (written < size) {
        uint32_t sector_offset = u32Offset & ~(RP2040_FLASH_ERASE_SIZE - 1u);
        uint32_t in_sector = u32Offset - sector_offset;
        size_t chunk = RP2040_FLASH_ERASE_SIZE - in_sector;
        uint32_t irqState;

        if (chunk > (size - written)) {
            chunk = size - written;
        }

        memcpy(s_sector_cache,
               (const void *)(uintptr_t)(RP2040_FLASH_XIP_BASE + sector_offset),
               sizeof(s_sector_cache));
        memcpy(&s_sector_cache[in_sector], &buf[written], chunk);

        irqState = save_and_disable_interrupts();
        flash_range_erase(sector_offset, RP2040_FLASH_ERASE_SIZE);
        rp2040_flash_program_sector(sector_offset);
        restore_interrupts(irqState);

        written += chunk;
        u32Offset += (uint32_t)chunk;
    }

    return (int)size;
}

const struct fal_flash_dev rp2040_onchip_flash = {
    FAL_FLASH_DEV_NAME,
    RP2040_FLASH_XIP_BASE,
    RP2040_FLASH_TOTAL_SIZE,
    RP2040_FLASH_ERASE_SIZE,
    {
        rp2040_onchip_flash_init,
        rp2040_onchip_flash_read,
        rp2040_onchip_flash_write,
        rp2040_onchip_flash_erase,
    },
    1u,
};
