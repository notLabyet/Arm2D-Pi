#include "service_sensor.h"
#include "drv_battery.h"

service_sensor_t g_service_sensor;

void service_sensor_init(void)
{
    drv_battery_init();
}

void service_sensor_task(void)
{
    drv_battery_task();
    g_service_sensor.battery_voltage = drv_battery_get_voltage();
    g_service_sensor.battery_percentage = drv_battery_get_percentage();
}
