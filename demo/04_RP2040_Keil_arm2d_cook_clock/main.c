#include "platform/pi_platform.h"
#include "platform/st7789_simple.h"
#include "arm_2d.h"
#include "arm_2d_disp_adapter_0.h"
#include "application/buzzer_task.h"
#include "application/power_task.h"
#include "service-cook-clock/arm_2d_scene_cook_clock.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"

#ifndef __COOK_CLOCK_ENABLE_IMU__
#   define __COOK_CLOCK_ENABLE_IMU__                0
#endif

#if __COOK_CLOCK_ENABLE_IMU__
#include "application/qmi8658c_task.h"
#endif

#ifndef __COOK_CLOCK_ENABLE_AUTOMATED_TEST__
#   define __COOK_CLOCK_ENABLE_AUTOMATED_TEST__     0
#endif

#define COOK_CLOCK_INITIAL_DURATION_IN_SECONDS       (0u * 60u)
#define COOK_CLOCK_INITIAL_COLOUR                     ((arm_2d_color_rgb565_t){.tValue = __RGB(0, 255, 0)})
#define COOK_CLOCK_BACKLIGHT_IDLE_MS                  60000u
#define COOK_CLOCK_BACKLIGHT_DIM_PERCENT               20u
#define COOK_CLOCK_WATCHDOG_TIMEOUT_MS                  3000u

static struct {
    uint32_t wLastActivityMS;
    bool bDimmed;
    bool bWakeInputPending;
} s_tCookClockBacklight;

#if !__COOK_CLOCK_ENABLE_AUTOMATED_TEST__
static const uint16_t c_hwCookClockPresetDurationsInSeconds[] = {
    5u * 60u, 10u * 60u, 15u * 60u, 30u * 60u, 45u * 60u,
    60u * 60u, (60u + 30u) * 60u, 99u * 60u + 59u,
};

static uint8_t s_chCookClockPreset;
static const arm_2d_color_rgb565_t c_tCookClockColourCycle[] = {
    {.tValue = __RGB(  0, 255,   0)},
    {.tValue = __RGB(  0, 255, 255)},
    {.tValue = __RGB(  0,   0, 255)},
    {.tValue = __RGB(127,   0, 255)},
    {.tValue = __RGB(255,   0,   0)},
    {.tValue = __RGB(255, 127,   0)},
    {.tValue = __RGB(255, 255,   0)},
};
static uint8_t s_chCookClockColour;
#endif

#if __COOK_CLOCK_ENABLE_AUTOMATED_TEST__
typedef struct cook_clock_test_step_t {
    uint8_t chDurationInSeconds;
    arm_2d_color_rgb565_t tColour;
} cook_clock_test_step_t;

static const cook_clock_test_step_t c_tCookClockTestSteps[] = {
    {10, {.tValue = __RGB(255,   0,   0)}},
    {20, {.tValue = __RGB(255, 127,   0)}},
    {30, {.tValue = __RGB(255, 255,   0)}},
    {40, {.tValue = __RGB(  0, 255,   0)}},
    {50, {.tValue = __RGB(  0, 255, 255)}},
    {60, {.tValue = __RGB(  0,   0, 255)}},
    {70, {.tValue = __RGB(127,   0, 255)}},
};

typedef struct cook_clock_test_t {
    uint64_t qwStepStartTime;
    uint8_t chStep;
    bool bWaitingForAlert;
} cook_clock_test_t;

static cook_clock_test_t s_tCookClockTest;

static void __cook_clock_test_apply_step(cook_clock_test_t *ptThis)
{
    const cook_clock_test_step_t *ptStep =
        &c_tCookClockTestSteps[ptThis->chStep];

    cook_clock_set_colour(ptStep->tColour);
    cook_clock_set_countdown(ptStep->chDurationInSeconds);
    ptThis->qwStepStartTime = time_us_64();
    ptThis->bWaitingForAlert = false;
}

static void __cook_clock_test_init(void)
{
    s_tCookClockTest.chStep = 0;
    __cook_clock_test_apply_step(&s_tCookClockTest);
}

static void __cook_clock_test_task(void)
{
    cook_clock_test_t *ptThis = &s_tCookClockTest;
    const cook_clock_test_step_t *ptStep =
        &c_tCookClockTestSteps[ptThis->chStep];

    if (!ptThis->bWaitingForAlert &&
        ((time_us_64() - ptThis->qwStepStartTime)
        >= (uint64_t)ptStep->chDurationInSeconds * 1000000ULL)) {
        ptThis->bWaitingForAlert = true;
    }

    if (ptThis->bWaitingForAlert && !app_buzzer_is_playing()) {
        ptThis->chStep = (ptThis->chStep + 1u) % dimof(c_tCookClockTestSteps);
        __cook_clock_test_apply_step(ptThis);
    }
}
#endif

