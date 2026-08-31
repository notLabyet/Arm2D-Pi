#ifndef QMI8658C_TASK_H
#define QMI8658C_TASK_H

#include <stdbool.h>
#include <stdint.h>

bool qmi8658c_init(void);
void qmi8658c_task(uint32_t wNowMS);
bool qmi8658c_read(float fData[6]);

#endif
