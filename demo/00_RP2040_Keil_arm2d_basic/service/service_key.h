#ifndef __SERVICE_KEY_H__
#define __SERVICE_KEY_H__

#include <stdbool.h>
#include <stdint.h>

typedef struct service_key_t {
    bool bPressed;
    bool bShortPressPending;
    bool bLongPressPending;
    bool bLongPressTriggered;
    bool bPowerOffPending;
    uint32_t wPowerOffAtMS;
    bool bRightShortPressPending;
    bool bRightLongPressPending;
    bool bRightLongPressTriggered;
} service_key_t;

void service_key_init(void);
void service_key_task(void);
bool service_key_was_short_pressed(void);
bool service_key_was_long_pressed(void);
bool service_key_was_right_short_pressed(void);
bool service_key_was_right_long_pressed(void);
void service_key_request_power_off(void);

extern service_key_t g_service_key;

#endif