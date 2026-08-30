#ifndef __DRV_BATTERY_H__
#define __DRV_BATTERY_H__

#include <stdint.h>

#define DRV_BATTERY_ADC_MAX                 4095u
#define DRV_BATTERY_ADC_REFERENCE_MV        3300.0f
#define DRV_BATTERY_DIVIDER_RATIO           2.0f

void drv_battery_init(void);
void drv_battery_task(void);
/* 返回单位为 V 的电池电压。 */
float drv_battery_get_voltage(void);
/* 返回范围为 0.0 到 100.0 的估算电量百分比。 */
float drv_battery_get_percentage(void);

#endif // __DRV_BATTERY_H__
