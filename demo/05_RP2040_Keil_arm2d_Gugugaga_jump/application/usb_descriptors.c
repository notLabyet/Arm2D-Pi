#include "tusb.h"

#include <string.h>

#include "usb_debug.h"

#define USB_VID         0xCafe
#define USB_PID         0x4004
#define USB_BCD         0x0200

enum {
    ITF_NUM_HID,
    ITF_NUM_MSC,
    ITF_NUM_TOTAL
};

#define CONFIG_TOTAL_LEN    (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN + TUD_MSC_DESC_LEN)
#define EPNUM_HID           0x81
#define EPNUM_MSC_OUT       0x02
#define EPNUM_MSC_IN        0x82

static tusb_desc_device_t const s_tDeviceDescriptor = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = USB_BCD,
    .bDeviceClass       = 0x00,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor           = USB_VID,
    .idProduct          = USB_PID,
    .bcdDevice          = 0x0100,

    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,

    .bNumConfigurations = 0x01,
};

static uint8_t const s_chHIDReportDescriptor[] = {
    TUD_HID_REPORT_DESC_MOUSE()
};

static uint8_t const s_chConfigurationDescriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

    TUD_HID_DESCRIPTOR(ITF_NUM_HID, 0, HID_ITF_PROTOCOL_MOUSE,
                       sizeof(s_chHIDReportDescriptor), EPNUM_HID,
                       CFG_TUD_HID_EP_BUFSIZE, 5),

    TUD_MSC_DESCRIPTOR(ITF_NUM_MSC, 0, EPNUM_MSC_OUT, EPNUM_MSC_IN, 64),
};

static char const *s_ppchStringDescriptor[] = {
    (const char[]){0x09, 0x04},
    "RP2040",
    "RP2040 Mouse + SD MSC",
    "000001",
};

static uint16_t s_hwStringDescriptor[32];

uint8_t const *tud_descriptor_device_cb(void)
{
    g_tUSBDebugState.descriptor_device_count++;
    USB_DEBUG_PRINTF("USB DESC_DEVICE count=%lu\r\n",
                     (unsigned long)g_tUSBDebugState.descriptor_device_count);

    return (uint8_t const *)&s_tDeviceDescriptor;
}

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    (void)instance;

    g_tUSBDebugState.descriptor_hid_report_count++;
    USB_DEBUG_PRINTF("USB DESC_HID_REPORT inst=%u count=%lu len=%u\r\n",
                     (unsigned)instance,
                     (unsigned long)g_tUSBDebugState.descriptor_hid_report_count,
                     (unsigned)sizeof(s_chHIDReportDescriptor));

    return s_chHIDReportDescriptor;
}

uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    g_tUSBDebugState.descriptor_config_count++;
    USB_DEBUG_PRINTF("USB DESC_CONFIG index=%u count=%lu len=%u\r\n",
                     (unsigned)index,
                     (unsigned long)g_tUSBDebugState.descriptor_config_count,
                     (unsigned)sizeof(s_chConfigurationDescriptor));

    return s_chConfigurationDescriptor;
}

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;

    uint8_t chCount;
    g_tUSBDebugState.descriptor_string_count++;
    USB_DEBUG_PRINTF("USB DESC_STRING index=%u count=%lu\r\n",
                     (unsigned)index,
                     (unsigned long)g_tUSBDebugState.descriptor_string_count);

    if (index == 0) {
        memcpy(&s_hwStringDescriptor[1], s_ppchStringDescriptor[0], 2);
        chCount = 1;
    } else {
        if (index >= (sizeof(s_ppchStringDescriptor) / sizeof(s_ppchStringDescriptor[0]))) {
            return NULL;
        }

        char const *pchString = s_ppchStringDescriptor[index];
        chCount = (uint8_t)strlen(pchString);
        if (chCount > 31) {
            chCount = 31;
        }

        for (uint8_t i = 0; i < chCount; i++) {
            s_hwStringDescriptor[1 + i] = pchString[i];
        }
    }

    s_hwStringDescriptor[0] = (uint16_t)((TUSB_DESC_STRING << 8) |
                                        (2 * chCount + 2));

    return s_hwStringDescriptor;
}
