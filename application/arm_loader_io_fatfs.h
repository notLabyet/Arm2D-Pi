#ifndef ARM_LOADER_IO_FATFS_H
#define ARM_LOADER_IO_FATFS_H

#ifndef ARM_LOADER_IO_FATFS_CACHE_SIZE
#   define ARM_LOADER_IO_FATFS_CACHE_SIZE   2*1024u
#endif

#ifndef ARM_LOADER_IO_FATFS_CACHE_WAYS
#   define ARM_LOADER_IO_FATFS_CACHE_WAYS   2u
#endif

#ifndef ARM_LOADER_IO_FATFS_USE_CACHE
#   ifdef NULL
#       pragma push_macro("NULL")
#       undef NULL
#       define ARM_LOADER_IO_FATFS_RESTORE_NULL
#   endif
#   if (0 == ARM_LOADER_IO_FATFS_CACHE_SIZE) || (0 == ARM_LOADER_IO_FATFS_CACHE_WAYS)
#       define ARM_LOADER_IO_FATFS_USE_CACHE    0
#   else
#       define ARM_LOADER_IO_FATFS_USE_CACHE    1
#   endif
#   ifdef ARM_LOADER_IO_FATFS_RESTORE_NULL
#       pragma pop_macro("NULL")
#       undef ARM_LOADER_IO_FATFS_RESTORE_NULL
#   endif
#endif

#include <stdbool.h>
#include <stdint.h>

#include "ff.h"
#include "__arm_2d_loader_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#if ARM_LOADER_IO_FATFS_USE_CACHE
typedef struct arm_loader_io_fatfs_cache_t {
    uint8_t buffer[ARM_LOADER_IO_FATFS_CACHE_SIZE];
    FSIZE_t start;
    UINT size;
    uint32_t age;
    bool valid;
} arm_loader_io_fatfs_cache_t;
#endif

typedef struct arm_loader_io_fatfs_t {
    const char *path;
    FIL file;
    FSIZE_t file_size;
    FSIZE_t position;
#if ARM_LOADER_IO_FATFS_USE_CACHE
    uint32_t cache_age;
    arm_loader_io_fatfs_cache_t cache[ARM_LOADER_IO_FATFS_CACHE_WAYS];
#endif
    bool is_open;
} arm_loader_io_fatfs_t;

extern const arm_loader_io_t ARM_LOADER_IO_FATFS;

arm_2d_err_t arm_loader_io_fatfs_init(arm_loader_io_fatfs_t *io,
                                      const char *path);

#ifdef __cplusplus
}
#endif

#endif
