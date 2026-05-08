#ifndef TUFTY_SDCARD_H
#define TUFTY_SDCARD_H

#include <stdbool.h>

#include "ff.h"

#ifdef __cplusplus
extern "C" {
#endif

FRESULT tufty_sdcard_mount(void);
void tufty_sdcard_unmount(void);
bool tufty_sdcard_write_test_file(void);
bool tufty_sdcard_read_write_test(void);
FRESULT tufty_sdcard_write_file(const char *path,
                                const void *data,
                                UINT data_size,
                                UINT *bytes_written);
FRESULT tufty_sdcard_read_file(const char *path,
                               void *buffer,
                               UINT buffer_size,
                               UINT *bytes_read);

#ifdef __cplusplus
}
#endif

#endif
