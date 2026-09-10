#include "thd_clock_hands.h"

#include <stddef.h>

static q31_t __thd_clock_hands_phase_to_angle(uint32_t wPhaseMs,
                                               uint32_t wPeriodMs)
{
    uint32_t wTurnsQ31 = (uint32_t)(
        ((uint64_t)wPhaseMs << 31) / wPeriodMs);

    /* Negative model-view Z rotation appears clockwise on screen. */
    return -(q31_t)wTurnsQ31;
}

void thd_clock_hands_init(thd_clock_hands_t *ptThis)
{
    if (NULL == ptThis) {
        return;
    }

    ptThis->wLastTimestampMs = 0;
    ptThis->wElapsedMs = 0;
    ptThis->q31HourAngle = 0;
    ptThis->q31MinuteAngle = 0;
    ptThis->bInitialized = 0;
}

void thd_clock_hands_update(thd_clock_hands_t *ptThis,
                            uint32_t wTimestampMs)
{
    if (NULL == ptThis) {
        return;
    }

    if (!ptThis->bInitialized) {
        ptThis->wLastTimestampMs = wTimestampMs;
        ptThis->bInitialized = 1;
        return;
    }

    uint32_t wDeltaMs = wTimestampMs - ptThis->wLastTimestampMs;
    ptThis->wLastTimestampMs = wTimestampMs;

    wDeltaMs %= THD_CLOCK_HANDS_HOUR_PERIOD_MS;
    ptThis->wElapsedMs += wDeltaMs;
    if (ptThis->wElapsedMs >= THD_CLOCK_HANDS_HOUR_PERIOD_MS) {
        ptThis->wElapsedMs -= THD_CLOCK_HANDS_HOUR_PERIOD_MS;
    }

    ptThis->q31MinuteAngle = __thd_clock_hands_phase_to_angle(
        ptThis->wElapsedMs % THD_CLOCK_HANDS_MINUTE_PERIOD_MS,
        THD_CLOCK_HANDS_MINUTE_PERIOD_MS);
    ptThis->q31HourAngle = __thd_clock_hands_phase_to_angle(
        ptThis->wElapsedMs,
        THD_CLOCK_HANDS_HOUR_PERIOD_MS);
}

q31_t thd_clock_hands_get_angle(const thd_clock_hands_t *ptThis,
                                thd_clock_hand_t tHand)
{
    if (NULL == ptThis) {
        return 0;
    }

    switch (tHand) {
        case THD_CLOCK_HAND_HOUR:
            return ptThis->q31HourAngle;

        case THD_CLOCK_HAND_MINUTE:
            return ptThis->q31MinuteAngle;

        default:
            return 0;
    }
}
