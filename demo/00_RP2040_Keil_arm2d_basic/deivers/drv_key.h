#ifndef __DRV_KEY_H__
#define __DRV_KEY_H__

#include <stdbool.h>
#include <stdint.h>

#ifndef DRV_KEY_PIN
#   define DRV_KEY_PIN                 9u
#endif

#ifndef DRV_KEY_POWER_KEEP_PIN
#   define DRV_KEY_POWER_KEEP_PIN       2u
#endif

#ifndef DRV_KEY_RIGHT_PIN
#   define DRV_KEY_RIGHT_PIN            24u
#endif

#ifndef DRV_KEY_DEBOUNCE_MS
#   define DRV_KEY_DEBOUNCE_MS         10u
#endif

#ifndef DRV_KEY_LONG_PRESS_MS
#   define DRV_KEY_LONG_PRESS_MS       1500u
#endif

#ifndef DRV_KEY_RIGHT_LONG_PRESS_MS
#   define DRV_KEY_RIGHT_LONG_PRESS_MS 1000u
#endif

void drv_key_init(void);
void drv_key_task(void);
void drv_key_set_power_keep(bool bKeepPower);

uint8_t drv_key_get_state(void);
bool drv_key_was_pressed(void);
bool drv_key_was_released(void);
bool drv_key_was_long_pressed(void);
bool drv_key_right_was_released(void);
bool drv_key_right_was_long_pressed(void);

#endif // __DRV_KEY_H__
