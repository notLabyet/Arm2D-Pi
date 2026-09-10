#ifndef LIGHT_TASK_H
#define LIGHT_TASK_H

#include <stdint.h>

#ifndef LIGHT_TASK_INTERVAL_MS
#   define LIGHT_TASK_INTERVAL_MS     500u
#endif

void light_task_init(void);
void light_task(uint32_t wPeriodMS);

#endif
