#include "power_task.h"

#include <stddef.h>

#include "bsp_cfg.h"
#include "buzzer_task.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"

#define POWER_KEY_POWER_OFF_CHIME_MS                  350u
#define APP_POWER_KEY_EVENT_NONE                       (-1)

typedef enum app_power_key_state_t {
    APP_POWER_KEY_STATE_SOURCE_CHECK = 0,
    APP_POWER_KEY_STATE_WAIT_PRESS,
    APP_POWER_KEY_STATE_WAIT_RELEASE,
    APP_POWER_KEY_STATE_WAIT_LONG_PRESS,
    APP_POWER_KEY_STATE_WAIT_SHORT_RELEASE,
    APP_POWER_KEY_STATE_WAIT_POWER_OFF,
    APP_POWER_KEY_STATE_POWER_OFF,
} app_power_key_state_t;

static struct {
    app_power_key_state_t tState;
    uint32_t wStampMS;
    bool bKeyPressed;
} s_tPowerKey;

typedef enum app_button_gesture_state_t {
    APP_BUTTON_GESTURE_IDLE = 0,
    APP_BUTTON_GESTURE_FIRST_PRESS,
    APP_BUTTON_GESTURE_SUPPRESSED,
} app_button_gesture_state_t;

typedef struct app_button_gesture_t {
    uint32_t wStampMS;
    app_button_gesture_state_t tState;
} app_button_gesture_t;

static app_button_t s_tLeftButton;
static app_button_t s_tRightButton;
static app_button_gesture_t s_tButtonGestures[2];
static bool s_bBothButtonsPressed;
static bool s_bPowerKeyActive;
static uint8_t s_chPowerKeyEvents;

static bool app_power_key_is_pressed(void)
{
    return (gpio_get(POWER_UP_CHECK_PIN) == 0u);
}

static void __power_key_emit_event(app_power_key_event_t tEvent)
{
    s_chPowerKeyEvents |= (uint8_t)(1u << (uint8_t)tEvent);

    switch (tEvent) {
    case APP_POWER_KEY_EVENT_LEFT_SHORT_PRESS:
    case APP_POWER_KEY_EVENT_RIGHT_SHORT_PRESS:
        (void)app_buzzer_play_power_key_feedback(false);
        break;
    case APP_POWER_KEY_EVENT_RIGHT_LONG_PRESS:
        (void)app_buzzer_play_power_key_feedback(true);
        break;
    case APP_POWER_KEY_EVENT_BOTH_PRESSED:
        (void)app_buzzer_play_power_keys_together_feedback();
        break;
    default:
        break;
    }
}

static void __power_key_gesture_task(app_button_t *ptButton,
                                     app_button_gesture_t *ptGesture,
                                     app_power_key_event_t tShortEvent,
                                     int8_t chLongEvent,
                                     uint32_t wNowMS)
{
    bool const bPressed = app_button_was_pressed(ptButton);
    bool const bReleased = app_button_was_released(ptButton);

    if (bPressed) {
        s_bPowerKeyActive = true;
    }

    if (APP_BUTTON_GESTURE_SUPPRESSED == ptGesture->tState) {
        if (bReleased) {
            ptGesture->tState = APP_BUTTON_GESTURE_IDLE;
        }
        return;
    }

    switch (ptGesture->tState) {
    case APP_BUTTON_GESTURE_IDLE:
        if (bPressed) {
            ptGesture->wStampMS = wNowMS;
            ptGesture->tState = APP_BUTTON_GESTURE_FIRST_PRESS;
        }
        break;

    case APP_BUTTON_GESTURE_FIRST_PRESS:
        if (app_button_is_pressed(ptButton) &&
            ((uint32_t)(wNowMS - ptGesture->wStampMS) >= BUTTON_LONG_PRESS_MS)) {
            if (APP_POWER_KEY_EVENT_NONE != chLongEvent) {
                __power_key_emit_event((app_power_key_event_t)chLongEvent);
            }
            ptGesture->tState = APP_BUTTON_GESTURE_SUPPRESSED;
        } else if (bReleased) {
            __power_key_emit_event(tShortEvent);
            ptGesture->tState = APP_BUTTON_GESTURE_IDLE;
        }
        break;

    default:
        ptGesture->tState = APP_BUTTON_GESTURE_IDLE;
        break;
    }
}

