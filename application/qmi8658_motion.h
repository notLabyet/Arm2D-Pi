#ifndef __QMI8658_MOTION_H__
#define __QMI8658_MOTION_H__

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef QMI8658_MOTION_SAMPLE_PERIOD_MS
#   define QMI8658_MOTION_SAMPLE_PERIOD_MS          20u
#endif

#ifndef QMI8658_FIRE_USE_PRISM
#   define QMI8658_FIRE_USE_PRISM                    1
#endif

#ifndef QMI8658_MOTION_ACCEL_X_AXIS
#   define QMI8658_MOTION_ACCEL_X_AXIS              0u
#endif

#ifndef QMI8658_MOTION_ACCEL_Y_AXIS
#   define QMI8658_MOTION_ACCEL_Y_AXIS              1u
#endif

#ifndef QMI8658_MOTION_ACCEL_Z_AXIS
#   define QMI8658_MOTION_ACCEL_Z_AXIS              2u
#endif

#ifndef QMI8658_MOTION_ACCEL_X_SIGN
#   define QMI8658_MOTION_ACCEL_X_SIGN              1
#endif

#ifndef QMI8658_MOTION_ACCEL_Y_SIGN
#   define QMI8658_MOTION_ACCEL_Y_SIGN              1
#endif

#ifndef QMI8658_MOTION_ACCEL_Z_SIGN
#   define QMI8658_MOTION_ACCEL_Z_SIGN              1
#endif

#ifndef QMI8658_MOTION_GYRO_X_AXIS
#   define QMI8658_MOTION_GYRO_X_AXIS               0u
#endif

#ifndef QMI8658_MOTION_GYRO_Y_AXIS
#   define QMI8658_MOTION_GYRO_Y_AXIS               1u
#endif

#ifndef QMI8658_MOTION_GYRO_Z_AXIS
#   define QMI8658_MOTION_GYRO_Z_AXIS               2u
#endif

#ifndef QMI8658_MOTION_GYRO_X_SIGN
#   define QMI8658_MOTION_GYRO_X_SIGN               1
#endif

#ifndef QMI8658_MOTION_GYRO_Y_SIGN
#   define QMI8658_MOTION_GYRO_Y_SIGN               1
#endif

#ifndef QMI8658_MOTION_GYRO_Z_SIGN
#   define QMI8658_MOTION_GYRO_Z_SIGN               1
#endif

#ifndef QMI8658_MOTION_GYRO_OFFSET_MAX_X
#   define QMI8658_MOTION_GYRO_OFFSET_MAX_X         24
#endif

#ifndef QMI8658_MOTION_GYRO_OFFSET_MAX_Y
#   define QMI8658_MOTION_GYRO_OFFSET_MAX_Y         24
#endif

#ifndef QMI8658_MOTION_GYRO_OFFSET_MAX_Z
#   define QMI8658_MOTION_GYRO_OFFSET_MAX_Z         24
#endif

#ifndef QMI8658_MOTION_GYRO_DEADZONE_Z_DPS100
#   define QMI8658_MOTION_GYRO_DEADZONE_Z_DPS100    100
#endif

typedef struct qmi8658_fire_motion_t {
    int16_t gravity_x_q15;
    int16_t gravity_y_q15;
    int16_t acceleration_x_q15;
    int16_t acceleration_y_q15;
    uint16_t disturbance_q15;
} qmi8658_fire_motion_t;

void qmi8658_motion_init(void);
void qmi8658_motion_reset(void);

bool qmi8658_motion_poll(uint32_t current_ms);
void qmi8658_motion_update_raw(int16_t const raw_data[6]);
void qmi8658_motion_report_z_tap(void);

void qmi8658_motion_reset_position(int16_t x, int16_t y);
void qmi8658_motion_set_bounds(int16_t max_x, int16_t max_y);

bool qmi8658_motion_get_position(int16_t *x, int16_t *y);
bool qmi8658_motion_get_gyro_offset(int16_t *x, int16_t *y);
bool qmi8658_motion_get_gyro_offset_xyz(int16_t *x, int16_t *y, int16_t *z);
bool qmi8658_motion_get_fire_motion(qmi8658_fire_motion_t *motion);
bool qmi8658_motion_consume_z_tap(void);
bool qmi8658_motion_is_ready(void);

#ifdef __cplusplus
}
#endif

#endif
