#include "usb_mouse.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "tusb.h"
#include "device/dcd.h"
#include "hardware/structs/usb.h"
#include "pico/stdlib.h"
#include "usb_debug.h"
#include "perf_counter.h"
typedef struct usb_mouse_motion_t {
    int8_t x;
    int8_t y;
    int8_t wheel;
} usb_mouse_motion_t;

#ifndef USB_MOUSE_SIMULATE_GYRO
#define USB_MOUSE_SIMULATE_GYRO 0
#endif

#ifndef USB_MOUSE_IMU_DEBUG_PRINTF
#define USB_MOUSE_IMU_DEBUG_PRINTF 1
#endif

#define USB_MOUSE_IMU_CALIBRATION_COUNT     60u
#define USB_MOUSE_ACCEL_FILTER_COUNT        6u
#define USB_MOUSE_ACCEL_DEADZONE_G10000     200
#define USB_MOUSE_ACCEL_DAMPING_NUM         86
#define USB_MOUSE_ACCEL_DAMPING_DEN         100
#define USB_MOUSE_ACCEL_STILL_DECAY_NUM     72
#define USB_MOUSE_ACCEL_STILL_DECAY_DEN     100
#define USB_MOUSE_ACCEL_STILL_G10000        120
#define USB_MOUSE_ACCEL_STILL_TOL_G10000    1800
#define USB_MOUSE_GYRO_STILL_DPS100         1800
#define USB_MOUSE_GYRO_REJECT_DPS100        8000
#define USB_MOUSE_ROTATION_ACCEL_GATE_G10000 350
#define USB_MOUSE_MAX_VELOCITY_CPX          4200
#define USB_MOUSE_MAX_REPORT_DELTA_CPX      4000
#define USB_MOUSE_IMU_DEBUG_PERIOD_MS       20u
#define USB_MOUSE_REENUMERATE_DELAY_MS      250u

static usb_mouse_motion_t s_tMouseMotion;
static int32_t s_nAccelBias[2];
static int32_t s_nAccelBiasSum[2];
static int32_t s_nAccelFilterX[USB_MOUSE_ACCEL_FILTER_COUNT];
static int32_t s_nAccelFilterY[USB_MOUSE_ACCEL_FILTER_COUNT];
static int32_t s_nAccelFilterSumX;
static int32_t s_nAccelFilterSumY;
static int32_t s_nGyroBias[3];
static int32_t s_nGyroBiasSum[3];
static int32_t s_nMouseVelocityX;
static int32_t s_nMouseVelocityY;
static int32_t s_nReportRemainderX;
static int32_t s_nReportRemainderY;
static uint8_t s_chGyroCalibrationCount;
static uint8_t s_chAccelFilterIndex;
static uint8_t s_chAccelFilterCount;
#if USB_MOUSE_IMU_DEBUG_PRINTF
static uint32_t s_wLastIMUDebugMS;
#endif

#if USB_MOUSE_SIMULATE_GYRO
static int8_t const s_chSineMotion[] = {
     0,  1,  1,  2,  2,  3,  3,  4,
     4,  4,  5,  5,  5,  5,  5,  5,
     5,  5,  5,  5,  5,  4,  4,  4,
     3,  3,  2,  2,  1,  1,  0,  0,
     0, -1, -1, -2, -2, -3, -3, -4,
    -4, -4, -5, -5, -5, -5, -5, -5,
    -5, -5, -5, -5, -5, -4, -4, -4,
    -3, -3, -2, -2, -1, -1,  0,  0,
};
#endif

void usb_mouse_init(void)
{
    if (!tusb_inited() && !tusb_init()) {
        return;
    }

    tud_disconnect();
    uint32_t const start = get_system_ms();
    while ((uint32_t)(get_system_ms() - start) < USB_MOUSE_REENUMERATE_DELAY_MS) {
        tud_task();
        sleep_ms(1);
    }

    tud_connect();
    for (uint32_t i = 0; i < 20u; i++) {
        tud_task();
        sleep_ms(1);
    }
}

