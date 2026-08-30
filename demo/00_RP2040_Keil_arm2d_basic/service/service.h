#ifndef _SERVICE_H_
#define _SERVICE_H_

#include "service_buzzer.h"
#include "service_key.h"
#include "service_sensor.h"

void service_init(void);
void service_task(void);

#endif // _SERVICE_H_