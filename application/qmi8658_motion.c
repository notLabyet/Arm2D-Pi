#include "qmi8658_motion.h"

#include <string.h>

#define QMI8658_MOTION_POSITION_SCALE                100
#define QMI8658_MOTION_CALIBRATION_COUNT             60u
#define QMI8658_MOTION_ACCEL_DEADZONE_G10000         200
#define QMI8658_MOTION_ACCEL_STILL_TOL_G10000        1800
#define QMI8658_MOTION_MAX_VELOCITY_CPX              4200
#define QMI8658_MOTION_ACCEL_AS_VELOCITY_DIV         10
#define QMI8658_MOTION_POSITION_GAIN                 10
#define QMI8658_MOTION_GYRO_DEADZONE_DPS100          18
#define QMI8658_MOTION_GYRO_DEG100_PER_PIXEL         22
#define QMI8658_MOTION_GYRO_FILTER_SHIFT             2
#define QMI8658_MOTION_GYRO_REVERSE_DEADZONE_DPS100  80
#define QMI8658_MOTION_CALIB_GYRO_STILL_DPS100       250

typedef struct qmi8658_motion_state_t {
    int32_t accel_bias[2];
    int32_t accel_bias_sum[2];
    int32_t gyro_bias[3];
    int32_t gyro_bias_sum[3];
    int32_t velocity_x;
    int32_t velocity_y;
    int32_t position_x;
    int32_t position_y;
    int32_t gyro_offset_x;
    int32_t gyro_offset_y;
    int32_t gyro_offset_z;
    int32_t gyro_filtered[3];
    int16_t max_x;
    int16_t max_y;
    uint8_t calibration_count;
    uint8_t tap_event_latched;
} qmi8658_motion_state_t;

static qmi8658_motion_state_t s_tMotion;

static int32_t qmi8658_motion_limit_i32(int32_t value, int32_t limit)
{
    if (value > limit) {
        return limit;
    }

    if (value < -limit) {
        return -limit;
    }

    return value;
}

static int32_t qmi8658_motion_apply_deadzone_i32(int32_t value, int32_t deadzone)
{
    if (value > deadzone) {
        return value - deadzone;
    }

    if (value < -deadzone) {
        return value + deadzone;
    }

    return 0;
}

static int16_t qmi8658_motion_pixel_from_cpx(int32_t cpx)
{
    if (cpx >= 0) {
        return (int16_t)((cpx + (QMI8658_MOTION_POSITION_SCALE / 2)) /
                         QMI8658_MOTION_POSITION_SCALE);
    }

    return (int16_t)((cpx - (QMI8658_MOTION_POSITION_SCALE / 2)) /
                      QMI8658_MOTION_POSITION_SCALE);
}

static uint8_t qmi8658_motion_normalize_axis(uint8_t axis)
{
    return (axis <= 2u) ? axis : 0u;
}

static int32_t qmi8658_motion_filter_i32(int32_t value, int32_t *state)
{
    *state += (value - *state) >> QMI8658_MOTION_GYRO_FILTER_SHIFT;
    return *state;
}

static int32_t qmi8658_motion_raw_accel_to_g10000(int16_t raw)
{
    return ((int32_t)raw * 5000) / 1024;
}

static int32_t qmi8658_motion_raw_gyro_to_dps100(int16_t raw)
{
    return ((int32_t)raw * 25) / 4;
}

static int32_t qmi8658_motion_mapped_raw_accel_to_g10000(int16_t const raw_data[6],
                                                         uint8_t axis,
                                                         int32_t sign)
{
    axis = qmi8658_motion_normalize_axis(axis);
    return sign * qmi8658_motion_raw_accel_to_g10000(raw_data[axis]);
}

static int32_t qmi8658_motion_mapped_raw_gyro_to_dps100(int16_t const raw_data[6],
                                                        uint8_t axis,
                                                        int32_t sign)
{
    axis = qmi8658_motion_normalize_axis(axis);
    return sign * qmi8658_motion_raw_gyro_to_dps100(raw_data[3u + axis]);
}

