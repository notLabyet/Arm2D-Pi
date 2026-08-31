#ifndef __SERVICE_SENSOR_H__
#define __SERVICE_SENSOR_H__

#include <stdint.h>

typedef struct
{
    float battery_voltage;
    float battery_percentage;
} service_sensor_t;

void service_sensor_init(void);
void service_sensor_task(void);

extern service_sensor_t g_service_sensor;

#endif // __SERVICE_SENSOR_H__
