#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "ThD_test.h"

#ifndef THD_HOLO_CALIBRATION_SAMPLES
#   define THD_HOLO_CALIBRATION_SAMPLES        16
#endif

#ifndef THD_HOLO_ACCEL_FILTER_SHIFT
#   define THD_HOLO_ACCEL_FILTER_SHIFT         2
#endif

#ifndef THD_HOLO_OUTPUT_FILTER_SHIFT
#   define THD_HOLO_OUTPUT_FILTER_SHIFT        1
#endif

#define THD_HOLO_TILT_DEADZONE_Q16             Q16(0.012f)
#define THD_HOLO_TILT_GAIN_Q16                 Q16(1.15f)
#define THD_HOLO_TILT_LIMIT_Q16                Q16(0.35f)
#define THD_HOLO_YAW_LIMIT_Q16                 Q16(0.26f)
#define THD_HOLO_YAW_GYRO_DEADZONE_Q16         Q16(0.026f)
#define THD_HOLO_YAW_INTEGRATION_GAIN_Q16      Q16(1.40f)
#define THD_HOLO_YAW_RECENTER_SHIFT            6
#define THD_HOLO_YAW_SNAP_Q16                  Q16(0.001f)
#define THD_HOLO_CALIB_GYRO_LIMIT_Q16          Q16(0.35f)
#define THD_HOLO_GYRO_EXTRA_LEAD_Q16           Q16(0.010f)
#define THD_HOLO_SAMPLE_DT_Q16                 Q16(0.020f)
#define THD_HOLO_RAD_TO_TURN_Q16               Q16(0.15915494f)

/* CTRL2=0x33: +/-16 g, CTRL3=0x73: +/-2048 dps. */
#define THD_HOLO_ACCEL_RAW_TO_Q16               32
#define THD_HOLO_GYRO_RAW_TO_RAD_Q16            71

static q16_t thd_abs_q16(q16_t value)
{
    return (value >= 0) ? value : -value;
}

static q16_t thd_limit_q16(q16_t value, q16_t limit)
{
    if (value > limit) {
        return limit;
    }
    if (value < -limit) {
        return -limit;
    }
    return value;
}

static q16_t thd_apply_deadzone_q16(q16_t value, q16_t deadzone)
{
    if (value > deadzone) {
        return value - deadzone;
    }
    if (value < -deadzone) {
        return value + deadzone;
    }
    return 0;
}

static q16_t thd_angle_difference_q16(q16_t angle, q16_t reference)
{
    q16_t difference = angle - reference;

    if (difference > PI_Q16) {
        difference -= 2 * PI_Q16;
    } else if (difference < -PI_Q16) {
        difference += 2 * PI_Q16;
    }
    return difference;
}

static q16_t thd_hypot_approx_q16(q16_t x, q16_t y)
{
    q16_t ax = thd_abs_q16(x);
    q16_t ay = thd_abs_q16(y);
    q16_t maximum = (ax > ay) ? ax : ay;
    q16_t minimum = (ax > ay) ? ay : ax;

    return maximum + ((minimum * 3) >> 3);
}

q16_t sqrt_q16(q16_t x)
{
    if (x <= 0) {
        return 0;
    }
    q16_t guess = (x > (1 << 16)) ? x : (1 << 16);
    guess = (guess + div_q16(x, guess)) >> 1;
    guess = (guess + div_q16(x, guess)) >> 1;
    guess = (guess + div_q16(x, guess)) >> 1;
    guess = (guess + div_q16(x, guess)) >> 1;
    return guess;
}

q16_t atan2_approx(q16_t y, q16_t x)
{
    q16_t abs_x = thd_abs_q16(x);
    q16_t abs_y = thd_abs_q16(y);
    q16_t ratio;
    q16_t correction;
    q16_t angle;
    const q16_t pi_over_4 = Q16(0.78539816f);
    const q16_t curve = Q16(0.273f);

    if ((0 == abs_x) && (0 == abs_y)) {
        return 0;
    }

    if (abs_x >= abs_y) {
        ratio = div_q16(y, x);
        correction = pi_over_4
                   + mul_q16(curve, Q16_ONE - thd_abs_q16(ratio));
        angle = mul_q16(ratio, correction);
        if (x < 0) {
            angle += (y >= 0) ? PI_Q16 : -PI_Q16;
        }
    } else {
        ratio = div_q16(x, y);
        correction = pi_over_4
                   + mul_q16(curve, Q16_ONE - thd_abs_q16(ratio));
        angle = ((y > 0) ? (PI_Q16 / 2) : (-PI_Q16 / 2))
              - mul_q16(ratio, correction);
    }

    return angle;
}

