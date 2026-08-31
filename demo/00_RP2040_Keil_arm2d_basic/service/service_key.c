#include "service_key.h"

#include "drv_key.h"
#include "pico/stdlib.h"

#define SERVICE_KEY_POWER_OFF_CHIME_MS     350u

/* service 层保存当前按下状态和待消费的手势事件。 */
service_key_t g_service_key;

void service_key_init(void)
{
    drv_key_init();
    g_service_key.bPressed = false;
    g_service_key.bShortPressPending = false;
    g_service_key.bLongPressPending = false;
    g_service_key.bLongPressTriggered = false;
    g_service_key.bPowerOffPending = false;
    g_service_key.wPowerOffAtMS = 0u;
    g_service_key.bRightShortPressPending = false;
    g_service_key.bRightLongPressPending = false;
    g_service_key.bRightLongPressTriggered = false;
}

void service_key_task(void)
{
    uint32_t wNowMS = to_ms_since_boot(get_absolute_time());

    drv_key_task();
    g_service_key.bPressed = drv_key_get_state() != 0u;

    if (g_service_key.bPowerOffPending
    && ((int32_t)(wNowMS - g_service_key.wPowerOffAtMS) >= 0)) {
        /* 与 4 号工程一致：关机旋律播放约 350ms 后撤销 GPIO2 保持。 */
        drv_key_set_power_keep(false);
        g_service_key.bPowerOffPending = false;
    }

    if (drv_key_was_long_pressed()) {
        /* 长按发生后，直到释放前不应再生成短按事件。 */
        g_service_key.bLongPressPending = true;
        g_service_key.bLongPressTriggered = true;
    }
    if (drv_key_was_released()) {
        if (!g_service_key.bLongPressTriggered) {
            g_service_key.bShortPressPending = true;
        }
        g_service_key.bLongPressTriggered = false;
    }

    if (drv_key_right_was_long_pressed()) {
        g_service_key.bRightLongPressPending = true;
        g_service_key.bRightLongPressTriggered = true;
    }
    if (drv_key_right_was_released()) {
        if (!g_service_key.bRightLongPressTriggered) {
            g_service_key.bRightShortPressPending = true;
        }
        g_service_key.bRightLongPressTriggered = false;
    }
}

bool service_key_was_short_pressed(void)
{
    /* 读取即清除，避免同一个事件被重复处理。 */
    bool bPending = g_service_key.bShortPressPending;
    g_service_key.bShortPressPending = false;
    return bPending;
}

bool service_key_was_long_pressed(void)
{
    bool bPending = g_service_key.bLongPressPending;
    g_service_key.bLongPressPending = false;
    return bPending;
}

bool service_key_was_right_short_pressed(void)
{
    bool bPending = g_service_key.bRightShortPressPending;
    g_service_key.bRightShortPressPending = false;
    return bPending;
}

bool service_key_was_right_long_pressed(void)
{
    bool bPending = g_service_key.bRightLongPressPending;
    g_service_key.bRightLongPressPending = false;
    return bPending;
}

void service_key_request_power_off(void)
{
    /* 让关机旋律先完整播放，再关闭电源保持输出。 */
    g_service_key.wPowerOffAtMS = to_ms_since_boot(get_absolute_time())
                                + SERVICE_KEY_POWER_OFF_CHIME_MS;
    g_service_key.bPowerOffPending = true;
}