#ifndef PLATFORMER_SPEED_CONTROL_H
#define PLATFORMER_SPEED_CONTROL_H
#include "platformer_game.h"

#define PLATFORMER_CRUISE_SPEED_PPS       90
#define PLATFORMER_TILT_DEADZONE_DEG10     40
#define PLATFORMER_TILT_FULL_DEG10        200
#define PLATFORMER_ACCELERATION_PPS2      90
#define PLATFORMER_BRAKING_PPS2          140

/* Persistent speed in 1/1000 pixel/s; level means cruise, not stop. */
typedef struct {
    int32_t speed_milli;
} platformer_speed_control_t;

static inline void platformer_speed_control_init(platformer_speed_control_t *control)
{
    control->speed_milli = PLATFORMER_CRUISE_SPEED_PPS * 1000;
}

static inline int16_t platformer_speed_control_update(
    platformer_speed_control_t *control, int16_t tilt_deg10,
    bool input_ready, uint32_t elapsed_ms)
{
    /* Match the physics catch-up cap: a stalled frame cannot jump to full speed. */
    if (elapsed_ms > 100u) { elapsed_ms = 100u; }
    int32_t tilt = input_ready ? tilt_deg10 : 0;
    int32_t magnitude = tilt < 0 ? -tilt : tilt;
    if (magnitude > PLATFORMER_TILT_DEADZONE_DEG10) {
        if (magnitude > PLATFORMER_TILT_FULL_DEG10) {
            magnitude = PLATFORMER_TILT_FULL_DEG10;
        }
        int32_t rate = (magnitude - PLATFORMER_TILT_DEADZONE_DEG10)
                     * (tilt > 0 ? PLATFORMER_ACCELERATION_PPS2 : PLATFORMER_BRAKING_PPS2)
                     / (PLATFORMER_TILT_FULL_DEG10 - PLATFORMER_TILT_DEADZONE_DEG10);
        control->speed_milli += (tilt > 0 ? rate : -rate) * (int32_t)elapsed_ms;
        if (control->speed_milli < 0) { control->speed_milli = 0; }
        if (control->speed_milli > PLATFORMER_GAME_MAX_SPEED_PPS * 1000) {
            control->speed_milli = PLATFORMER_GAME_MAX_SPEED_PPS * 1000;
        }
    }
    return (int16_t)(control->speed_milli / 1000);
}
#endif