void imu_update_euler(attitude_t *att,
                      q16_t ax, q16_t ay, q16_t az,
                      q16_t gx, q16_t gy, q16_t gz,
                      q16_t dt)
{
    q16_t magnitude = thd_abs_q16(ax)
                    + thd_abs_q16(ay)
                    + thd_abs_q16(az);

    if (!att->initialized) {
        if (magnitude < Q16(0.1f)) {
            return;
        }
        att->gravity_x = ax;
        att->gravity_y = ay;
        att->gravity_z = az;
        att->gravity_magnitude = magnitude;
        att->initialized = 1u;
    }

    bool accel_confident =
        (magnitude > (att->gravity_magnitude >> 1))
     && (magnitude < (att->gravity_magnitude << 1));

    if (accel_confident) {
        att->gravity_x += (ax - att->gravity_x) >> THD_HOLO_ACCEL_FILTER_SHIFT;
        att->gravity_y += (ay - att->gravity_y) >> THD_HOLO_ACCEL_FILTER_SHIFT;
        att->gravity_z += (az - att->gravity_z) >> THD_HOLO_ACCEL_FILTER_SHIFT;

        if (att->calibration_samples < THD_HOLO_CALIBRATION_SAMPLES) {
            att->gravity_magnitude +=
                (magnitude - att->gravity_magnitude) >> 2;
        }
    }

    q16_t roll_acc = atan2_approx(att->gravity_y, att->gravity_z);
    q16_t pitch_acc = atan2_approx(
        -att->gravity_x,
        thd_hypot_approx_q16(att->gravity_y, att->gravity_z));
    bool calibration_still =
        (thd_abs_q16(gx) + thd_abs_q16(gy) + thd_abs_q16(gz))
        < THD_HOLO_CALIB_GYRO_LIMIT_Q16;

    if (att->calibration_samples < THD_HOLO_CALIBRATION_SAMPLES) {
        if (accel_confident && calibration_still) {
            att->neutral_roll_sum += roll_acc;
            att->neutral_pitch_sum += pitch_acc;
            att->gyro_z_bias_sum += gz;
            att->calibration_samples++;

            if (att->calibration_samples == THD_HOLO_CALIBRATION_SAMPLES) {
                att->neutral_roll =
                    att->neutral_roll_sum / THD_HOLO_CALIBRATION_SAMPLES;
                att->neutral_pitch =
                    att->neutral_pitch_sum / THD_HOLO_CALIBRATION_SAMPLES;
                att->gyro_z_bias =
                    att->gyro_z_bias_sum / THD_HOLO_CALIBRATION_SAMPLES;
            }
        }
        return;
    }

    q16_t roll_target = thd_apply_deadzone_q16(
        thd_angle_difference_q16(roll_acc, att->neutral_roll),
        THD_HOLO_TILT_DEADZONE_Q16);
    q16_t pitch_target = thd_apply_deadzone_q16(
        thd_angle_difference_q16(pitch_acc, att->neutral_pitch),
        THD_HOLO_TILT_DEADZONE_Q16);
    q16_t gyro_lead = dt + THD_HOLO_GYRO_EXTRA_LEAD_Q16;

    roll_target = mul_q16(roll_target, THD_HOLO_TILT_GAIN_Q16)
                + mul_q16(gx, gyro_lead);
    pitch_target = mul_q16(pitch_target, THD_HOLO_TILT_GAIN_Q16)
                 + mul_q16(gy, gyro_lead);

    att->target_roll = thd_limit_q16(roll_target, THD_HOLO_TILT_LIMIT_Q16);
    att->target_pitch = thd_limit_q16(pitch_target, THD_HOLO_TILT_LIMIT_Q16);

    /* No magnetometer is available, so yaw is relative and spring-centred. */
    q16_t yaw_rate = thd_apply_deadzone_q16(
        gz - att->gyro_z_bias,
        THD_HOLO_YAW_GYRO_DEADZONE_Q16);
    if (0 != yaw_rate) {
        q16_t yaw_delta = mul_q16(yaw_rate, dt);
        att->target_yaw += mul_q16(
            yaw_delta,
            THD_HOLO_YAW_INTEGRATION_GAIN_Q16);
        att->target_yaw = thd_limit_q16(
            att->target_yaw,
            THD_HOLO_YAW_LIMIT_Q16);
    } else {
        att->target_yaw -= att->target_yaw >> THD_HOLO_YAW_RECENTER_SHIFT;
        if (thd_abs_q16(att->target_yaw) < THD_HOLO_YAW_SNAP_Q16) {
            att->target_yaw = 0;
        }
    }
}