void power_task_init(void)
{
    app_power_key_init();
}

void power_task(uint32_t wNowMS)
{
    app_button_poll(&s_tLeftButton, wNowMS);
    app_button_poll(&s_tRightButton, wNowMS);

    bool const bBothButtonsPressed = app_button_is_pressed(&s_tLeftButton)
                                   && app_button_is_pressed(&s_tRightButton);
    if (bBothButtonsPressed && !s_bBothButtonsPressed) {
        s_tButtonGestures[0].tState = APP_BUTTON_GESTURE_SUPPRESSED;
        s_tButtonGestures[1].tState = APP_BUTTON_GESTURE_SUPPRESSED;
        __power_key_emit_event(APP_POWER_KEY_EVENT_BOTH_PRESSED);
    }
    s_bBothButtonsPressed = bBothButtonsPressed;

    __power_key_gesture_task(&s_tLeftButton,
                             &s_tButtonGestures[0],
                             APP_POWER_KEY_EVENT_LEFT_SHORT_PRESS,
                             APP_POWER_KEY_EVENT_NONE,
                             wNowMS);
    __power_key_gesture_task(&s_tRightButton,
                             &s_tButtonGestures[1],
                             APP_POWER_KEY_EVENT_RIGHT_SHORT_PRESS,
                             APP_POWER_KEY_EVENT_RIGHT_LONG_PRESS,
                             wNowMS);

    app_power_key_task(wNowMS);
}

void app_power_key_init(void)
{
    gpio_init(POWER_KEEP_PIN);
    gpio_set_function(POWER_KEEP_PIN, GPIO_FUNC_SIO);
    gpio_set_dir(POWER_KEEP_PIN, GPIO_OUT);
    gpio_put(POWER_KEEP_PIN, 1);
    (void)app_buzzer_play_power_on_chime();

    gpio_init(POWER_UP_CHECK_PIN);
    gpio_set_function(POWER_UP_CHECK_PIN, GPIO_FUNC_SIO);
    gpio_set_dir(POWER_UP_CHECK_PIN, GPIO_IN);
    gpio_pull_up(POWER_UP_CHECK_PIN);

    app_button_init(&s_tLeftButton,
                    POWER_UP_CHECK_PIN,
                    true,
                    BUTTON_DEBOUNCE_MS);
    app_button_init(&s_tRightButton,
                    RIGHT_BUTTON_PIN,
                    true,
                    BUTTON_DEBOUNCE_MS);

    s_tPowerKey.tState = APP_POWER_KEY_STATE_SOURCE_CHECK;
    s_tPowerKey.wStampMS = to_ms_since_boot(get_absolute_time());
    s_tPowerKey.bKeyPressed = false;
    s_tButtonGestures[0].tState = app_button_is_pressed(&s_tLeftButton)
                                ? APP_BUTTON_GESTURE_SUPPRESSED
                                : APP_BUTTON_GESTURE_IDLE;
    s_tButtonGestures[1].tState = app_button_is_pressed(&s_tRightButton)
                                ? APP_BUTTON_GESTURE_SUPPRESSED
                                : APP_BUTTON_GESTURE_IDLE;
    s_bBothButtonsPressed = false;
    s_bPowerKeyActive = false;
    s_chPowerKeyEvents = 0u;
}