void usb_mouse_set_motion(int8_t x, int8_t y, int8_t wheel)
{
    s_tMouseMotion.x = x;
    s_tMouseMotion.y = y;
    s_tMouseMotion.wheel = wheel;
}

static int32_t usb_mouse_apply_deadzone_i32(int32_t value, int32_t deadzone)
{
    if (value > deadzone) {
        return value - deadzone;
    }

    if (value < -deadzone) {
        return value + deadzone;
    }

    return 0;
}

static int8_t usb_mouse_cpx_to_report_delta(int32_t value)
{
    if (value > USB_MOUSE_MAX_REPORT_DELTA_CPX) {
        value = USB_MOUSE_MAX_REPORT_DELTA_CPX;
    } else if (value < -USB_MOUSE_MAX_REPORT_DELTA_CPX) {
        value = -USB_MOUSE_MAX_REPORT_DELTA_CPX;
    }

    if (value >= 0) {
        return (int8_t)((value + 50) / 100);
    }

    return (int8_t)((value - 50) / 100);
}

static int8_t usb_mouse_velocity_to_report_delta(int32_t velocity, int32_t *remainder)
{
    int8_t report;
    int32_t combined = velocity + *remainder;

    report = usb_mouse_cpx_to_report_delta(combined);
    *remainder = combined - ((int32_t)report * 100);

    return report;
}

static int32_t usb_mouse_limit_i32(int32_t value, int32_t limit)
{
    if (value > limit) {
        return limit;
    }

    if (value < -limit) {
        return -limit;
    }

    return value;
}

static int32_t usb_mouse_abs_i32(int32_t value)
{
    return (value < 0) ? -value : value;
}

#if USB_MOUSE_IMU_DEBUG_PRINTF
static void usb_mouse_debug_print_imu(char const *state,
                                      int32_t ax, int32_t ay, int32_t az,
                                      int32_t gx, int32_t gy, int32_t gz,
                                      int32_t filtered_ax, int32_t filtered_ay,
                                      int32_t vx, int32_t vy)
{
    uint32_t const now = get_system_ms();

    if ((uint32_t)(now - s_wLastIMUDebugMS) < USB_MOUSE_IMU_DEBUG_PERIOD_MS) {
        return;
    }
    s_wLastIMUDebugMS = now;

    printf("IMU %s ax=%ld ay=%ld az=%ld gx=%ld gy=%ld gz=%ld fax=%ld fay=%ld vx=%ld vy=%ld bx=%ld by=%ld\r\n",
           state,
           (long)ax,
           (long)ay,
           (long)az,
           (long)gx,
           (long)gy,
           (long)gz,
           (long)filtered_ax,
           (long)filtered_ay,
           (long)vx,
           (long)vy,
           (long)s_nAccelBias[0],
           (long)s_nAccelBias[1]);
}
#endif

static int32_t usb_mouse_filter_accel_x(int32_t value)
{
    uint8_t const count = (s_chAccelFilterCount < USB_MOUSE_ACCEL_FILTER_COUNT) ?
                          (s_chAccelFilterCount + 1u) : s_chAccelFilterCount;

    s_nAccelFilterSumX -= s_nAccelFilterX[s_chAccelFilterIndex];
    s_nAccelFilterX[s_chAccelFilterIndex] = value;
    s_nAccelFilterSumX += value;

    return s_nAccelFilterSumX / count;
}

static int32_t usb_mouse_filter_accel_y(int32_t value)
{
    s_nAccelFilterSumY -= s_nAccelFilterY[s_chAccelFilterIndex];
    s_nAccelFilterY[s_chAccelFilterIndex] = value;
    s_nAccelFilterSumY += value;

    s_chAccelFilterIndex++;
    if (s_chAccelFilterIndex >= USB_MOUSE_ACCEL_FILTER_COUNT) {
        s_chAccelFilterIndex = 0;
    }

    if (s_chAccelFilterCount < USB_MOUSE_ACCEL_FILTER_COUNT) {
        s_chAccelFilterCount++;
    }

    return s_nAccelFilterSumY / s_chAccelFilterCount;
}

static int32_t usb_mouse_raw_accel_to_g10000(int16_t raw)
{
    return ((int32_t)raw * 5000) / 1024;
}

