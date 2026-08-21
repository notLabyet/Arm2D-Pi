#include "qmi8658c_task.h"

#include <stddef.h>

#include "bsp_cfg.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"

#include "drv_QMI8658.h"

static bool s_bImuReady;

bool qmi8658c_init(void)
{
    i2c_init(I2C_PORT, QMI8658_I2C_BAUD_HZ);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    s_bImuReady = QMI8658A_Init() != 0;
    return s_bImuReady;
}

void qmi8658c_task(uint32_t wNowMS)
{
    (void)wNowMS;
}

bool qmi8658c_read(float fData[6])
{
    int16_t hwRawData[6];

    if ((NULL == fData) || !s_bImuReady || !QMI8658A_ReadData(hwRawData)) {
        return false;
    }

    QMI8658A_ConvertData(hwRawData, fData, ACCRANGE, GYRRANGE);
    return true;
}
