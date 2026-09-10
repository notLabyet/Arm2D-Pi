#ifndef BUZZER_TASK_H
#define BUZZER_TASK_H

#include <stdint.h>

#ifndef BUZZER_TASK_REPEAT_PAUSE_MS
#   define BUZZER_TASK_REPEAT_PAUSE_MS     900u
#endif

#ifndef BUZZER_TASK_ENABLE_PRINTF
#   define BUZZER_TASK_ENABLE_PRINTF       1
#endif

void buzzer_task_init(void);
void buzzer_task(uint32_t wPeriodMS);

#endif
