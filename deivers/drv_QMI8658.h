/**
 * @file drv_QMI8658.h
 * @brief Register map, I2C wiring, and public API for the QMI8658 6-axis IMU on RP2040.
 *
 * Transport uses @c hardware/i2c.h with @ref I2C_PORT and @ref Device_Address. Low-level
 * @c iic0_read_bytes / @c iic0_write_bytes return 1 on success (legacy convention shared with
 * other drivers such as PAJ7620).
 */

#ifndef DRV_QMI8658_H
#define DRV_QMI8658_H
#include "hardware/i2c.h"
#include "bsp_cfg.h"
#include <stdint.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

/** 7-bit I2C slave address (AD0 pin strapping; check schematic). */
#ifndef Device_Address
#define Device_Address 0x6B
#endif
#ifndef QMI8658_ADDRESS_ALT
#define QMI8658_ADDRESS_ALT 0x6A
#endif
/** Default I2C controller instance; change if the sensor is on i2c1. */
#ifndef I2C_PORT
#define I2C_PORT i2c0
#endif
#ifndef QMI8658_I2C_BAUD_HZ
#define QMI8658_I2C_BAUD_HZ 40000u
#endif

char iic0_read_bytes(unsigned char addr,unsigned char reg, unsigned char *value,unsigned short len);
char iic0_write_bytes(unsigned char addr,unsigned char reg, unsigned char *value,unsigned short len);

#ifndef QMI8658_CTRL2_VALUE
#define QMI8658_CTRL2_VALUE 0x33u
#endif
#ifndef QMI8658_CTRL3_VALUE
#define QMI8658_CTRL3_VALUE 0x73u
#endif

/** Full-scale range used by @ref QMI8658A_ConvertData; CTRL2=0x33 selects +/-16 g. */
#ifndef ACCRANGE
#define ACCRANGE  16
#endif
/** Full-scale gyro range used by @ref QMI8658A_ConvertData; CTRL3=0x73 selects +/-2048 dps. */
#ifndef GYRRANGE
#define GYRRANGE  2048
#endif
/** Nominal ODR used in comments / tuning (Hz). */
#ifndef SAMPLERATE
#define SAMPLERATE 1000.0f
#endif
/** Still-calibration: number of stationary samples to collect. */
#define MIN_COLLECTION_COUNT 1000
/** After sorting each axis, average this many central samples (trimmed mean). */
#define USED_DATA_COUNT 50
/** |Δ|a| below this (g) counts as “stationary” for calibration collection. */
#define STATIONARY_THRESHOLD 0.001

#ifndef QMI8658_STARTUP_SELF_TEST
#define QMI8658_STARTUP_SELF_TEST 0
#endif

#ifndef QMI8658_STARTUP_COD
#define QMI8658_STARTUP_COD 0
#endif

#ifndef QMI8658_STARTUP_STILL_CALIBRATION
#define QMI8658_STARTUP_STILL_CALIBRATION 0
#endif
#ifndef QMI8658_TAP_ENABLE
#define QMI8658_TAP_ENABLE 1
#endif

#ifndef QMI8658_TAP_POLL_INTERVAL_MS
#define QMI8658_TAP_POLL_INTERVAL_MS 50u
#endif

#ifndef QMI8658_TAP_DEBUG_PRINTF
#define QMI8658_TAP_DEBUG_PRINTF 1
#endif
#ifndef QMI8658_TAP_REPORT_SINGLE
#define QMI8658_TAP_REPORT_SINGLE 0
#endif

#ifndef QMI8658_TAP_EVENT_COOLDOWN_MS
#define QMI8658_TAP_EVENT_COOLDOWN_MS 300u
#endif

#ifndef QMI8658_TAP_DEBUG_INVALID_PRINTF
#define QMI8658_TAP_DEBUG_INVALID_PRINTF 0
#endif

#ifndef QMI8658_TAP_DEFAULT_PEAK_WINDOW
#define QMI8658_TAP_DEFAULT_PEAK_WINDOW 16u
#endif

