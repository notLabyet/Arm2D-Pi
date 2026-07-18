#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

#define CFG_TUSB_MCU             OPT_MCU_RP2040
#define CFG_TUSB_OS              OPT_OS_NONE
#define CFG_TUSB_DEBUG           0

#define CFG_TUSB_RHPORT0_MODE    OPT_MODE_DEVICE

#define CFG_TUD_ENABLED          1
#define CFG_TUD_MAX_SPEED        OPT_MODE_FULL_SPEED
#define CFG_TUD_ENDPOINT0_SIZE   64

#define CFG_TUD_CDC              0
#define CFG_TUD_MSC              1
#define CFG_TUD_HID              2   /* mouse HID + Codex status HID */
#define CFG_TUD_MIDI             0
#define CFG_TUD_VENDOR           0

#define CFG_TUD_HID_EP_BUFSIZE   64  /* Codex status packet is 64 bytes */
#define CFG_TUD_MSC_EP_BUFSIZE   32768

#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN       __attribute__ ((aligned(4)))

#ifdef __cplusplus
}
#endif

#endif
