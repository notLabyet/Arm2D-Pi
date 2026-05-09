#ifndef __BM8563_TASK_H__
#define __BM8563_TASK_H__



#include "bm8563.h"

/** Global BM8563 instance wired to the board I2C callbacks. */
extern bm8563_t tbm8563;

/**
 * Initialize the BM8563 RTC wrapper and write the default startup time.
 *
 * The current implementation uses iic0_read_bytes()/iic0_write_bytes() from the
 * shared I2C layer. Change the default struct tm in bm8563_task.c if you do not
 * want the RTC to be overwritten at boot.
 */
extern char bm8563_hander_init();




















#endif
