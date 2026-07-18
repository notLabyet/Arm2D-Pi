#ifndef RP2040_SDCARD_H
#define RP2040_SDCARD_H

#include <stdbool.h>
#include <stdint.h>

#include "ff.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Mount the first FatFs volume.
 *
 * The function is idempotent: calling it again after a successful mount returns
 * FR_OK immediately. On failure it prints extra SD/partition diagnostics.
 */
FRESULT rp2040_sdcard_mount(void);

/** Unmount the FatFs volume if it is currently mounted. */
void rp2040_sdcard_unmount(void);

/** Return true after a successful rp2040_sdcard_mount(). */
bool rp2040_sdcard_is_mounted(void);

/** Append a short text line to sdio_test.txt for a quick write smoke test. */
bool rp2040_sdcard_write_test_file(void);

/** Run a small read/write/verify test file check. */
bool rp2040_sdcard_read_write_test(void);

/** Write a complete memory buffer to a FatFs path. */
FRESULT rp2040_sdcard_write_file(const char *path,
                                const void *data,
                                UINT data_size,
                                UINT *bytes_written);

/** Read up to buffer_size bytes from a FatFs path. */
FRESULT rp2040_sdcard_read_file(const char *path,
                               void *buffer,
                               UINT buffer_size,
                               UINT *bytes_read);

/** Detailed statistics returned by rp2040_sdcard_perf_test(). */
typedef struct rp2040_sdcard_perf_result_t {
    uint32_t file_size_bytes;       /**< Total test file size. */
    uint32_t chunk_size_bytes;      /**< Transfer chunk size. */
    uint32_t write_time_ms;         /**< Time spent writing file data. */
    uint32_t read_time_ms;          /**< Time spent reading file data. */
    uint32_t verify_time_ms;        /**< Time spent comparing readback data. */
    uint32_t write_kib_per_s;       /**< Approximate write throughput. */
    uint32_t read_kib_per_s;        /**< Approximate read throughput. */
    uint32_t write_errors;          /**< FatFs write call failures. */
    uint32_t read_errors;           /**< FatFs read call failures. */
    uint32_t short_writes;          /**< Writes that returned fewer bytes. */
    uint32_t short_reads;           /**< Reads that returned fewer bytes. */
    uint32_t write_retries;         /**< Retry attempts used while writing. */
    uint32_t read_retries;          /**< Retry attempts used while reading. */
    uint32_t verify_errors;         /**< Byte mismatches during verification. */
    uint32_t transfer_errors;       /**< Combined non-verify transfer errors. */
    uint32_t last_sdio_error_code;  /**< Low-level SDIO error code snapshot. */
    uint32_t last_sdio_error_line;  /**< Source line for low-level SDIO error. */
    uint32_t sync_time_ms;          /**< Time spent in final f_sync/f_close. */
    FRESULT last_error;             /**< Last FatFs error seen by the test. */
    bool passed;                    /**< True if the whole performance test passed. */
} rp2040_sdcard_perf_result_t;

/** Create, read back, and verify a test file of the requested size. */
bool rp2040_sdcard_perf_test(uint32_t file_size_bytes,
                            uint32_t chunk_size_bytes,
                            rp2040_sdcard_perf_result_t *result);

/** Run the default 12 MiB / 8 KiB chunk performance test. */
bool rp2040_sdcard_default_perf_test(void);

#ifdef __cplusplus
}
#endif

#endif