void app_power_key_task(uint32_t wNowMS)
{
    switch (s_tPowerKey.tState) {
    case APP_POWER_KEY_STATE_SOURCE_CHECK:
        if (app_power_key_is_pressed()) {
            s_tPowerKey.wStampMS = wNowMS;
            s_tPowerKey.tState = APP_POWER_KEY_STATE_WAIT_RELEASE;
        } else {
            gpio_put(POWER_KEEP_PIN, 0);
            s_tPowerKey.wStampMS = wNowMS;
            s_tPowerKey.tState = APP_POWER_KEY_STATE_WAIT_PRESS;
        }
        break;

    case APP_POWER_KEY_STATE_WAIT_PRESS:
        if (app_power_key_is_pressed()) {
            if ((uint32_t)(wNowMS - s_tPowerKey.wStampMS) >= POWER_KEY_DEBOUNCE_MS) {
                gpio_put(POWER_KEEP_PIN, 1);
                s_tPowerKey.wStampMS = wNowMS;
                s_tPowerKey.tState = APP_POWER_KEY_STATE_WAIT_RELEASE;
            }
        } else {
            s_tPowerKey.wStampMS = wNowMS;
        }
        break;

    case APP_POWER_KEY_STATE_WAIT_RELEASE:
        if (!app_power_key_is_pressed()) {
            if ((uint32_t)(wNowMS - s_tPowerKey.wStampMS) >= POWER_KEY_DEBOUNCE_MS) {
                s_tPowerKey.wStampMS = wNowMS;
                s_tPowerKey.tState = APP_POWER_KEY_STATE_WAIT_LONG_PRESS;
            }
        } else {
            s_tPowerKey.wStampMS = wNowMS;
        }
        break;

    case APP_POWER_KEY_STATE_WAIT_LONG_PRESS:
        if (app_power_key_is_pressed()) {
            s_tPowerKey.bKeyPressed = true;
            if (!s_bBothButtonsPressed &&
                ((uint32_t)(wNowMS - s_tPowerKey.wStampMS) >= POWER_KEY_LONG_PRESS_MS)) {
                (void)app_buzzer_play_power_off_chime();
                s_tPowerKey.wStampMS = wNowMS;
                s_tPowerKey.tState = APP_POWER_KEY_STATE_WAIT_POWER_OFF;
            }
        } else {
            if (s_tPowerKey.bKeyPressed) {
                s_tPowerKey.wStampMS = wNowMS;
                s_tPowerKey.tState = APP_POWER_KEY_STATE_WAIT_SHORT_RELEASE;
                break;
            }
            s_tPowerKey.wStampMS = wNowMS;
        }
        break;

    case APP_POWER_KEY_STATE_WAIT_SHORT_RELEASE:
        if (!app_power_key_is_pressed()) {
            if ((uint32_t)(wNowMS - s_tPowerKey.wStampMS) >= POWER_KEY_DEBOUNCE_MS) {
                s_tPowerKey.bKeyPressed = false;
                s_tPowerKey.wStampMS = wNowMS;
                s_tPowerKey.tState = APP_POWER_KEY_STATE_WAIT_LONG_PRESS;
            }
        } else {
            s_tPowerKey.wStampMS = wNowMS;
            s_tPowerKey.tState = APP_POWER_KEY_STATE_WAIT_LONG_PRESS;
        }
        break;

    case APP_POWER_KEY_STATE_WAIT_POWER_OFF:
        if ((uint32_t)(wNowMS - s_tPowerKey.wStampMS) >= POWER_KEY_POWER_OFF_CHIME_MS) {
            s_tPowerKey.tState = APP_POWER_KEY_STATE_POWER_OFF;
        }
        break;

    case APP_POWER_KEY_STATE_POWER_OFF:
        gpio_put(POWER_KEEP_PIN, 0);
        break;

    default:
        s_tPowerKey.tState = APP_POWER_KEY_STATE_SOURCE_CHECK;
        s_tPowerKey.wStampMS = wNowMS;
        break;
    }
}

