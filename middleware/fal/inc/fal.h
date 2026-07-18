#ifndef FAL_H
#define FAL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FAL_DEV_NAME_MAX               24u
#define FAL_PART_MAGIC_WORD            0x45503130u
#define FAL_PART_FLAG_READ_ONLY        (1u << 0)

struct fal_flash_dev {
    char name[FAL_DEV_NAME_MAX];
    uint32_t addr;
    size_t len;
    size_t blk_size;
    struct {
        int (*init)(void);
        int (*read)(long offset, uint8_t *buf, size_t size);
        int (*write)(long offset, const uint8_t *buf, size_t size);
        int (*erase)(long offset, size_t size);
    } ops;
    uint32_t write_gran;
};

struct fal_partition {
    uint32_t magic_word;
    char name[FAL_DEV_NAME_MAX];
    char flash_name[FAL_DEV_NAME_MAX];
    long offset;
    size_t len;
    uint32_t reserved;
};

typedef struct fal_flash_dev fal_flash_dev_t;
typedef struct fal_partition fal_partition_t;

int fal_init(void);

const struct fal_flash_dev *fal_flash_device_find(const char *name);
const struct fal_partition *fal_partition_find(const char *name);
const struct fal_flash_dev *fal_partition_device_find(const struct fal_partition *partition);

int fal_partition_read(const struct fal_partition *partition,
                       long offset,
                       uint8_t *buf,
                       size_t size);
int fal_partition_write(const struct fal_partition *partition,
                        long offset,
                        const uint8_t *buf,
                        size_t size);
int fal_partition_erase(const struct fal_partition *partition,
                        long offset,
                        size_t size);
int fal_partition_erase_all(const struct fal_partition *partition);

const struct fal_partition *fal_partition_table_get(size_t *count);
size_t fal_partition_table_len(void);

#ifdef __cplusplus
}
#endif

#endif
