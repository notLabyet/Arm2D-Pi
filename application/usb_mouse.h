#ifndef __USB_MOUSE_H__
#define __USB_MOUSE_H__

#include <stdint.h>

void usb_mouse_init(void);
void usb_mouse_task(void);
void usb_mouse_set_motion(int8_t x, int8_t y, int8_t wheel);
void usb_mouse_update_imu_raw(int16_t const raw_data[6]);

#endif