void imu_holo_update_raw(attitude_t *att, const int16_t raw_data[6])
{
    if ((NULL == att) || (NULL == raw_data)) {
        return;
    }

    imu_update_euler(
        att,
        (q16_t)((int32_t)raw_data[0] * THD_HOLO_ACCEL_RAW_TO_Q16),
        (q16_t)((int32_t)raw_data[1] * THD_HOLO_ACCEL_RAW_TO_Q16),
        (q16_t)((int32_t)raw_data[2] * THD_HOLO_ACCEL_RAW_TO_Q16),
        (q16_t)((int32_t)raw_data[3] * THD_HOLO_GYRO_RAW_TO_RAD_Q16),
        (q16_t)((int32_t)raw_data[4] * THD_HOLO_GYRO_RAW_TO_RAD_Q16),
        (q16_t)((int32_t)raw_data[5] * THD_HOLO_GYRO_RAW_TO_RAD_Q16),
        THD_HOLO_SAMPLE_DT_Q16);
}

void imu_holo_step(attitude_t *att)
{
    if (NULL == att) {
        return;
    }

    att->roll += (att->target_roll - att->roll) >> THD_HOLO_OUTPUT_FILTER_SHIFT;
    att->pitch += (att->target_pitch - att->pitch) >> THD_HOLO_OUTPUT_FILTER_SHIFT;
    att->yaw += (att->target_yaw - att->yaw) >> THD_HOLO_OUTPUT_FILTER_SHIFT;
}

q31_t imu_holo_angle_to_q31(q16_t radians)
{
    q16_t turns = mul_q16(radians, THD_HOLO_RAD_TO_TURN_Q16);
    return (q31_t)((int64_t)turns * 32768);
}

mat3_t build_rot_matrix(q31_t x_rad, q31_t y_rad, q31_t z_rad)
{
    mat3_t R;

    q16_t cx = arm_cos_q31(x_rad) >> 15;
    q16_t sx = arm_sin_q31(x_rad) >> 15;

    q16_t cy = arm_cos_q31(y_rad) >> 15;
    q16_t sy = arm_sin_q31(y_rad) >> 15;

    q16_t cz = arm_cos_q31(z_rad) >> 15;
    q16_t sz = arm_sin_q31(z_rad) >> 15;

    R.m[0][0] = mul_q16(cz, cy);
    R.m[0][1] = mul_q16(cz, mul_q16(sy, sx)) - mul_q16(sz, cx);
    R.m[0][2] = mul_q16(cz, mul_q16(sy, cx)) + mul_q16(sz, sx);

    R.m[1][0] = mul_q16(sz, cy);
    R.m[1][1] = mul_q16(sz, mul_q16(sy, sx)) + mul_q16(cz, cx);
    R.m[1][2] = mul_q16(sz, mul_q16(sy, cx)) - mul_q16(cz, sx);

    R.m[2][0] = -sy;
    R.m[2][1] = mul_q16(cy, sx);
    R.m[2][2] = mul_q16(cy, cx);

    return R;
}

void mahony_update(quat_t *q,
                   q16_t ax, q16_t ay, q16_t az,
                   q16_t gx, q16_t gy, q16_t gz,
                   q16_t dt)
{
    q16_t norm = sqrt_q16(
        mul_q16(ax, ax) +
        mul_q16(ay, ay) +
        mul_q16(az, az)
    );
    if (norm == 0) {
        return;
    }

    ax = div_q16(ax, norm);
    ay = div_q16(ay, norm);
    az = div_q16(az, norm);

    q16_t vx = mul_q16(2, mul_q16(q->x, q->z) - mul_q16(q->w, q->y));
    q16_t vy = mul_q16(2, mul_q16(q->w, q->x) + mul_q16(q->y, q->z));
    q16_t vz = mul_q16(q->w, q->w)
             - mul_q16(q->x, q->x)
             - mul_q16(q->y, q->y)
             + mul_q16(q->z, q->z);

    q16_t ex = mul_q16(ay, vz) - mul_q16(az, vy);
    q16_t ey = mul_q16(az, vx) - mul_q16(ax, vz);
    q16_t ez = mul_q16(ax, vy) - mul_q16(ay, vx);

    q16_t Kp = (q16_t)(2 * 65536);

    gx += mul_q16(Kp, ex);
    gy += mul_q16(Kp, ey);
    gz += mul_q16(Kp, ez);

    q16_t half_dt = dt >> 1;

    q16_t qw = q->w;
    q16_t qx = q->x;
    q16_t qy = q->y;
    q16_t qz = q->z;

    q->w += mul_q16(-qx, gx) * half_dt +
            mul_q16(-qy, gy) * half_dt +
            mul_q16(-qz, gz) * half_dt;

    q->x += mul_q16(qw, gx) * half_dt +
            mul_q16(qy, gz) * half_dt -
            mul_q16(qz, gy) * half_dt;

    q->y += mul_q16(qw, gy) * half_dt -
            mul_q16(qx, gz) * half_dt +
            mul_q16(qz, gx) * half_dt;

    q->z += mul_q16(qw, gz) * half_dt +
            mul_q16(qx, gy) * half_dt -
            mul_q16(qy, gx) * half_dt;

    norm = sqrt_q16(
        mul_q16(q->w, q->w) +
        mul_q16(q->x, q->x) +
        mul_q16(q->y, q->y) +
        mul_q16(q->z, q->z)
    );

    q->w = div_q16(q->w, norm);
    q->x = div_q16(q->x, norm);
    q->y = div_q16(q->y, norm);
    q->z = div_q16(q->z, norm);
}