#ifndef QMI8658_TAP_DEFAULT_PRIORITY
#define QMI8658_TAP_DEFAULT_PRIORITY 0u
#endif

#ifndef QMI8658_TAP_DEFAULT_TAP_WINDOW
#define QMI8658_TAP_DEFAULT_TAP_WINDOW 50u
#endif

#ifndef QMI8658_TAP_DEFAULT_DTAP_WINDOW
#define QMI8658_TAP_DEFAULT_DTAP_WINDOW 220u
#endif

#ifndef QMI8658_TAP_DEFAULT_ALPHA
#define QMI8658_TAP_DEFAULT_ALPHA 0x02u
#endif

#ifndef QMI8658_TAP_DEFAULT_GAMMA
#define QMI8658_TAP_DEFAULT_GAMMA 0x08u
#endif

#ifndef QMI8658_TAP_DEFAULT_PEAK_MAG_THR
#define QMI8658_TAP_DEFAULT_PEAK_MAG_THR 0x0180u
#endif

#ifndef QMI8658_TAP_DEFAULT_UDM_THR
#define QMI8658_TAP_DEFAULT_UDM_THR 0x00C0u
#endif

/** Gyro bias terms applied inside @c drv_QMI8658.c after optional still calibration. */
extern float GyrCompensate[6];

#define WHO_AM_I   0x00   /* Expected product ID 0x05 (read-only). */
#define REVISION_ID  0x01 /* Silicon / firmware revision (read-only, often 0x7C). */

#define CTRL1      0x02  /* Host interface: address auto-increment, SPI/I2C, INT polarity, sync. */
#define CTRL2      0x03  /* Accelerometer ODR, range, self-test. */
#define CTRL3      0x04  /* Gyroscope ODR, range, self-test. */
#define CTRL5      0x06  /* Sensor filter / low-pass selection. */
#define CTRL7      0x08  /* Master enable for accel / gyro / sync modes. */
#define CTRL8      0x09  /* Motion / tap / pedometer related enables. */
#define CTRL9      0x0A  /* Command mailbox for factory / calibration routines. */

#define FIFO_WTM_TH      0x13  /* FIFO watermark level for interrupts. */
#define FIFO_CTRL        0x14  /* FIFO mode and enable. */
#define FIFO_SMPL_CNT    0x15  /* Samples currently stored in FIFO. */
#define FIFO_STATUS      0x16  /* FIFO overflow / watermark flags. */
#define FIFO_DATA        0x17  /* FIFO read port. */

#define STATUSINT        0x2D  /* Data-ready, lock, and CTRL9 handshake bits. */
#define STATUS1          0x2F  /* Activity / step / tap status summary. */
#define TIMESTAMP        0x30  /* Optional sample timestamp bytes. */

#define TEMP_L           0x33  /* Temperature LSB (fractional part). */
#define TEMP_H           0x34  /* Temperature MSB. */

#define A_XYZ            0x35  /* Accel XYZ little-endian (0x35–0x3A). */
#define G_XYZ            0x3B  /* Gyro XYZ little-endian (0x3B–0x40). */

#define COD_STATUS       0x46  /* Calibration-on-demand status / error code. */

#define TAP_STATUS       0x59  /* Tap engine status. */
#define STEP_COUNT       0x5A  /* Pedometer count bytes. */

#define RESET       0x60  /* Write 0xB0 to trigger soft reset. */
#define dQY_L       0x4D  /* After successful reset, this register reads 0x80 (boot ready flag). */

#define CAL1_L      0x0B
#define CAL1_H      0x0C
#define CAL2_L      0x0D
#define CAL2_H      0x0E
#define CAL3_L      0x0F
#define CAL3_H      0x10
#define CAL4_L      0x11
#define CAL4_H      0x12

