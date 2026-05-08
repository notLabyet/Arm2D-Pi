#ifndef ARM_LOADER_IO_FATFS_H
#define ARM_LOADER_IO_FATFS_H

#include <stdbool.h>
#include <stdint.h>

#include "ff.h"
#include "__arm_2d_loader_common.h"

#ifndef ARM_LOADER_IO_FATFS_CACHE_SIZE
#   define ARM_LOADER_IO_FATFS_CACHE_SIZE   2048u
#endif

#ifndef ARM_LOADER_IO_FATFS_CACHE_WAYS
#   define ARM_LOADER_IO_FATFS_CACHE_WAYS   2u
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct arm_loader_io_fatfs_cache_t {
    uint8_t buffer[ARM_LOADER_IO_FATFS_CACHE_SIZE];
    FSIZE_t start;
    UINT size;
    uint32_t age;
    bool valid;
} arm_loader_io_fatfs_cache_t;

typedef struct arm_loader_io_fatfs_t {
    const char *path;
    FIL file;
    FSIZE_t file_size;
    FSIZE_t position;
    uint32_t cache_age;
    arm_loader_io_fatfs_cache_t cache[ARM_LOADER_IO_FATFS_CACHE_WAYS];
    bool is_open;
} arm_loader_io_fatfs_t;

extern const arm_loader_io_t ARM_LOADER_IO_FATFS;

arm_2d_err_t arm_loader_io_fatfs_init(arm_loader_io_fatfs_t *io,
                                      const char *path);

#ifdef __cplusplus
}
#endif

#endif
