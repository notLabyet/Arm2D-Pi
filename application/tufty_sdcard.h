#ifndef TUFTY_SDCARD_H
#define TUFTY_SDCARD_H

#include <stdbool.h>
#include <stdint.h>

#include "ff.h"

#ifdef __cplusplus
extern "C" {
#endif

FRESULT tufty_sdcard_mount(void);
void tufty_sdcard_unmount(void);
bool tufty_sdcard_is_mounted(void);
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

typedef struct tufty_sdcard_perf_result_t {
    uint32_t file_size_bytes;
    uint32_t chunk_size_bytes;
    uint32_t write_time_ms;
    uint32_t read_time_ms;
    uint32_t verify_time_ms;
    uint32_t write_kib_per_s;
    uint32_t read_kib_per_s;
    uint32_t write_errors;
    uint32_t read_errors;
    uint32_t short_writes;
    uint32_t short_reads;
    uint32_t write_retries;
    uint32_t read_retries;
    uint32_t verify_errors;
    uint32_t transfer_errors;
    uint32_t last_sdio_error_code;
    uint32_t last_sdio_error_line;
    uint32_t sync_time_ms;
    FRESULT last_error;
    bool passed;
} tufty_sdcard_perf_result_t;

bool tufty_sdcard_perf_test(uint32_t file_size_bytes,
                            uint32_t chunk_size_bytes,
                            tufty_sdcard_perf_result_t *result);
bool tufty_sdcard_default_perf_test(void);

#ifdef __cplusplus
}
#endif

#endif
