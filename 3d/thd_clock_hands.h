#ifndef __THD_CLOCK_HANDS_H__
#define __THD_CLOCK_HANDS_H__

#include <stdint.h>
#include "__arm_2d_math.h"

#ifndef THD_CLOCK_HANDS_MINUTE_PERIOD_MS
#   define THD_CLOCK_HANDS_MINUTE_PERIOD_MS    (60u * 1000u)
#endif

#define THD_CLOCK_HANDS_HOUR_PERIOD_MS         \
    (12u * THD_CLOCK_HANDS_MINUTE_PERIOD_MS)

typedef enum thd_clock_hand_t {
    THD_CLOCK_HAND_NONE = 0,
    THD_CLOCK_HAND_HOUR,
    THD_CLOCK_HAND_MINUTE,
} thd_clock_hand_t;

typedef struct thd_clock_hands_t {
    uint32_t wLastTimestampMs;
    uint32_t wElapsedMs;
    q31_t q31HourAngle;
    q31_t q31MinuteAngle;
    uint8_t bInitialized;
} thd_clock_hands_t;

void thd_clock_hands_init(thd_clock_hands_t *ptThis);
void thd_clock_hands_update(thd_clock_hands_t *ptThis,
                            uint32_t wTimestampMs);
q31_t thd_clock_hands_get_angle(const thd_clock_hands_t *ptThis,
                                thd_clock_hand_t tHand);

#endif