static int32_t qmi8658_motion_gyro_to_offset_cpx_delta(int32_t gyro_dps100)
{
    int64_t const numerator =
        (int64_t)gyro_dps100 *
        (int64_t)QMI8658_MOTION_SAMPLE_PERIOD_MS *
        (int64_t)QMI8658_MOTION_POSITION_SCALE;

    return (int32_t)(numerator /
                     (1000 * QMI8658_MOTION_GYRO_DEG100_PER_PIXEL));
}

static int32_t qmi8658_motion_apply_reverse_deadzone_i32(int32_t gyro_dps100,
                                                          int32_t offset_cpx)
{
    if (((offset_cpx > 0) && (gyro_dps100 < 0))
    ||  ((offset_cpx < 0) && (gyro_dps100 > 0))) {
        return qmi8658_motion_apply_deadzone_i32(
            gyro_dps100,
            QMI8658_MOTION_GYRO_REVERSE_DEADZONE_DPS100);
    }

    return gyro_dps100;
}

static bool qmi8658_motion_gyro_is_still(int32_t gx,
                                         int32_t gy,
                                         int32_t gz)
{
    return (gx > -QMI8658_MOTION_CALIB_GYRO_STILL_DPS100) &&
           (gx <  QMI8658_MOTION_CALIB_GYRO_STILL_DPS100) &&
           (gy > -QMI8658_MOTION_CALIB_GYRO_STILL_DPS100) &&
           (gy <  QMI8658_MOTION_CALIB_GYRO_STILL_DPS100) &&
           (gz > -QMI8658_MOTION_CALIB_GYRO_STILL_DPS100) &&
           (gz <  QMI8658_MOTION_CALIB_GYRO_STILL_DPS100);
}

static void qmi8658_motion_integrate_gyro_offset(int32_t gx,
                                                 int32_t gy,
                                                 int32_t gz)
{
    s_tMotion.gyro_offset_x += qmi8658_motion_gyro_to_offset_cpx_delta(gx);
    s_tMotion.gyro_offset_y += qmi8658_motion_gyro_to_offset_cpx_delta(gy);
    s_tMotion.gyro_offset_z += qmi8658_motion_gyro_to_offset_cpx_delta(gz);
}

static void qmi8658_motion_clamp_position(void)
{
    int32_t const max_x =
        (int32_t)s_tMotion.max_x * QMI8658_MOTION_POSITION_SCALE;
    int32_t const max_y =
        (int32_t)s_tMotion.max_y * QMI8658_MOTION_POSITION_SCALE;

    if (s_tMotion.position_x < 0) {
        s_tMotion.position_x = 0;
        if (s_tMotion.velocity_x < 0) {
            s_tMotion.velocity_x = 0;
        }
    } else if (s_tMotion.position_x > max_x) {
        s_tMotion.position_x = max_x;
        if (s_tMotion.velocity_x > 0) {
            s_tMotion.velocity_x = 0;
        }
    }

    if (s_tMotion.position_y < 0) {
        s_tMotion.position_y = 0;
        if (s_tMotion.velocity_y < 0) {
            s_tMotion.velocity_y = 0;
        }
    } else if (s_tMotion.position_y > max_y) {
        s_tMotion.position_y = max_y;
        if (s_tMotion.velocity_y > 0) {
            s_tMotion.velocity_y = 0;
        }
    }
}

static void qmi8658_motion_clamp_gyro_offset(void)
{
    int32_t const max_x =
        (int32_t)QMI8658_MOTION_GYRO_OFFSET_MAX_X *
        QMI8658_MOTION_POSITION_SCALE;
    int32_t const max_y =
        (int32_t)QMI8658_MOTION_GYRO_OFFSET_MAX_Y *
        QMI8658_MOTION_POSITION_SCALE;
    int32_t const max_z =
        (int32_t)QMI8658_MOTION_GYRO_OFFSET_MAX_Z *
        QMI8658_MOTION_POSITION_SCALE;

    s_tMotion.gyro_offset_x =
        qmi8658_motion_limit_i32(s_tMotion.gyro_offset_x, max_x);
    s_tMotion.gyro_offset_y =
        qmi8658_motion_limit_i32(s_tMotion.gyro_offset_y, max_y);
    s_tMotion.gyro_offset_z =
        qmi8658_motion_limit_i32(s_tMotion.gyro_offset_z, max_z);
}

