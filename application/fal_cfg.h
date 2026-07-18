#ifndef FAL_CFG_H
#define FAL_CFG_H

#include "fal.h"
#include "rp2040_flash_layout.h"

#define FAL_FLASH_DEV_NAME             "rp2040_onchip"

extern const struct fal_flash_dev rp2040_onchip_flash;

#define FAL_FLASH_DEV_TABLE                                                \
{                                                                          \
    &rp2040_onchip_flash,                                                  \
}

#define FAL_PART_TABLE                                                     \
{                                                                          \
    {                                                                      \
        FAL_PART_MAGIC_WORD,                                               \
        "app",                                                             \
        FAL_FLASH_DEV_NAME,                                                \
        RP2040_FLASH_CODE_OFFSET,                                          \
        RP2040_FLASH_CODE_SIZE,                                            \
        FAL_PART_FLAG_READ_ONLY,                                           \
    },                                                                     \
    {                                                                      \
        FAL_PART_MAGIC_WORD,                                               \
        "pic",                                                             \
        FAL_FLASH_DEV_NAME,                                                \
        RP2040_FLASH_PIC_OFFSET,                                           \
        RP2040_FLASH_PIC_SIZE,                                             \
        0,                                                                 \
    },                                                                     \
}

#endif