mat3_t quat_to_matrix(quat_t *q)
{
    mat3_t R;

    q16_t w = q->w;
    q16_t x = q->x;
    q16_t y = q->y;
    q16_t z = q->z;

    R.m[0][0] = Q16_ONE - 2 * (mul_q16(y, y) + mul_q16(z, z));
    R.m[0][1] = 2 * (mul_q16(x, y) - mul_q16(z, w));
    R.m[0][2] = 2 * (mul_q16(x, z) + mul_q16(y, w));

    R.m[1][0] = 2 * (mul_q16(x, y) + mul_q16(z, w));
    R.m[1][1] = Q16_ONE - 2 * (mul_q16(x, x) + mul_q16(z, z));
    R.m[1][2] = 2 * (mul_q16(y, z) - mul_q16(x, w));

    R.m[2][0] = 2 * (mul_q16(x, z) - mul_q16(y, w));
    R.m[2][1] = 2 * (mul_q16(y, z) + mul_q16(x, w));
    R.m[2][2] = Q16_ONE - 2 * (mul_q16(x, x) + mul_q16(y, y));

    return R;
}

void to_screen(point_t pt, arm_2d_location_t *locate)
{
    locate->iX =
        reinterpret_s16_q16(mul_n_q16((pt.x + Q16_ONE), (CANVA_WIDTH >> 1)));
    locate->iY =
        reinterpret_s16_q16(Q16_CANVA_HEIGHT
                            - mul_n_q16((pt.y + Q16_ONE),
                                        (CANVA_HEIGHT >> 1)));
}

Thd_point_t apply_rot(Thd_point_t p, mat3_t *R)
{
    Thd_point_t out;

    out.x = mul_q16(R->m[0][0], p.x) +
            mul_q16(R->m[0][1], p.y) +
            mul_q16(R->m[0][2], p.z);

    out.y = mul_q16(R->m[1][0], p.x) +
            mul_q16(R->m[1][1], p.y) +
            mul_q16(R->m[1][2], p.z);

    out.z = mul_q16(R->m[2][0], p.x) +
            mul_q16(R->m[2][1], p.y) +
            mul_q16(R->m[2][2], p.z);

    return out;
}

point_t projection(Thd_point_t p, mat3_t *R)
{
    Thd_point_t t = apply_rot(p, R);

    q16_t z = t.z + DEPTH;
    if (z < (1 << 10)) {
        z = (1 << 10);
    }

    point_t out;
    out.x = div_q16(t.x, z);
    out.y = div_q16(t.y, z);

    return out;
}
 

/**************** test geometry ****************/

Thd_point_t cube_vertices[] = {
    {Q16(-0.5f), Q16(-0.5f), Q16(-0.5f)},
    {Q16( 0.5f), Q16(-0.5f), Q16(-0.5f)},
    {Q16( 0.5f), Q16( 0.5f), Q16(-0.5f)},
    {Q16(-0.5f), Q16( 0.5f), Q16(-0.5f)},

    {Q16(-0.5f), Q16(-0.5f), Q16( 0.5f)},
    {Q16( 0.5f), Q16(-0.5f), Q16( 0.5f)},
    {Q16( 0.5f), Q16( 0.5f), Q16( 0.5f)},
    {Q16(-0.5f), Q16( 0.5f), Q16( 0.5f)},
};

tri_t cube_tris[] = {
    {0, 1, 2}, {0, 2, 3},
    {4, 5, 6}, {4, 6, 7},
    {0, 1, 5}, {0, 5, 4},
    {2, 3, 7}, {2, 7, 6},
    {1, 2, 6}, {1, 6, 5},
    {0, 3, 7}, {0, 7, 4},
};