void qmi8658_motion_init(void)
{
    qmi8658_motion_reset();
}

void qmi8658_motion_reset(void)
{
    memset(&s_tMotion, 0, sizeof(s_tMotion));
}

void qmi8658_motion_report_z_tap(void)
{
    s_tMotion.tap_event_latched = 1u;
}

void qmi8658_motion_reset_position(int16_t x, int16_t y)
{
    if (x < 0) {
        x = 0;
    }

    if (y < 0) {
        y = 0;
    }

    if (x > s_tMotion.max_x) {
        x = s_tMotion.max_x;
    }

    if (y > s_tMotion.max_y) {
        y = s_tMotion.max_y;
    }

    s_tMotion.position_x = (int32_t)x * QMI8658_MOTION_POSITION_SCALE;
    s_tMotion.position_y = (int32_t)y * QMI8658_MOTION_POSITION_SCALE;
    s_tMotion.velocity_x = 0;
    s_tMotion.velocity_y = 0;
}

void qmi8658_motion_set_bounds(int16_t max_x, int16_t max_y)
{
    if (max_x < 0) {
        max_x = 0;
    }

    if (max_y < 0) {
        max_y = 0;
    }

    s_tMotion.max_x = max_x;
    s_tMotion.max_y = max_y;
    qmi8658_motion_clamp_position();
}

bool qmi8658_motion_get_position(int16_t *x, int16_t *y)
{
    if (x != NULL) {
        *x = qmi8658_motion_pixel_from_cpx(s_tMotion.position_x);
    }

    if (y != NULL) {
        *y = qmi8658_motion_pixel_from_cpx(s_tMotion.position_y);
    }

    return qmi8658_motion_is_ready();
}

bool qmi8658_motion_get_gyro_offset(int16_t *x, int16_t *y)
{
    if (x != NULL) {
        *x = qmi8658_motion_pixel_from_cpx(s_tMotion.gyro_offset_x);
    }

    if (y != NULL) {
        *y = qmi8658_motion_pixel_from_cpx(s_tMotion.gyro_offset_y);
    }

    return qmi8658_motion_is_ready();
}

bool qmi8658_motion_get_gyro_offset_xyz(int16_t *x,
                                        int16_t *y,
                                        int16_t *z)
{
    if (x != NULL) {
        *x = qmi8658_motion_pixel_from_cpx(s_tMotion.gyro_offset_x);
    }

    if (y != NULL) {
        *y = qmi8658_motion_pixel_from_cpx(s_tMotion.gyro_offset_y);
    }

    if (z != NULL) {
        *z = qmi8658_motion_pixel_from_cpx(s_tMotion.gyro_offset_z);
    }

    return qmi8658_motion_is_ready();
}

bool qmi8658_motion_consume_z_tap(void)
{
    bool const tapped = (0u != s_tMotion.tap_event_latched);
    s_tMotion.tap_event_latched = 0u;
    return tapped;
}

bool qmi8658_motion_is_ready(void)
{
    return s_tMotion.calibration_count >= QMI8658_MOTION_CALIBRATION_COUNT;
}

