#include "fal.h"

#include <string.h>

#include "fal_cfg.h"

#ifndef FAL_FLASH_DEV_TABLE
#   error FAL_FLASH_DEV_TABLE must be defined in fal_cfg.h.
#endif

#ifndef FAL_PART_TABLE
#   error FAL_PART_TABLE must be defined in fal_cfg.h.
#endif

#define FAL_ARRAY_SIZE(__array)         (sizeof(__array) / sizeof((__array)[0]))

static const struct fal_flash_dev * const s_flash_table[] = FAL_FLASH_DEV_TABLE;
static const struct fal_partition s_partition_table[] = FAL_PART_TABLE;
static int s_fal_inited = 0;

static int fal_partition_table_check(void)
{
    size_t i;

    for (i = 0; i < FAL_ARRAY_SIZE(s_partition_table); i++) {
        const struct fal_partition *partition = &s_partition_table[i];
        const struct fal_flash_dev *dev;

        if (FAL_PART_MAGIC_WORD != partition->magic_word || partition->offset < 0) {
            return -1;
        }

        dev = fal_flash_device_find(partition->flash_name);
        if (NULL == dev) {
            return -1;
        }

        if ((uint32_t)partition->offset > dev->len ||
            partition->len > (dev->len - (uint32_t)partition->offset)) {
            return -1;
        }
    }

    return 0;
}

static int fal_partition_range_check(const struct fal_partition *partition,
                                     long offset,
                                     size_t size)
{
    uint32_t u32Offset;

    if (NULL == partition || offset < 0) {
        return -1;
    }

    if (FAL_PART_MAGIC_WORD != partition->magic_word) {
        return -1;
    }

    u32Offset = (uint32_t)offset;
    if (u32Offset > partition->len) {
        return -1;
    }

    if (size > (partition->len - u32Offset)) {
        return -1;
    }

    return 0;
}

int fal_init(void)
{
    size_t i;

    if (s_fal_inited) {
        return 0;
    }

    for (i = 0; i < FAL_ARRAY_SIZE(s_flash_table); i++) {
        const struct fal_flash_dev *dev = s_flash_table[i];

        if (NULL == dev) {
            return -1;
        }

        if (dev->ops.init && dev->ops.init() < 0) {
            return -1;
        }
    }

    if (fal_partition_table_check() < 0) {
        return -1;
    }

    s_fal_inited = 1;
    return 0;
}

const struct fal_flash_dev *fal_flash_device_find(const char *name)
{
    size_t i;

    if (NULL == name) {
        return NULL;
    }

    for (i = 0; i < FAL_ARRAY_SIZE(s_flash_table); i++) {
        const struct fal_flash_dev *dev = s_flash_table[i];

        if (dev && 0 == strncmp(dev->name, name, FAL_DEV_NAME_MAX)) {
            return dev;
        }
    }

    return NULL;
}

const struct fal_partition *fal_partition_find(const char *name)
{
    size_t i;

    if (NULL == name) {
        return NULL;
    }

    for (i = 0; i < FAL_ARRAY_SIZE(s_partition_table); i++) {
        const struct fal_partition *partition = &s_partition_table[i];

        if (0 == strncmp(partition->name, name, FAL_DEV_NAME_MAX)) {
            return partition;
        }
    }

    return NULL;
}

const struct fal_flash_dev *fal_partition_device_find(const struct fal_partition *partition)
{
    if (NULL == partition || FAL_PART_MAGIC_WORD != partition->magic_word) {
        return NULL;
    }

    return fal_flash_device_find(partition->flash_name);
}

int fal_partition_read(const struct fal_partition *partition,
                       long offset,
                       uint8_t *buf,
                       size_t size)
{
    const struct fal_flash_dev *dev;

    if ((size > 0u && NULL == buf) ||
        fal_partition_range_check(partition, offset, size) < 0) {
        return -1;
    }

    dev = fal_partition_device_find(partition);
    if (NULL == dev || NULL == dev->ops.read) {
        return -1;
    }

    return dev->ops.read(partition->offset + offset, buf, size);
}

int fal_partition_write(const struct fal_partition *partition,
                        long offset,
                        const uint8_t *buf,
                        size_t size)
{
    const struct fal_flash_dev *dev;

    if ((size > 0u && NULL == buf) ||
        fal_partition_range_check(partition, offset, size) < 0) {
        return -1;
    }

    if (partition->reserved & FAL_PART_FLAG_READ_ONLY) {
        return -1;
    }

    dev = fal_partition_device_find(partition);
    if (NULL == dev || NULL == dev->ops.write) {
        return -1;
    }

    return dev->ops.write(partition->offset + offset, buf, size);
}

int fal_partition_erase(const struct fal_partition *partition,
                        long offset,
                        size_t size)
{
    const struct fal_flash_dev *dev;

    if (fal_partition_range_check(partition, offset, size) < 0) {
        return -1;
    }

    if (partition->reserved & FAL_PART_FLAG_READ_ONLY) {
        return -1;
    }

    dev = fal_partition_device_find(partition);
    if (NULL == dev || NULL == dev->ops.erase) {
        return -1;
    }

    if (((uint32_t)offset % dev->blk_size) || (size % dev->blk_size)) {
        return -1;
    }

    return dev->ops.erase(partition->offset + offset, size);
}

int fal_partition_erase_all(const struct fal_partition *partition)
{
    if (NULL == partition) {
        return -1;
    }

    return fal_partition_erase(partition, 0, partition->len);
}

const struct fal_partition *fal_partition_table_get(size_t *count)
{
    if (count) {
        *count = FAL_ARRAY_SIZE(s_partition_table);
    }

    return s_partition_table;
}

size_t fal_partition_table_len(void)
{
    return FAL_ARRAY_SIZE(s_partition_table);
}
