#ifndef ARM_LOADER_IO_FATFS_H
#define ARM_LOADER_IO_FATFS_H

/*
 * Optional read cache for Arm-2D resource streaming.
 *
 * QOI/LMSK decoders often seek and read in small pieces. A small set-associative
 * cache reduces repeated FatFs f_lseek()/f_read() calls while keeping RAM use
 * predictable. Set cache size or ways to 0 to compile the direct-read path.
 */
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
/** One cached file window used by the FatFs Arm-2D loader adapter. */
typedef struct arm_loader_io_fatfs_cache_t {
    uint8_t buffer[ARM_LOADER_IO_FATFS_CACHE_SIZE]; /**< Cached file bytes. */
    FSIZE_t start;                                  /**< File offset of buffer[0]. */
    UINT size;                                      /**< Valid bytes in buffer. */
    uint32_t age;                                   /**< LRU counter. */
    bool valid;                                     /**< True when this way is usable. */
} arm_loader_io_fatfs_cache_t;
#endif

/** Per-file state passed to ARM_LOADER_IO_FATFS as the target pointer. */
typedef struct arm_loader_io_fatfs_t {
    const char *path;       /**< FatFs path opened by the loader. */
    FIL file;               /**< FatFs file object. */
    FSIZE_t file_size;      /**< File size captured at open time. */
    FSIZE_t position;       /**< Logical read position tracked by the adapter. */
#if ARM_LOADER_IO_FATFS_USE_CACHE
    uint32_t cache_age;     /**< Monotonic LRU age counter. */
    arm_loader_io_fatfs_cache_t cache[ARM_LOADER_IO_FATFS_CACHE_WAYS]; /**< Cache ways. */
#endif
    bool is_open;           /**< True between fnOpen and fnClose. */
} arm_loader_io_fatfs_t;

/** Arm-2D loader callback table backed by FatFs files on the SD card. */
extern const arm_loader_io_t ARM_LOADER_IO_FATFS;

/**
 * Prepare an Arm-2D FatFs loader target.
 *
 * After this call, pass the returned object address as the loader target and
 * pass ARM_LOADER_IO_FATFS as the I/O implementation.
 */
arm_2d_err_t arm_loader_io_fatfs_init(arm_loader_io_fatfs_t *io,
                                      const char *path);

#ifdef __cplusplus
}
#endif

#endif