void qmi8658_motion_update_raw(int16_t const raw_data[6])
{
    int32_t const ax =
        qmi8658_motion_mapped_raw_accel_to_g10000(raw_data,
                                                  QMI8658_MOTION_ACCEL_X_AXIS,
                                                  QMI8658_MOTION_ACCEL_X_SIGN);
    int32_t const ay =
        qmi8658_motion_mapped_raw_accel_to_g10000(raw_data,
                                                  QMI8658_MOTION_ACCEL_Y_AXIS,
                                                  QMI8658_MOTION_ACCEL_Y_SIGN);
    int32_t const az = qmi8658_motion_raw_accel_to_g10000(raw_data[2]);
    int32_t const gx = qmi8658_motion_raw_gyro_to_dps100(raw_data[3]);
    int32_t const gy = qmi8658_motion_raw_gyro_to_dps100(raw_data[4]);
    int32_t const gz = qmi8658_motion_raw_gyro_to_dps100(raw_data[5]);
    uint8_t const gyro_x_axis =
        qmi8658_motion_normalize_axis(QMI8658_MOTION_GYRO_X_AXIS);
    uint8_t const gyro_y_axis =
        qmi8658_motion_normalize_axis(QMI8658_MOTION_GYRO_Y_AXIS);
    uint8_t const gyro_z_axis =
        qmi8658_motion_normalize_axis(QMI8658_MOTION_GYRO_Z_AXIS);
    int32_t const mapped_gx =
        qmi8658_motion_mapped_raw_gyro_to_dps100(raw_data,
                                                 gyro_x_axis,
                                                 QMI8658_MOTION_GYRO_X_SIGN);
    int32_t const mapped_gy =
        qmi8658_motion_mapped_raw_gyro_to_dps100(raw_data,
                                                 gyro_y_axis,
                                                 QMI8658_MOTION_GYRO_Y_SIGN);
    int32_t const mapped_gz =
        qmi8658_motion_mapped_raw_gyro_to_dps100(raw_data,
                                                 gyro_z_axis,
                                                 QMI8658_MOTION_GYRO_Z_SIGN);
    int64_t const acc_mag2 =
        (int64_t)ax * ax + (int64_t)ay * ay + (int64_t)az * az;
    int32_t const still_low =
        10000 - QMI8658_MOTION_ACCEL_STILL_TOL_G10000;
    int32_t const still_high =
        10000 + QMI8658_MOTION_ACCEL_STILL_TOL_G10000;
    bool const mostly_still =
        (acc_mag2 > (int64_t)still_low * still_low) &&
        (acc_mag2 < (int64_t)still_high * still_high);

    if (!qmi8658_motion_is_ready()) {
        bool const gyro_still = qmi8658_motion_gyro_is_still(gx, gy, gz);

        if (mostly_still && gyro_still) {
            s_tMotion.accel_bias_sum[0] += ax;
            s_tMotion.accel_bias_sum[1] += ay;
            s_tMotion.gyro_bias_sum[0] += gx;
            s_tMotion.gyro_bias_sum[1] += gy;
            s_tMotion.gyro_bias_sum[2] += gz;
            s_tMotion.calibration_count++;

            if (qmi8658_motion_is_ready()) {
                s_tMotion.accel_bias[0] =
                    s_tMotion.accel_bias_sum[0] /
                    QMI8658_MOTION_CALIBRATION_COUNT;
                s_tMotion.accel_bias[1] =
                    s_tMotion.accel_bias_sum[1] /
                    QMI8658_MOTION_CALIBRATION_COUNT;
                s_tMotion.gyro_bias[0] =
                    s_tMotion.gyro_bias_sum[0] /
                    QMI8658_MOTION_CALIBRATION_COUNT;
                s_tMotion.gyro_bias[1] =
                    s_tMotion.gyro_bias_sum[1] /
                    QMI8658_MOTION_CALIBRATION_COUNT;
                s_tMotion.gyro_bias[2] =
                    s_tMotion.gyro_bias_sum[2] /
                    QMI8658_MOTION_CALIBRATION_COUNT;
            }
        } else if (!gyro_still) {
            int32_t const preview_gx =
                qmi8658_motion_apply_deadzone_i32(mapped_gx,
                                                  QMI8658_MOTION_GYRO_DEADZONE_DPS100);
            int32_t const preview_gy =
                qmi8658_motion_apply_deadzone_i32(mapped_gy,
                                                  QMI8658_MOTION_GYRO_DEADZONE_DPS100);
            int32_t const preview_gz =
                qmi8658_motion_apply_deadzone_i32(mapped_gz,
                                                  QMI8658_MOTION_GYRO_DEADZONE_Z_DPS100);

            qmi8658_motion_integrate_gyro_offset(preview_gx,
                                                 preview_gy,
                                                 preview_gz);
            qmi8658_motion_clamp_gyro_offset();
        }

        s_tMotion.velocity_x = 0;
        s_tMotion.velocity_y = 0;
        return;
    }

    {
        int32_t const corrected_ax =
            qmi8658_motion_apply_deadzone_i32(ax - s_tMotion.accel_bias[0],
                                              QMI8658_MOTION_ACCEL_DEADZONE_G10000);
        int32_t const corrected_ay =
            qmi8658_motion_apply_deadzone_i32(ay - s_tMotion.accel_bias[1],
                                              QMI8658_MOTION_ACCEL_DEADZONE_G10000);
        int32_t const corrected_gx =
            qmi8658_motion_apply_deadzone_i32(
                mapped_gx - s_tMotion.gyro_bias[gyro_x_axis],
                QMI8658_MOTION_GYRO_DEADZONE_DPS100);
        int32_t const corrected_gy =
            qmi8658_motion_apply_deadzone_i32(
                mapped_gy - s_tMotion.gyro_bias[gyro_y_axis],
                QMI8658_MOTION_GYRO_DEADZONE_DPS100);
        int32_t const corrected_gz =
            qmi8658_motion_apply_deadzone_i32(
                mapped_gz - s_tMotion.gyro_bias[gyro_z_axis],
                QMI8658_MOTION_GYRO_DEADZONE_Z_DPS100);
        int32_t const filtered_gx =
            qmi8658_motion_filter_i32(corrected_gx,
                                      &s_tMotion.gyro_filtered[gyro_x_axis]);
        int32_t const filtered_gy =
            qmi8658_motion_filter_i32(corrected_gy,
                                      &s_tMotion.gyro_filtered[gyro_y_axis]);
        int32_t const filtered_gz =
            qmi8658_motion_filter_i32(corrected_gz,
                                      &s_tMotion.gyro_filtered[gyro_z_axis]);
        int32_t const effective_gx =
            qmi8658_motion_apply_reverse_deadzone_i32(
                filtered_gx,
                s_tMotion.gyro_offset_x);
        int32_t const effective_gy =
            qmi8658_motion_apply_reverse_deadzone_i32(
                filtered_gy,
                s_tMotion.gyro_offset_y);
        int32_t const effective_gz =
            qmi8658_motion_apply_reverse_deadzone_i32(
                filtered_gz,
                s_tMotion.gyro_offset_z);

        s_tMotion.velocity_x =
            corrected_ax / QMI8658_MOTION_ACCEL_AS_VELOCITY_DIV;
        s_tMotion.velocity_y =
            -corrected_ay / QMI8658_MOTION_ACCEL_AS_VELOCITY_DIV;

        s_tMotion.velocity_x =
            qmi8658_motion_limit_i32(s_tMotion.velocity_x,
                                     QMI8658_MOTION_MAX_VELOCITY_CPX);
        s_tMotion.velocity_y =
            qmi8658_motion_limit_i32(s_tMotion.velocity_y,
                                     QMI8658_MOTION_MAX_VELOCITY_CPX);

        s_tMotion.position_x +=
            s_tMotion.velocity_x * QMI8658_MOTION_POSITION_GAIN;
        s_tMotion.position_y +=
            s_tMotion.velocity_y * QMI8658_MOTION_POSITION_GAIN;
        qmi8658_motion_integrate_gyro_offset(effective_gx,
                                             effective_gy,
                                             effective_gz);

    }

    qmi8658_motion_clamp_position();
    qmi8658_motion_clamp_gyro_offset();
}
