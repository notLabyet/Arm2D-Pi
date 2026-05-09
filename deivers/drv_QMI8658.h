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
#include <stdint.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

/** 7-bit I2C slave address (AD0 pin strapping; check schematic). */
#define Device_Address 0x6B
/** Default I2C controller instance; change if the sensor is on i2c1. */
#define I2C_PORT i2c0

char iic0_read_bytes(unsigned char addr,unsigned char reg, unsigned char *value,unsigned short len);
char iic0_write_bytes(unsigned char addr,unsigned char reg, unsigned char *value,unsigned short len);

/** Full-scale range used by @ref QMI8658A_ConvertData when reading via @ref QMI8658A_Get_G_DPS (g). */
#define ACCRANGE  16
/** Full-scale gyro range (dps) paired with @ref ACCRANGE in conversion helpers. */
#define GYRRANGE  2048
/** Nominal ODR used in comments / tuning (Hz). */
#define SAMPLERATE 800.0f
/** Still-calibration: number of stationary samples to collect. */
#define MIN_COLLECTION_COUNT 1000
/** After sorting each axis, average this many central samples (trimmed mean). */
#define USED_DATA_COUNT 50
/** |忖|a| below this (g) counts as ※stationary§ for calibration collection. */
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

#define A_XYZ            0x35  /* Accel XYZ little-endian (0x35每0x3A). */
#define G_XYZ            0x3B  /* Gyro XYZ little-endian (0x3B每0x40). */

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

/** Return the static descriptor row for @p cmd (no bounds checking). */
CommandInfo getCommandInfo(CommandEnum cmd);

/** Reset, verify ID, apply default CTRL* map, optional self-test / COD / still calibration. */
int QMI8658A_Init(void);

/** Sample path used for bring-up: read, convert, optional logging (mostly commented in .c). */
void QMI8658A_ReadConvertAndPrint();

/**
 * @brief Collect stationary samples and compute mean offsets (written to @p OutData).
 * @note Typical use is feeding @c GyrCompensate; see @c QMI8658_STARTUP_STILL_CALIBRATION.
 */
uint8_t calibration_ACC_GYR(float *OutData);

/** Burst-read raw int16 axis data [AX,AY,AZ,GX,GY,GZ]. */
int QMI8658A_ReadData(int16_t *DATA);

/** Scale raw counts to g (axes 0每2) and dps (axes 3每5) using @p accelRange / @p gyroRange. */
void QMI8658A_ConvertData(int16_t *InData, float *OutData, int accelRange, int gyroRange);

/** Magnitude of gravity vector from three floats (g). */
float calculateAccelerationMagnitude(float *OutData);

/** Read + convert with board @ref ACCRANGE and @ref GYRRANGE. */
void QMI8658A_Get_G_DPS(float *OutData);
#endif