#if !__COOK_CLOCK_ENABLE_AUTOMATED_TEST__
static void __cook_clock_button_task(void)
{
    if (s_tCookClockBacklight.bWakeInputPending) {
        bool const bLeftShortPress =
            app_power_key_was_event(APP_POWER_KEY_EVENT_LEFT_SHORT_PRESS);
        bool const bRightShortPress =
            app_power_key_was_event(APP_POWER_KEY_EVENT_RIGHT_SHORT_PRESS);
        bool const bRightLongPress =
            app_power_key_was_event(APP_POWER_KEY_EVENT_RIGHT_LONG_PRESS);
        bool const bBothPressed =
            app_power_key_was_event(APP_POWER_KEY_EVENT_BOTH_PRESSED);

        if (bLeftShortPress || bRightShortPress ||
            bRightLongPress || bBothPressed) {
            s_tCookClockBacklight.bWakeInputPending = false;
        }
        return;
    }

    if (app_power_key_was_event(APP_POWER_KEY_EVENT_LEFT_SHORT_PRESS)) {
        cook_clock_set_countdown(
            c_hwCookClockPresetDurationsInSeconds[s_chCookClockPreset]);
        s_chCookClockPreset = (s_chCookClockPreset + 1u)
                            % dimof(c_hwCookClockPresetDurationsInSeconds);
    }

    if (app_power_key_was_event(APP_POWER_KEY_EVENT_RIGHT_SHORT_PRESS)) {
        cook_clock_toggle_pause();
    }

    if (app_power_key_was_event(APP_POWER_KEY_EVENT_RIGHT_LONG_PRESS)) {
        cook_clock_finish_countdown();
    }

    if (app_power_key_was_event(APP_POWER_KEY_EVENT_BOTH_PRESSED)) {
        cook_clock_set_colour(c_tCookClockColourCycle[s_chCookClockColour]);
        s_chCookClockColour = (s_chCookClockColour + 1u)
                            % dimof(c_tCookClockColourCycle);
    }
}
#endif

static void __cook_clock_backlight_init(uint32_t wNowMS)
{
    s_tCookClockBacklight.wLastActivityMS = wNowMS;
    s_tCookClockBacklight.bDimmed = false;
    s_tCookClockBacklight.bWakeInputPending = false;
    st7789_set_backlight_brightness(100u);
}

static void __cook_clock_backlight_task(uint32_t wNowMS)
{
    if (app_power_key_was_active()) {
        s_tCookClockBacklight.wLastActivityMS = wNowMS;
        if (s_tCookClockBacklight.bDimmed) {
            st7789_set_backlight_brightness(100u);
            s_tCookClockBacklight.bDimmed = false;
            s_tCookClockBacklight.bWakeInputPending = true;
        }
    } else if (!s_tCookClockBacklight.bDimmed &&
               ((uint32_t)(wNowMS - s_tCookClockBacklight.wLastActivityMS)
                >= COOK_CLOCK_BACKLIGHT_IDLE_MS)) {
        st7789_set_backlight_brightness(COOK_CLOCK_BACKLIGHT_DIM_PERCENT);
        s_tCookClockBacklight.bDimmed = true;
    }
}

static void __on_cook_clock_countdown_finished(
                                cook_clock_countdown_finished_effect_t tEffect,
                                void *pTarget)
{
    (void)pTarget;

#if !__COOK_CLOCK_ENABLE_AUTOMATED_TEST__
    s_chCookClockPreset = 0u;
#endif

    s_tCookClockBacklight.wLastActivityMS =
        to_ms_since_boot(get_absolute_time());
    s_tCookClockBacklight.bDimmed = false;
    s_tCookClockBacklight.bWakeInputPending = false;
    st7789_set_backlight_brightness(100u);

    if (COOK_CLOCK_COUNTDOWN_FINISHED_EFFECT_BIRTHDAY == tEffect) {
        (void)app_buzzer_play_happy_birthday();
    } else {
        (void)app_buzzer_play_countdown_finished_chime();
    }
}

static void __cook_clock_display_init(void)
{
    arm_irq_safe {
        arm_2d_init();
    }

    disp_adapter0_init();
    arm_2d_scene_cook_clock_init(&DISP0_ADAPTER);
    // COOK_CLOCK_COUNTDOWN_FINISHED_EFFECT_BIRTHDAY
    // COOK_CLOCK_COUNTDOWN_FINISHED_EFFECT_DEFAULT
    cook_clock_set_countdown_finished_effect(
                                COOK_CLOCK_COUNTDOWN_FINISHED_EFFECT_DEFAULT);
    cook_clock_set_countdown_finished_handler(
                                __on_cook_clock_countdown_finished, NULL);
    cook_clock_set_colour(COOK_CLOCK_INITIAL_COLOUR);
    // cook_clock_set_countdown(COOK_CLOCK_INITIAL_DURATION_IN_SECONDS);

#if __COOK_CLOCK_ENABLE_AUTOMATED_TEST__
    __cook_clock_test_init();
#endif
}

int main(void)
{
    platform_init();
    st7789_set_backlight(false);
    buzzer_task_init();
    power_task_init();
#if __COOK_CLOCK_ENABLE_IMU__
    (void)qmi8658c_init();
#endif
    __cook_clock_display_init();

    while (arm_fsm_rt_cpl != disp_adapter0_task()) {
    }

    __cook_clock_backlight_init(to_ms_since_boot(get_absolute_time()));
    watchdog_enable(COOK_CLOCK_WATCHDOG_TIMEOUT_MS, true);

    while (true) {
        uint32_t wNowMS = to_ms_since_boot(get_absolute_time());

        buzzer_task(wNowMS);
        power_task(wNowMS);
        __cook_clock_backlight_task(wNowMS);
    #if __COOK_CLOCK_ENABLE_IMU__
        qmi8658c_task(wNowMS);
    #endif
        cook_clock_task();
#if __COOK_CLOCK_ENABLE_AUTOMATED_TEST__
        __cook_clock_test_task();
    #else
        __cook_clock_button_task();
#endif
        disp_adapter0_task();
        watchdog_update();
    }
}