static int32_t usb_mouse_raw_gyro_to_dps100(int16_t raw)
{
    return ((int32_t)raw * 25) / 4;
}

void usb_mouse_update_imu_raw(int16_t const raw_data[6])
{
    int32_t const ax = usb_mouse_raw_accel_to_g10000(raw_data[0]);
    int32_t const ay = usb_mouse_raw_accel_to_g10000(raw_data[1]);
    int32_t const az = usb_mouse_raw_accel_to_g10000(raw_data[2]);
    int32_t const gx = usb_mouse_raw_gyro_to_dps100(raw_data[3]);
    int32_t const gy = usb_mouse_raw_gyro_to_dps100(raw_data[4]);
    int32_t const gz = usb_mouse_raw_gyro_to_dps100(raw_data[5]);
    int64_t const acc_mag2 = (int64_t)ax * ax + (int64_t)ay * ay + (int64_t)az * az;
    int32_t const still_low = 10000 - USB_MOUSE_ACCEL_STILL_TOL_G10000;
    int32_t const still_high = 10000 + USB_MOUSE_ACCEL_STILL_TOL_G10000;
    bool const mostly_still = (acc_mag2 > (int64_t)still_low * still_low) &&
                              (acc_mag2 < (int64_t)still_high * still_high);

    if (s_chGyroCalibrationCount < USB_MOUSE_IMU_CALIBRATION_COUNT) {
        if (mostly_still) {
            s_nAccelBiasSum[0] += ax;
            s_nAccelBiasSum[1] += ay;
            s_nGyroBiasSum[0] += gx;
            s_nGyroBiasSum[1] += gy;
            s_nGyroBiasSum[2] += gz;
            s_chGyroCalibrationCount++;

            if (s_chGyroCalibrationCount == USB_MOUSE_IMU_CALIBRATION_COUNT) {
                s_nAccelBias[0] = s_nAccelBiasSum[0] / USB_MOUSE_IMU_CALIBRATION_COUNT;
                s_nAccelBias[1] = s_nAccelBiasSum[1] / USB_MOUSE_IMU_CALIBRATION_COUNT;
                s_nGyroBias[0] = s_nGyroBiasSum[0] / USB_MOUSE_IMU_CALIBRATION_COUNT;
                s_nGyroBias[1] = s_nGyroBiasSum[1] / USB_MOUSE_IMU_CALIBRATION_COUNT;
                s_nGyroBias[2] = s_nGyroBiasSum[2] / USB_MOUSE_IMU_CALIBRATION_COUNT;
            }
        }

        s_nMouseVelocityX = 0;
        s_nMouseVelocityY = 0;
#if USB_MOUSE_IMU_DEBUG_PRINTF
        usb_mouse_debug_print_imu("CAL", ax, ay, az, gx, gy, gz,
                                  0, 0,
                                  s_nMouseVelocityX, s_nMouseVelocityY);
#endif
        return;
    }

    int32_t const corrected_ax = usb_mouse_apply_deadzone_i32(ax - s_nAccelBias[0],
                                                              USB_MOUSE_ACCEL_DEADZONE_G10000);
    int32_t const corrected_ay = usb_mouse_apply_deadzone_i32(ay - s_nAccelBias[1],
                                                              USB_MOUSE_ACCEL_DEADZONE_G10000);
    int32_t const corrected_gx = gx - s_nGyroBias[0];
    int32_t const corrected_gy = gy - s_nGyroBias[1];
    int32_t const corrected_gz = gz - s_nGyroBias[2];
    int64_t const gyro_mag2 = (int64_t)corrected_gx * corrected_gx +
                              (int64_t)corrected_gy * corrected_gy +
                              (int64_t)corrected_gz * corrected_gz;
    int64_t const plane_accel2 = (int64_t)corrected_ax * corrected_ax +
                                 (int64_t)corrected_ay * corrected_ay;
    bool const rotating = (gyro_mag2 > (int64_t)USB_MOUSE_GYRO_REJECT_DPS100 *
                                       USB_MOUSE_GYRO_REJECT_DPS100) &&
                          (plane_accel2 < (int64_t)USB_MOUSE_ROTATION_ACCEL_GATE_G10000 *
                                          USB_MOUSE_ROTATION_ACCEL_GATE_G10000);

    if (rotating) {
        s_nMouseVelocityX = (s_nMouseVelocityX * USB_MOUSE_ACCEL_STILL_DECAY_NUM) /
                            USB_MOUSE_ACCEL_STILL_DECAY_DEN;
        s_nMouseVelocityY = (s_nMouseVelocityY * USB_MOUSE_ACCEL_STILL_DECAY_NUM) /
                            USB_MOUSE_ACCEL_STILL_DECAY_DEN;
        s_nReportRemainderX = 0;
        s_nReportRemainderY = 0;
#if USB_MOUSE_IMU_DEBUG_PRINTF
        usb_mouse_debug_print_imu("ROT", ax, ay, az, gx, gy, gz,
                                  0, 0,
                                  s_nMouseVelocityX, s_nMouseVelocityY);
#endif
        return;
    }

    int32_t const filtered_ax = usb_mouse_filter_accel_x(corrected_ax);
    int32_t const filtered_ay = usb_mouse_filter_accel_y(corrected_ay);
    bool const imu_still = (usb_mouse_abs_i32(filtered_ax) < USB_MOUSE_ACCEL_STILL_G10000) &&
                           (usb_mouse_abs_i32(filtered_ay) < USB_MOUSE_ACCEL_STILL_G10000) &&
                           (gyro_mag2 < (int64_t)USB_MOUSE_GYRO_STILL_DPS100 *
                                       USB_MOUSE_GYRO_STILL_DPS100);

    s_nMouseVelocityX += filtered_ax / 10;
    s_nMouseVelocityY += -filtered_ay / 10;

    s_nMouseVelocityX = (s_nMouseVelocityX * USB_MOUSE_ACCEL_DAMPING_NUM) /
                        USB_MOUSE_ACCEL_DAMPING_DEN;
    s_nMouseVelocityY = (s_nMouseVelocityY * USB_MOUSE_ACCEL_DAMPING_NUM) /
                        USB_MOUSE_ACCEL_DAMPING_DEN;

    if (imu_still) {
        s_nMouseVelocityX = (s_nMouseVelocityX * USB_MOUSE_ACCEL_STILL_DECAY_NUM) /
                            USB_MOUSE_ACCEL_STILL_DECAY_DEN;
        s_nMouseVelocityY = (s_nMouseVelocityY * USB_MOUSE_ACCEL_STILL_DECAY_NUM) /
                            USB_MOUSE_ACCEL_STILL_DECAY_DEN;

        if ((s_nMouseVelocityX > -35) && (s_nMouseVelocityX < 35)) {
            s_nMouseVelocityX = 0;
        }

        if ((s_nMouseVelocityY > -35) && (s_nMouseVelocityY < 35)) {
            s_nMouseVelocityY = 0;
        }
    }

    s_nMouseVelocityX = usb_mouse_limit_i32(s_nMouseVelocityX, USB_MOUSE_MAX_VELOCITY_CPX);
    s_nMouseVelocityY = usb_mouse_limit_i32(s_nMouseVelocityY, USB_MOUSE_MAX_VELOCITY_CPX);

#if USB_MOUSE_IMU_DEBUG_PRINTF
    usb_mouse_debug_print_imu(imu_still ? "STL" : "MOV",
                              ax, ay, az, gx, gy, gz,
                              filtered_ax, filtered_ay,
                              s_nMouseVelocityX, s_nMouseVelocityY);
#endif
}

