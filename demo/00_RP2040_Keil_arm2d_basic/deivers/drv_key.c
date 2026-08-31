#include "drv_key.h"

#include "hardware/gpio.h"
#include "pico/stdlib.h"

static bool s_bPressed;
static bool s_bCandidatePressed;
static bool s_bPressLatched;
static bool s_bReleaseLatched;
static bool s_bLongPressLatched;
static bool s_bLongPressTriggered;
static uint32_t s_wCandidateSinceMS;
static uint32_t s_wPressedSinceMS;

static struct {
    bool bPressed;
    bool bCandidatePressed;
    bool bReleaseLatched;
    bool bLongPressLatched;
    bool bLongPressTriggered;
    uint32_t wCandidateSinceMS;
    uint32_t wPressedSinceMS;
} s_tRightKey;

static bool __drv_key_read_pressed(uint8_t chPin)
{
    return gpio_get(chPin) == 0u;
}

void drv_key_init(void)
{
    uint32_t wNowMS = to_ms_since_boot(get_absolute_time());

    /* GPIO2 拉高后保持板卡供电，GPIO9 用于检测低有效按键。 */
    gpio_init(DRV_KEY_POWER_KEEP_PIN);
    gpio_set_dir(DRV_KEY_POWER_KEEP_PIN, GPIO_OUT);
    drv_key_set_power_keep(true);

    gpio_init(DRV_KEY_PIN);
    gpio_set_dir(DRV_KEY_PIN, GPIO_IN);
    gpio_pull_up(DRV_KEY_PIN);

    s_bPressed = __drv_key_read_pressed(DRV_KEY_PIN);
    s_bCandidatePressed = s_bPressed;
    s_bPressLatched = false;
    s_bReleaseLatched = false;
    s_bLongPressLatched = false;
    s_bLongPressTriggered = false;
    s_wCandidateSinceMS = wNowMS;
    s_wPressedSinceMS = wNowMS;

    /* GPIO24 是 4 号工程中用于短按和长按操作的右按键。 */
    gpio_init(DRV_KEY_RIGHT_PIN);
    gpio_set_dir(DRV_KEY_RIGHT_PIN, GPIO_IN);
    gpio_pull_up(DRV_KEY_RIGHT_PIN);
    s_tRightKey.bPressed = __drv_key_read_pressed(DRV_KEY_RIGHT_PIN);
    s_tRightKey.bCandidatePressed = s_tRightKey.bPressed;
    s_tRightKey.bReleaseLatched = false;
    s_tRightKey.bLongPressLatched = false;
    s_tRightKey.bLongPressTriggered = false;
    s_tRightKey.wCandidateSinceMS = wNowMS;
    s_tRightKey.wPressedSinceMS = wNowMS;
}

void drv_key_set_power_keep(bool bKeepPower)
{
    gpio_put(DRV_KEY_POWER_KEEP_PIN, bKeepPower ? 1 : 0);
}

void drv_key_task(void)
{
    uint32_t wNowMS = to_ms_since_boot(get_absolute_time());
    bool bPressed = __drv_key_read_pressed(DRV_KEY_PIN);
    bool bRightPressed = __drv_key_read_pressed(DRV_KEY_RIGHT_PIN);

    /* 原始电平变化后先进入候选态，持续稳定一个消抖周期才接受。 */
    if (bPressed != s_bCandidatePressed) {
        s_bCandidatePressed = bPressed;
        s_wCandidateSinceMS = wNowMS;
        return;
    }

    if ((bPressed != s_bPressed)
    && ((uint32_t)(wNowMS - s_wCandidateSinceMS) >= DRV_KEY_DEBOUNCE_MS)) {
        s_bPressed = bPressed;
        if (bPressed) {
            /* 锁存按下沿，供 service 层在本轮后续任务中消费。 */
            s_bPressLatched = true;
            s_bLongPressTriggered = false;
            s_wPressedSinceMS = wNowMS;
        } else {
            /* 锁存释放沿，短按事件由 service 层在释放时生成。 */
            s_bReleaseLatched = true;
        }
    }

    if (s_bPressed && !s_bLongPressTriggered
    && ((uint32_t)(wNowMS - s_wPressedSinceMS) >= DRV_KEY_LONG_PRESS_MS)) {
        /* 同一次按压只触发一次长按事件。 */
        s_bLongPressLatched = true;
        s_bLongPressTriggered = true;
    }

    if (bRightPressed != s_tRightKey.bCandidatePressed) {
        s_tRightKey.bCandidatePressed = bRightPressed;
        s_tRightKey.wCandidateSinceMS = wNowMS;
        return;
    }

    if ((bRightPressed != s_tRightKey.bPressed)
    && ((uint32_t)(wNowMS - s_tRightKey.wCandidateSinceMS) >= DRV_KEY_DEBOUNCE_MS)) {
        s_tRightKey.bPressed = bRightPressed;
        if (bRightPressed) {
            s_tRightKey.bLongPressTriggered = false;
            s_tRightKey.wPressedSinceMS = wNowMS;
        } else {
            s_tRightKey.bReleaseLatched = true;
        }
    }

    if (s_tRightKey.bPressed && !s_tRightKey.bLongPressTriggered
    && ((uint32_t)(wNowMS - s_tRightKey.wPressedSinceMS)
        >= DRV_KEY_RIGHT_LONG_PRESS_MS)) {
        s_tRightKey.bLongPressLatched = true;
        s_tRightKey.bLongPressTriggered = true;
    }
}

uint8_t drv_key_get_state(void)
{
    return s_bPressed ? 1u : 0u;
}

bool drv_key_was_pressed(void)
{
    bool bPressed = s_bPressLatched;
    s_bPressLatched = false;
    return bPressed;
}

bool drv_key_was_released(void)
{
    bool bReleased = s_bReleaseLatched;
    s_bReleaseLatched = false;
    return bReleased;
}

bool drv_key_was_long_pressed(void)
{
    bool bLongPressed = s_bLongPressLatched;
    s_bLongPressLatched = false;
    return bLongPressed;
}

bool drv_key_right_was_released(void)
{
    bool bReleased = s_tRightKey.bReleaseLatched;
    s_tRightKey.bReleaseLatched = false;
    return bReleased;
}

bool drv_key_right_was_long_pressed(void)
{
    bool bLongPressed = s_tRightKey.bLongPressLatched;
    s_tRightKey.bLongPressLatched = false;
    return bLongPressed;
}
