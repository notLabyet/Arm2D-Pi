#include "qmi8658_motion.h"

#include <string.h>

/*
 * 本模块尽量使用整数运算：
 * - 加速度单位为 g * 10000；
 * - 陀螺仪单位为 dps * 100；
 * - 位置单位为像素 * QMI8658_MOTION_POSITION_SCALE。
 */
#define QMI8658_MOTION_POSITION_SCALE                100
#define QMI8658_MOTION_CALIBRATION_COUNT             60u
#define QMI8658_MOTION_ACCEL_DEADZONE_G10000         200
#define QMI8658_MOTION_ACCEL_STILL_TOL_G10000        1800
#define QMI8658_MOTION_MAX_VELOCITY_CPX              4200
#define QMI8658_MOTION_ACCEL_AS_VELOCITY_DIV         10
#define QMI8658_MOTION_POSITION_GAIN                 10
#define QMI8658_MOTION_GYRO_DEADZONE_DPS100          100
#define QMI8658_MOTION_GYRO_DEG100_PER_PIXEL         80
#define QMI8658_MOTION_GYRO_RECENTER_STEP_CPX        8

typedef struct qmi8658_motion_state_t {
    /* 静止校准得到的 X/Y 加速度零偏。 */
    int32_t accel_bias[2];
    /* 校准阶段累加值，用于最后取平均。 */
    int32_t accel_bias_sum[2];
    /* 静止校准得到的三轴陀螺仪零偏。 */
    int32_t gyro_bias[3];
    int32_t gyro_bias_sum[3];
    /* 速度和位置均为内部固定点单位，不直接等于屏幕像素。 */
    int32_t velocity_x;
    int32_t velocity_y;
    int32_t position_x;
    int32_t position_y;
    int32_t gyro_offset_x;
    int32_t gyro_offset_y;
    int32_t gyro_offset_z;
    int32_t tap_accel_baseline[3];
    uint32_t tap_candidate_ms;
    uint32_t tap_last_ms;
    uint16_t tap_warmup_ms;
    uint8_t tap_candidate_active;
    uint8_t tap_event_latched;
    /* 屏幕/画布允许移动到的最大像素坐标。 */
    int16_t max_x;
    int16_t max_y;
    uint8_t calibration_count;
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
    /* 去掉很小的噪声，避免静止时积分出缓慢漂移。 */
    if (value > deadzone) {
        return value - deadzone;
    }

    if (value < -deadzone) {
        return value + deadzone;
    }

    return 0;
}

static int32_t qmi8658_motion_abs_i32(int32_t value)
{
    return (value >= 0) ? value : -value;
}

