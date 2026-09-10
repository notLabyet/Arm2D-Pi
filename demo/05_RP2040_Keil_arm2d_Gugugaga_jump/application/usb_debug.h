#ifndef __USB_DEBUG_H__
#define __USB_DEBUG_H__

#include <stdint.h>
#include <stdio.h>

#ifndef USB_DEBUG_PRINTF_ENABLED
#define USB_DEBUG_PRINTF_ENABLED 0
#endif

#if USB_DEBUG_PRINTF_ENABLED
#define USB_DEBUG_PRINTF(...) printf(__VA_ARGS__)
#else
#define USB_DEBUG_PRINTF(...) ((void)0)
#endif

typedef struct usb_debug_state_t {
    volatile uint32_t irq_count;
    volatile uint32_t last_ints;
    volatile uint32_t last_sie_status;
    volatile uint32_t last_buf_status;
    volatile uint32_t last_main_ctrl;
    volatile uint32_t last_sie_ctrl;
    volatile uint32_t last_muxing;
    volatile uint32_t last_ep0_in_buf_ctrl;
    volatile uint32_t last_ep0_out_buf_ctrl;
    volatile uint32_t last_ep1_in_ctrl;
    volatile uint32_t last_ep1_out_ctrl;
    volatile uint32_t last_ep1_in_buf_ctrl;
    volatile uint32_t last_ep1_out_buf_ctrl;
    volatile uint32_t setup_count;
    volatile uint32_t bus_reset_count;
    volatile uint32_t stray_buffer_count;
    volatile uint32_t xfer_count;
    volatile uint32_t last_xfer_ep_addr;
    volatile uint32_t last_xfer_len;
    volatile uint32_t last_xfer_buf_ctrl;
    volatile uint32_t xfer_complete_count;
    volatile uint32_t last_xfer_complete_ep_addr;
    volatile uint32_t last_xfer_complete_len;
    volatile uint32_t set_address_count;
    volatile uint32_t last_dev_addr;
    volatile uint32_t descriptor_device_count;
    volatile uint32_t descriptor_config_count;
    volatile uint32_t descriptor_string_count;
    volatile uint32_t descriptor_hid_report_count;
    volatile uint32_t mount_count;
    volatile uint32_t unmount_count;
    volatile uint32_t suspend_count;
    volatile uint32_t resume_count;
    volatile uint8_t setup_packet[8];
} usb_debug_state_t;

extern usb_debug_state_t g_tUSBDebugState;

#endif