void usb_mouse_task(void)
{
    static uint32_t s_wLastReportMS = 0;
#if USB_MOUSE_SIMULATE_GYRO
    static uint8_t s_chSineIndex = 0;
#endif

    tud_task();

    if (!tud_hid_ready()) {
        return;
    }

    uint32_t wNow = get_system_ms();
    if ((uint32_t)(wNow - s_wLastReportMS) < 10) {
        return;
    }
    s_wLastReportMS = wNow;

#if USB_MOUSE_SIMULATE_GYRO
    s_tMouseMotion.x = s_chSineMotion[s_chSineIndex];
    s_tMouseMotion.y =
        s_chSineMotion[(s_chSineIndex + (sizeof(s_chSineMotion) / sizeof(s_chSineMotion[0])) / 4) %
                       (sizeof(s_chSineMotion) / sizeof(s_chSineMotion[0]))];
    s_tMouseMotion.wheel = 0;
    s_chSineIndex++;
    if (s_chSineIndex >= (sizeof(s_chSineMotion) / sizeof(s_chSineMotion[0]))) {
        s_chSineIndex = 0;
    }
#else
    s_tMouseMotion.x = usb_mouse_velocity_to_report_delta(s_nMouseVelocityX,
                                                          &s_nReportRemainderX);
    s_tMouseMotion.y = usb_mouse_velocity_to_report_delta(s_nMouseVelocityY,
                                                          &s_nReportRemainderY);
    s_tMouseMotion.wheel = 0;
#endif

    tud_hid_mouse_report(0, 0, s_tMouseMotion.x, s_tMouseMotion.y,
                         s_tMouseMotion.wheel, 0);

    s_tMouseMotion.x = 0;
    s_tMouseMotion.y = 0;
    s_tMouseMotion.wheel = 0;
}