void app_button_init(app_button_t *ptButton,
                     uint8_t chPin,
                     bool bActiveLow,
                     uint32_t wDebounceMS)
{
    bool bPressed;

    if (NULL == ptButton) {
        return;
    }

    gpio_init(chPin);
    gpio_set_dir(chPin, GPIO_IN);
    if (bActiveLow) {
        gpio_pull_up(chPin);
    } else {
        gpio_pull_down(chPin);
    }

    bPressed = (gpio_get(chPin) != 0u);
    if (bActiveLow) {
        bPressed = !bPressed;
    }

    ptButton->chPin = chPin;
    ptButton->bActiveLow = bActiveLow;
    ptButton->bPressed = bPressed;
    ptButton->bCandidatePressed = bPressed;
    ptButton->bPressLatched = false;
    ptButton->bReleaseLatched = false;
    ptButton->bLongPressLatched = false;
    ptButton->bLongPressTriggered = false;
    ptButton->wCandidateSinceMS = to_ms_since_boot(get_absolute_time());
    ptButton->wPressedSinceMS = ptButton->wCandidateSinceMS;
    ptButton->wDebounceMS = wDebounceMS;
}

void app_button_poll(app_button_t *ptButton, uint32_t wNowMS)
{
    bool bPressed;

    if (NULL == ptButton) {
        return;
    }

    bPressed = (gpio_get(ptButton->chPin) != 0u);
    if (ptButton->bActiveLow) {
        bPressed = !bPressed;
    }

    if (bPressed != ptButton->bCandidatePressed) {
        ptButton->bCandidatePressed = bPressed;
        ptButton->wCandidateSinceMS = wNowMS;
        return;
    }

    if ((bPressed != ptButton->bPressed) &&
        ((uint32_t)(wNowMS - ptButton->wCandidateSinceMS) >= ptButton->wDebounceMS)) {
        ptButton->bPressed = bPressed;
        if (bPressed) {
            ptButton->bPressLatched = true;
            ptButton->bLongPressTriggered = false;
            ptButton->wPressedSinceMS = wNowMS;
        } else {
            ptButton->bReleaseLatched = true;
            ptButton->bLongPressTriggered = false;
        }
    }

    if (ptButton->bPressed && !ptButton->bLongPressTriggered &&
        ((uint32_t)(wNowMS - ptButton->wPressedSinceMS) >= BUTTON_LONG_PRESS_MS)) {
        ptButton->bLongPressLatched = true;
        ptButton->bLongPressTriggered = true;
    }
}

bool app_button_is_pressed(const app_button_t *ptButton)
{
    return (NULL != ptButton) && ptButton->bPressed;
}

bool app_button_was_pressed(app_button_t *ptButton)
{
    bool bPressed;

    if (NULL == ptButton) {
        return false;
    }

    bPressed = ptButton->bPressLatched;
    ptButton->bPressLatched = false;
    return bPressed;
}

bool app_button_was_released(app_button_t *ptButton)
{
    bool bReleased;

    if (NULL == ptButton) {
        return false;
    }

    bReleased = ptButton->bReleaseLatched;
    ptButton->bReleaseLatched = false;
    return bReleased;
}

bool app_button_was_long_pressed(app_button_t *ptButton)
{
    bool bLongPressed;

    if (NULL == ptButton) {
        return false;
    }

    bLongPressed = ptButton->bLongPressLatched;
    ptButton->bLongPressLatched = false;
    return bLongPressed;
}

bool app_power_keys_were_pressed_together(void)
{
    return app_power_key_was_event(APP_POWER_KEY_EVENT_BOTH_PRESSED);
}

bool app_power_key_was_event(app_power_key_event_t tEvent)
{
    uint8_t const chEventMask = (uint8_t)(1u << (uint8_t)tEvent);
    bool const bEventPending = (s_chPowerKeyEvents & chEventMask) != 0u;

    s_chPowerKeyEvents &= (uint8_t)~chEventMask;
    return bEventPending;
}

bool app_power_key_was_active(void)
{
    bool const bActive = s_bPowerKeyActive;

    s_bPowerKeyActive = false;
    return bActive;
}
