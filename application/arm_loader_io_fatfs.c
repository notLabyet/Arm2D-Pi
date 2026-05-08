#include "arm_loader_io_fatfs.h"

#include <stdio.h>
#include <string.h>

#include "f_util.h"
#include "tufty_sdcard.h"

static bool fatfs_io_open(uintptr_t target, void *loader);
static void fatfs_io_close(uintptr_t target, void *loader);
static bool fatfs_io_seek(uintptr_t target, void *loader, int32_t offset, int32_t whence);
static intptr_t fatfs_io_tell(uintptr_t target, void *loader);
static size_t fatfs_io_read(uintptr_t target, void *loader, uint8_t *buffer, size_t size);

const arm_loader_io_t ARM_LOADER_IO_FATFS = {
    .fnOpen = fatfs_io_open,
    .fnClose = fatfs_io_close,
    .fnSeek = fatfs_io_seek,
    .fnGetPosition = fatfs_io_tell,
    .fnRead = fatfs_io_read,
};

arm_2d_err_t arm_loader_io_fatfs_init(arm_loader_io_fatfs_t *io,
                                      const char *path)
{
    if ((NULL == io) || (NULL == path)) {
        return ARM_2D_ERR_INVALID_PARAM;
    }

    memset(io, 0, sizeof(*io));
    io->path = path;

    return ARM_2D_ERR_NONE;
}

static bool fatfs_io_open(uintptr_t target, void *loader)
{
    arm_loader_io_fatfs_t *io = (arm_loader_io_fatfs_t *)target;
    FRESULT fr;

    (void)loader;

    if ((NULL == io) || (NULL == io->path)) {
        return false;
    }

    if (io->is_open) {
        (void)f_close(&io->file);
        io->is_open = false;
    }

    fr = tufty_sdcard_mount();
    if (FR_OK != fr) {
        printf("QOI SD mount failed: %s (%d)\r\n", FRESULT_str(fr), fr);
        return false;
    }

    fr = f_open(&io->file, io->path, FA_READ);
    if (FR_OK != fr) {
        printf("QOI open %s failed: %s (%d)\r\n", io->path, FRESULT_str(fr), fr);
        tufty_sdcard_unmount();
        return false;
    }

    io->is_open = true;
    return true;
}

static void fatfs_io_close(uintptr_t target, void *loader)
{
    arm_loader_io_fatfs_t *io = (arm_loader_io_fatfs_t *)target;

    (void)loader;

    if ((NULL != io) && io->is_open) {
        (void)f_close(&io->file);
        io->is_open = false;
    }
}

static bool fatfs_io_seek(uintptr_t target, void *loader, int32_t offset, int32_t whence)
{
    arm_loader_io_fatfs_t *io = (arm_loader_io_fatfs_t *)target;
    FSIZE_t base = 0;
    FSIZE_t pos;

    (void)loader;

    if ((NULL == io) || !io->is_open) {
        return false;
    }

    switch (whence) {
        case SEEK_SET:
            base = 0;
            break;

        case SEEK_CUR:
            base = f_tell(&io->file);
            break;

        case SEEK_END:
            base = f_size(&io->file);
            break;

        default:
            return false;
    }

    if ((offset < 0) && ((FSIZE_t)(-offset) > base)) {
        return false;
    }

    pos = (offset < 0) ? (base - (FSIZE_t)(-offset)) : (base + (FSIZE_t)offset);

    return FR_OK == f_lseek(&io->file, pos);
}

static intptr_t fatfs_io_tell(uintptr_t target, void *loader)
{
    arm_loader_io_fatfs_t *io = (arm_loader_io_fatfs_t *)target;

    (void)loader;

    if ((NULL == io) || !io->is_open) {
        return -1;
    }

    return (intptr_t)f_tell(&io->file);
}

static size_t fatfs_io_read(uintptr_t target, void *loader, uint8_t *buffer, size_t size)
{
    arm_loader_io_fatfs_t *io = (arm_loader_io_fatfs_t *)target;
    UINT bytes_read = 0;
    FRESULT fr;

    (void)loader;

    if ((NULL == io) || !io->is_open || (NULL == buffer)) {
        return 0;
    }

    fr = f_read(&io->file, buffer, (UINT)size, &bytes_read);
    if (FR_OK != fr) {
        printf("QOI read failed: %s (%d)\r\n", FRESULT_str(fr), fr);
        return 0;
    }

    return (size_t)bytes_read;
}