/** Indices into @c commandInfos[] for CTRL9 helper tables / debug printouts. */
typedef enum {
    CTRL_CMD_ACK_ENUM,
    CTRL_CMD_RST_FIFO_ENUM,
    CTRL_CMD_REQ_FIFO_ENUM,
    CTRL_CMD_WRITE_WOM_SETTING_ENUM,
    CTRL_CMD_ACCEL_HOST_DELTA_OFFSET_ENUM,
    CTRL_CMD_GYRO_HOST_DELTA_OFFSET_ENUM,
    CTRL_CMD_CONFIGURE_TAP_ENUM,
    CTRL_CMD_CONFIGURE_PEDOMETER_ENUM,
    CTRL_CMD_CONFIGURE_MOTION_ENUM,
    CTRL_CMD_RESET_PEDOMETER_ENUM,
    CTRL_CMD_COPY_USID_ENUM,
    CTRL_CMD_SET_RPU_ENUM,
    CTRL_CMD_AHB_CLOCK_GATING_ENUM,
    CTRL_CMD_ON_DEMAND_CALIBRATION_ENUM,
    CTRL_CMD_APPLY_GYRO_GAINS_ENUM
} CommandEnum;

/** Human-readable metadata for each @c CommandEnum entry (debug / documentation). */
typedef struct {
    char commandName[50];
    int ctrl9CommandValue;
    char protocolType[10];
    char description[200];
} CommandInfo;
typedef enum {
    QMI8658_TAP_NUM_NONE = 0,
    QMI8658_TAP_NUM_SINGLE = 1,
    QMI8658_TAP_NUM_DOUBLE = 2,
} qmi8658_tap_num_t;

typedef enum {
    QMI8658_TAP_AXIS_NONE = 0,
    QMI8658_TAP_AXIS_X = 1,
    QMI8658_TAP_AXIS_Y = 2,
    QMI8658_TAP_AXIS_Z = 3,
} qmi8658_tap_axis_t;

typedef enum {
    QMI8658_TAP_POLARITY_POSITIVE = 0,
    QMI8658_TAP_POLARITY_NEGATIVE = 1,
} qmi8658_tap_polarity_t;

typedef struct {
    qmi8658_tap_num_t num;
    qmi8658_tap_axis_t axis;
    qmi8658_tap_polarity_t polarity;
    uint8_t raw_status;
} qmi8658_tap_status_t;

/** Return the static descriptor row for @p cmd (no bounds checking). */
CommandInfo getCommandInfo(CommandEnum cmd);

/** Reset, verify ID, apply default CTRL* map, optional self-test / COD / still calibration. */
int QMI8658A_Init(void);
/** Active 7-bit I2C address selected by the init-time probe. */
uint8_t QMI8658A_GetActiveAddress(void);
/** Configure the internal Tap engine with datasheet example parameters and enable it. */
int QMI8658A_EnableTapDetectionDefault(void);

/** Poll Tap status; @p status is set to NONE when no Tap event is pending. */
int QMI8658A_ReadTapStatus(qmi8658_tap_status_t *status);

/** Sample path used for bring-up: read, convert, optional logging (mostly commented in .c). */
void QMI8658A_ReadConvertAndPrint();

/**
 * @brief Collect stationary samples and compute mean offsets (written to @p OutData).
 * @note Typical use is feeding @c GyrCompensate; see @c QMI8658_STARTUP_STILL_CALIBRATION.
 */
uint8_t calibration_ACC_GYR(float *OutData);

/** Burst-read raw int16 axis data [AX,AY,AZ,GX,GY,GZ]. */
int QMI8658A_ReadData(int16_t *DATA);

/** Scale raw counts to g (axes 0–2) and dps (axes 3–5) using @p accelRange / @p gyroRange. */
void QMI8658A_ConvertData(int16_t *InData, float *OutData, int accelRange, int gyroRange);

/** Magnitude of gravity vector from three floats (g). */
float calculateAccelerationMagnitude(float *OutData);

/** Read + convert with board @ref ACCRANGE and @ref GYRRANGE. */
void QMI8658A_Get_G_DPS(float *OutData);
#endif
