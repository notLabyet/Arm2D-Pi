#ifndef IR_TASK_H
#define IR_TASK_H

#include <stdint.h>

#ifndef IR_TASK_SEND_INTERVAL_MS
#   define IR_TASK_SEND_INTERVAL_MS    1000u
#endif

#ifndef IR_TASK_RESULT_DELAY_MS
#   define IR_TASK_RESULT_DELAY_MS     60u
#endif

void ir_task_init(void);

/*
 * Periodic loopback task. Pass the desired send interval in milliseconds.
 * The function is non-blocking, but should be called frequently from the main
 * loop so drv_ir_send_task() can keep the IR burst timing accurate.
 */
void ir_task(uint32_t wPeriodMS);

#endif
