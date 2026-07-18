#ifndef USB_MSC_SD_H
#define USB_MSC_SD_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool usb_msc_sd_init(void);
bool usb_msc_sd_is_ready(void);
uint32_t usb_msc_sd_block_count(void);

#ifdef __cplusplus
}
#endif

#endif