void USBCTRL_IRQ_Handler(void)
{
    uint32_t const wInts = usb_hw->ints;

    if (wInts == 0) {
        return;
    }

    g_tUSBDebugState.irq_count++;
    g_tUSBDebugState.last_ints = wInts;
    g_tUSBDebugState.last_sie_status = usb_hw->sie_status;
    g_tUSBDebugState.last_buf_status = usb_hw->buf_status;
    g_tUSBDebugState.last_main_ctrl = usb_hw->main_ctrl;
    g_tUSBDebugState.last_sie_ctrl = usb_hw->sie_ctrl;
    g_tUSBDebugState.last_muxing = usb_hw->muxing;
    g_tUSBDebugState.last_ep0_in_buf_ctrl = usb_dpram->ep_buf_ctrl[0].in;
    g_tUSBDebugState.last_ep0_out_buf_ctrl = usb_dpram->ep_buf_ctrl[0].out;
    g_tUSBDebugState.last_ep1_in_ctrl = usb_dpram->ep_ctrl[0].in;
    g_tUSBDebugState.last_ep1_out_ctrl = usb_dpram->ep_ctrl[0].out;
    g_tUSBDebugState.last_ep1_in_buf_ctrl = usb_dpram->ep_buf_ctrl[1].in;
    g_tUSBDebugState.last_ep1_out_buf_ctrl = usb_dpram->ep_buf_ctrl[1].out;

    dcd_int_handler(0);
}

void tud_mount_cb(void)
{
    g_tUSBDebugState.mount_count++;
    USB_DEBUG_PRINTF("USB MOUNT count=%lu\r\n",
                     (unsigned long)g_tUSBDebugState.mount_count);
}

void tud_umount_cb(void)
{
    g_tUSBDebugState.unmount_count++;
    USB_DEBUG_PRINTF("USB UNMOUNT count=%lu\r\n",
                     (unsigned long)g_tUSBDebugState.unmount_count);
}

void tud_suspend_cb(bool remote_wakeup_en)
{
    (void)remote_wakeup_en;
    g_tUSBDebugState.suspend_count++;
    USB_DEBUG_PRINTF("USB TUD_SUSPEND count=%lu\r\n",
                     (unsigned long)g_tUSBDebugState.suspend_count);
}

void tud_resume_cb(void)
{
    g_tUSBDebugState.resume_count++;
    USB_DEBUG_PRINTF("USB TUD_RESUME count=%lu\r\n",
                     (unsigned long)g_tUSBDebugState.resume_count);
}

uint16_t tud_hid_get_report_cb(uint8_t instance,
                               uint8_t report_id,
                               hid_report_type_t report_type,
                               uint8_t *buffer,
                               uint16_t reqlen)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)reqlen;

    return 0;
}

void tud_hid_set_report_cb(uint8_t instance,
                           uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const *buffer,
                           uint16_t bufsize)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)bufsize;
}
