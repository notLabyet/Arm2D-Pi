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
#if ARM_LOADER_IO_FATFS_USE_CACHE
static void fatfs_io_invalidate_cache(arm_loader_io_fatfs_t *io);
static arm_loader_io_fatfs_cache_t *fatfs_io_find_cache(arm_loader_io_fatfs_t *io,
                                                        FSIZE_t position,
                                                        size_t size);
static arm_loader_io_fatfs_cache_t *fatfs_io_load_cache(arm_loader_io_fatfs_t *io,
                                                        FSIZE_t position);
#endif

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
        printf("FatFs IO SD mount failed: %s (%d)\r\n", FRESULT_str(fr), fr);
        return false;
    }

    fr = f_open(&io->file, io->path, FA_READ);
    if (FR_OK != fr) {
        printf("FatFs IO open %s failed: %s (%d)\r\n", io->path, FRESULT_str(fr), fr);
        tufty_sdcard_unmount();
        return false;
    }

    io->is_open = true;
    io->file_size = f_size(&io->file);
    io->position = 0;
#if ARM_LOADER_IO_FATFS_USE_CACHE
    fatfs_io_invalidate_cache(io);
#endif
    return true;
}

static void fatfs_io_close(uintptr_t target, void *loader)
{
    arm_loader_io_fatfs_t *io = (arm_loader_io_fatfs_t *)target;

    (void)loader;

    if ((NULL != io) && io->is_open) {
        (void)f_close(&io->file);
#if ARM_LOADER_IO_FATFS_USE_CACHE
        fatfs_io_invalidate_cache(io);
#endif
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
            base = io->position;
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

    if (pos > io->file_size) {
        return false;
    }

    io->position = pos;
    return true;
}

static intptr_t fatfs_io_tell(uintptr_t target, void *loader)
{
    arm_loader_io_fatfs_t *io = (arm_loader_io_fatfs_t *)target;

    (void)loader;

    if ((NULL == io) || !io->is_open) {
        return -1;
    }

    return (intptr_t)io->position;
}

static size_t fatfs_io_read(uintptr_t target, void *loader, uint8_t *buffer, size_t size)
{
    arm_loader_io_fatfs_t *io = (arm_loader_io_fatfs_t *)target;
    uint8_t *dst = buffer;
    size_t total_read = 0;

    (void)loader;

    if ((NULL == io) || !io->is_open || (NULL == buffer)) {
        return 0;
    }

#if !ARM_LOADER_IO_FATFS_USE_CACHE
    while ((total_read < size) && (io->position < io->file_size)) {
        UINT request;
        UINT bytes_read = 0;
        FRESULT fr;

        request = (UINT)MIN((FSIZE_t)(size - total_read), io->file_size - io->position);

        fr = f_lseek(&io->file, io->position);
        if (FR_OK != fr) {
            printf("FatFs IO seek failed: %s (%d)\r\n", FRESULT_str(fr), fr);
            break;
        }

        fr = f_read(&io->file, &dst[total_read], request, &bytes_read);
        if (FR_OK != fr) {
            printf("FatFs IO read failed: %s (%d)\r\n", FRESULT_str(fr), fr);
            break;
        }

        if (0u == bytes_read) {
            break;
        }

        total_read += bytes_read;
        io->position += (FSIZE_t)bytes_read;
    }
#else
    while ((total_read < size) && (io->position < io->file_size)) {
        arm_loader_io_fatfs_cache_t *cache;
        FSIZE_t cache_offset;
        size_t available;
        size_t copy_count;

        cache = fatfs_io_find_cache(io, io->position, size - total_read);
        if (NULL == cache) {
            cache = fatfs_io_load_cache(io, io->position);
            if (NULL == cache) {
                break;
            }
        }

        cache_offset = io->position - cache->start;
        available = (size_t)(cache->size - (UINT)cache_offset);
        copy_count = MIN(size - total_read, available);

        memcpy(&dst[total_read], &cache->buffer[cache_offset], copy_count);
        total_read += copy_count;
        io->position += (FSIZE_t)copy_count;
    }
#endif

    return total_read;
}

#if ARM_LOADER_IO_FATFS_USE_CACHE
static void fatfs_io_invalidate_cache(arm_loader_io_fatfs_t *io)
{
    if (NULL == io) {
        return;
    }

    io->cache_age = 0;
    for (size_t i = 0; i < dimof(io->cache); ++i) {
        io->cache[i].valid = false;
        io->cache[i].start = 0;
        io->cache[i].size = 0;
        io->cache[i].age = 0;
    }
}

static arm_loader_io_fatfs_cache_t *fatfs_io_find_cache(arm_loader_io_fatfs_t *io,
                                                        FSIZE_t position,
                                                        size_t size)
{
    arm_loader_io_fatfs_cache_t *best = NULL;

    (void)size;

    for (size_t i = 0; i < dimof(io->cache); ++i) {
        arm_loader_io_fatfs_cache_t *cache = &io->cache[i];

        if (!cache->valid) {
            continue;
        }

        if ((position >= cache->start) &&
            (position < (cache->start + (FSIZE_t)cache->size))) {
            best = cache;
            break;
        }
    }

    if (NULL != best) {
        best->age = ++io->cache_age;
    }

    return best;
}

static arm_loader_io_fatfs_cache_t *fatfs_io_select_victim(arm_loader_io_fatfs_t *io)
{
    arm_loader_io_fatfs_cache_t *victim = &io->cache[0];

    for (size_t i = 0; i < dimof(io->cache); ++i) {
        arm_loader_io_fatfs_cache_t *cache = &io->cache[i];

        if (!cache->valid) {
            return cache;
        }

        if (cache->age < victim->age) {
            victim = cache;
        }
    }

    return victim;
}

static arm_loader_io_fatfs_cache_t *fatfs_io_load_cache(arm_loader_io_fatfs_t *io,
                                                        FSIZE_t position)
{
    arm_loader_io_fatfs_cache_t *cache = fatfs_io_select_victim(io);
    UINT bytes_read = 0;
    UINT request;
    FRESULT fr;

    if (position >= io->file_size) {
        return NULL;
    }

    request = (UINT)MIN((FSIZE_t)sizeof(cache->buffer), io->file_size - position);

    fr = f_lseek(&io->file, position);
    if (FR_OK != fr) {
        printf("FatFs IO seek failed: %s (%d)\r\n", FRESULT_str(fr), fr);
        cache->valid = false;
        return NULL;
    }

    fr = f_read(&io->file, cache->buffer, request, &bytes_read);
    if (FR_OK != fr) {
        printf("FatFs IO read failed: %s (%d)\r\n", FRESULT_str(fr), fr);
        cache->valid = false;
        return NULL;
    }

    if (0u == bytes_read) {
        cache->valid = false;
        return NULL;
    }

    cache->start = position;
    cache->size = bytes_read;
    cache->age = ++io->cache_age;
    cache->valid = true;

    return cache;
}
#endif
