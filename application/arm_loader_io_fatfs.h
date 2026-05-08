#ifndef ARM_LOADER_IO_FATFS_H
#define ARM_LOADER_IO_FATFS_H

#include <stdbool.h>
#include <stdint.h>

#include "ff.h"
#include "qoi_loader.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct arm_loader_io_fatfs_t {
    const char *path;
    FIL file;
    bool is_open;
} arm_loader_io_fatfs_t;

extern const arm_loader_io_t ARM_LOADER_IO_FATFS;

arm_2d_err_t arm_loader_io_fatfs_init(arm_loader_io_fatfs_t *io,
                                      const char *path);

#ifdef __cplusplus
}
#endif

#endif