static int16_t qmi8658_motion_pixel_from_cpx(int32_t cpx)
{
    /* 固定点转像素时做四舍五入，负数也保持对称。 */
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

static int32_t qmi8658_motion_raw_accel_to_g10000(int16_t raw)
{
    /* QMI8658 当前量程下的原始加速度换算为 g * 10000。 */
    return ((int32_t)raw * 5000) / 1024;
}

static int32_t qmi8658_motion_raw_gyro_to_dps100(int16_t raw)
{
    /* QMI8658 当前量程下的原始角速度换算为 dps * 100。 */
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

static void qmi8658_motion_clamp_position(void)
{
    /* 把内部固定点边界换算成同单位后再裁剪。 */
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

    s_tMotion.gyro_offset_x =
        qmi8658_motion_limit_i32(s_tMotion.gyro_offset_x, max_x);
    s_tMotion.gyro_offset_y =
        qmi8658_motion_limit_i32(s_tMotion.gyro_offset_y, max_y);
    s_tMotion.gyro_offset_z =
        qmi8658_motion_limit_i32(
        s_tMotion.gyro_offset_z,
        (int32_t)QMI8658_MOTION_GYRO_OFFSET_MAX_Z *
        QMI8658_MOTION_POSITION_SCALE);
}

static void qmi8658_motion_recentre_gyro_offset(void)
{
    if (s_tMotion.gyro_offset_x > QMI8658_MOTION_GYRO_RECENTER_STEP_CPX) {
        s_tMotion.gyro_offset_x -= QMI8658_MOTION_GYRO_RECENTER_STEP_CPX;
    } else if (s_tMotion.gyro_offset_x < -QMI8658_MOTION_GYRO_RECENTER_STEP_CPX) {
        s_tMotion.gyro_offset_x += QMI8658_MOTION_GYRO_RECENTER_STEP_CPX;
    } else {
        s_tMotion.gyro_offset_x = 0;
    }

    if (s_tMotion.gyro_offset_y > QMI8658_MOTION_GYRO_RECENTER_STEP_CPX) {
        s_tMotion.gyro_offset_y -= QMI8658_MOTION_GYRO_RECENTER_STEP_CPX;
    } else if (s_tMotion.gyro_offset_y < -QMI8658_MOTION_GYRO_RECENTER_STEP_CPX) {
        s_tMotion.gyro_offset_y += QMI8658_MOTION_GYRO_RECENTER_STEP_CPX;
    } else {
        s_tMotion.gyro_offset_y = 0;
    }

    if (s_tMotion.gyro_offset_z > QMI8658_MOTION_GYRO_RECENTER_STEP_CPX) {
        s_tMotion.gyro_offset_z -= QMI8658_MOTION_GYRO_RECENTER_STEP_CPX;
    } else if (s_tMotion.gyro_offset_z < -QMI8658_MOTION_GYRO_RECENTER_STEP_CPX) {
        s_tMotion.gyro_offset_z += QMI8658_MOTION_GYRO_RECENTER_STEP_CPX;
    } else {
        s_tMotion.gyro_offset_z = 0;
    }
}

void qmi8658_motion_init(void)
{
    qmi8658_motion_reset();
}

void qmi8658_motion_reset(void)
{
    memset(&s_tMotion, 0, sizeof(s_tMotion));
}

void qmi8658_motion_update_tap_raw(int16_t const raw_data[6])
{
    int32_t const ax =
        qmi8658_motion_mapped_raw_accel_to_g10000(raw_data,
                                                  QMI8658_MOTION_ACCEL_X_AXIS,
                                                  QMI8658_MOTION_ACCEL_X_SIGN);
    int32_t const ay =
        qmi8658_motion_mapped_raw_accel_to_g10000(raw_data,
                                                  QMI8658_MOTION_ACCEL_Y_AXIS,
                                                  QMI8658_MOTION_ACCEL_Y_SIGN);
    int32_t const az =
        qmi8658_motion_mapped_raw_accel_to_g10000(raw_data,
                                                  QMI8658_MOTION_ACCEL_Z_AXIS,
                                                  QMI8658_MOTION_ACCEL_Z_SIGN);
    uint32_t const sample_ms = QMI8658_MOTION_TAP_SAMPLE_PERIOD_MS;

    if (0u == s_tMotion.tap_warmup_ms) {
        s_tMotion.tap_accel_baseline[0] = ax;
        s_tMotion.tap_accel_baseline[1] = ay;
        s_tMotion.tap_accel_baseline[2] = az;
    } else {
        s_tMotion.tap_accel_baseline[0] +=
            (ax - s_tMotion.tap_accel_baseline[0]) >>
            QMI8658_MOTION_TAP_FILTER_SHIFT;
        s_tMotion.tap_accel_baseline[1] +=
            (ay - s_tMotion.tap_accel_baseline[1]) >>
            QMI8658_MOTION_TAP_FILTER_SHIFT;
        s_tMotion.tap_accel_baseline[2] +=
            (az - s_tMotion.tap_accel_baseline[2]) >>
            QMI8658_MOTION_TAP_FILTER_SHIFT;
    }

    if (s_tMotion.tap_warmup_ms < QMI8658_MOTION_TAP_WARMUP_MS) {
        s_tMotion.tap_warmup_ms += sample_ms;
        if (s_tMotion.tap_warmup_ms > QMI8658_MOTION_TAP_WARMUP_MS) {
            s_tMotion.tap_warmup_ms = QMI8658_MOTION_TAP_WARMUP_MS;
        }
        return;
    }

    if (s_tMotion.tap_last_ms < QMI8658_MOTION_TAP_REARM_MS) {
        s_tMotion.tap_last_ms += sample_ms;
        if (s_tMotion.tap_last_ms > QMI8658_MOTION_TAP_REARM_MS) {
            s_tMotion.tap_last_ms = QMI8658_MOTION_TAP_REARM_MS;
        }
    }

    {
        int32_t const dx = ax - s_tMotion.tap_accel_baseline[0];
        int32_t const dy = ay - s_tMotion.tap_accel_baseline[1];
        int32_t const dz = az - s_tMotion.tap_accel_baseline[2];
        int32_t const abs_dx = qmi8658_motion_abs_i32(dx);
        int32_t const abs_dy = qmi8658_motion_abs_i32(dy);
        int32_t const abs_dz = qmi8658_motion_abs_i32(dz);
        bool const z_dominant =
            (abs_dz * 100 >=
             abs_dx * QMI8658_MOTION_TAP_Z_DOMINANCE_PCT) &&
            (abs_dz * 100 >=
             abs_dy * QMI8658_MOTION_TAP_Z_DOMINANCE_PCT);
        bool const lateral_ok =
            (abs_dx <= QMI8658_MOTION_TAP_XY_LIMIT_G10000) &&
            (abs_dy <= QMI8658_MOTION_TAP_XY_LIMIT_G10000);

        if (!s_tMotion.tap_candidate_active) {
            if ((s_tMotion.tap_last_ms >= QMI8658_MOTION_TAP_REARM_MS) &&
                (abs_dz >= QMI8658_MOTION_TAP_Z_PEAK_G10000) &&
                lateral_ok &&
                z_dominant) {
                s_tMotion.tap_candidate_active = 1u;
                s_tMotion.tap_candidate_ms = 0u;
            }
            return;
        }

        s_tMotion.tap_candidate_ms += sample_ms;

        if ((abs_dz <= QMI8658_MOTION_TAP_Z_REBOUND_G10000) &&
            (s_tMotion.tap_candidate_ms >= QMI8658_MOTION_TAP_MIN_GAP_MS)) {
            s_tMotion.tap_event_latched = 1u;
            s_tMotion.tap_candidate_active = 0u;
            s_tMotion.tap_candidate_ms = 0u;
            s_tMotion.tap_last_ms = 0u;
            return;
        }

        if ((s_tMotion.tap_candidate_ms >=
             QMI8658_MOTION_TAP_CANDIDATE_TIMEOUT_MS) ||
            !lateral_ok ||
            !z_dominant) {
            s_tMotion.tap_candidate_active = 0u;
            s_tMotion.tap_candidate_ms = 0u;
        }
    }
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
    /* 按板子安装方向重新映射 X/Y 加速度轴。 */
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
    /* 判断模长是否接近 1g，用来确认设备大致处于静止状态。 */
    int32_t const still_low =
        10000 - QMI8658_MOTION_ACCEL_STILL_TOL_G10000;
    int32_t const still_high =
        10000 + QMI8658_MOTION_ACCEL_STILL_TOL_G10000;
    bool const mostly_still =
        (acc_mag2 > (int64_t)still_low * still_low) &&
        (acc_mag2 < (int64_t)still_high * still_high);

    if (!qmi8658_motion_is_ready()) {
        /* 上电后先收集静止样本，作为加速度和陀螺仪零偏。 */
        if (mostly_still) {
            s_tMotion.accel_bias_sum[0] += ax;
            s_tMotion.accel_bias_sum[1] += ay;
            s_tMotion.gyro_bias_sum[0] += gx;
            s_tMotion.gyro_bias_sum[1] += gy;
            s_tMotion.gyro_bias_sum[2] += gz;
            s_tMotion.calibration_count++;

            if (qmi8658_motion_is_ready()) {
                /* 采样数量足够后取平均值，后续更新都先扣除该偏置。 */
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
        }

        s_tMotion.velocity_x = 0;
        s_tMotion.velocity_y = 0;
        return;
    }

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
    /* Treat current acceleration as the velocity input for position integration. */
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

    /* 速度积分成位置，最后统一限制在设置的边界内。 */
    s_tMotion.position_x +=
        s_tMotion.velocity_x * QMI8658_MOTION_POSITION_GAIN;
    s_tMotion.position_y +=
        s_tMotion.velocity_y * QMI8658_MOTION_POSITION_GAIN;
    s_tMotion.gyro_offset_x +=
        qmi8658_motion_gyro_to_offset_cpx_delta(corrected_gx);
    s_tMotion.gyro_offset_y +=
        qmi8658_motion_gyro_to_offset_cpx_delta(corrected_gy);
    s_tMotion.gyro_offset_z +=
        qmi8658_motion_gyro_to_offset_cpx_delta(corrected_gz);

    if ((0 == corrected_gx) && (0 == corrected_gy) && (0 == corrected_gz)) {
        qmi8658_motion_recentre_gyro_offset();
    }

    qmi8658_motion_clamp_position();
    qmi8658_motion_clamp_gyro_offset();
}
