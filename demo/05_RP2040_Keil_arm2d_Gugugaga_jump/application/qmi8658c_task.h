#ifndef __QMI8658C_H__
#define __QMI8658C_H__

#include <stdint.h>

#include "drv_QMI8658.h"

/**
 * Initialize the board I2C pins and start the QMI8658 IMU.
 *
 * The lower-level chip driver uses I2C_PORT, Device_Address, and the
 * iic0_read_bytes()/iic0_write_bytes() helpers from drv_QMI8658.c.
 *
 * @return 1 on success, 0 on failure.
 */
extern uint8_t qmi8658c_init(void);
/** Poll optional QMI8658 runtime events such as Tap detection. */
extern void qmi8658c_task(uint32_t now_ms);

#endif
