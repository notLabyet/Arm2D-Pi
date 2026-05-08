#include "tufty_sdcard.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "diskio.h"
#include "f_util.h"

static FATFS s_sd_fs;
static bool s_sd_mounted;

static uint16_t rd_u16_le(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd_u32_le(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static bool sector_is_all(const uint8_t *sector, uint8_t value)
{
    for (size_t i = 0; i < 512u; ++i) {
        if (sector[i] != value) {
            return false;
        }
    }
    return true;
}

static void dump_sector_prefix(const uint8_t *sector)
{
    for (size_t row = 0; row < 4u; ++row) {
        printf("  %02u:", (unsigned)(row * 16u));
        for (size_t col = 0; col < 16u; ++col) {
            printf(" %02x", sector[row * 16u + col]);
        }
        printf("\r\n");
    }
}

static void dump_sector_range(const char *tag, const uint8_t *sector, size_t offset, size_t length)
{
    printf("%s offset 0x%03x length %u:\r\n", tag, (unsigned)offset, (unsigned)length);
    for (size_t row = 0; row < length; row += 16u) {
        printf("  %03x:", (unsigned)(offset + row));
        for (size_t col = 0; (col < 16u) && ((row + col) < length); ++col) {
            printf(" %02x", sector[offset + row + col]);
        }
        printf("\r\n");
    }
}

static void dump_boot_like_info(const char *tag, const uint8_t *sector)
{
    printf("%s signature: %02x %02x\r\n", tag, sector[510], sector[511]);
    printf("%s first 64 bytes:\r\n", tag);
    dump_sector_prefix(sector);

    printf("%s BPB: bytes/sector=%u sectors/cluster=%u reserved=%u fats=%u "
           "root_entries=%u total16=%u media=0x%02x fatsz16=%u hidden=%lu total32=%lu\r\n",
           tag,
           rd_u16_le(&sector[11]),
           sector[13],
           rd_u16_le(&sector[14]),
           sector[16],
           rd_u16_le(&sector[17]),
           rd_u16_le(&sector[19]),
           sector[21],
           rd_u16_le(&sector[22]),
           (unsigned long)rd_u32_le(&sector[28]),
           (unsigned long)rd_u32_le(&sector[32]));

    printf("%s FS strings: fat16='%.8s' fat32='%.8s' exfat='%.8s'\r\n",
           tag, &sector[54], &sector[82], &sector[3]);
}

static void tufty_sdcard_dump_mount_debug(void)
{
    static uint8_t sector[512];
    DSTATUS status;
    DRESULT dr;
    LBA_t sector_count = 0;
    DWORD block_size = 0;

    printf("SD mount debug begin\r\n");

    status = disk_status(0);
    printf("disk_status before init: 0x%02x\r\n", status);

    status = disk_initialize(0);
    printf("disk_initialize: 0x%02x", status);
    if (status & STA_NOINIT) {
        printf(" STA_NOINIT");
    }
    if (status & STA_NODISK) {
        printf(" STA_NODISK");
    }
    if (status & STA_PROTECT) {
        printf(" STA_PROTECT");
    }
    printf("\r\n");

    status = disk_status(0);
    printf("disk_status after init: 0x%02x\r\n", status);

    dr = disk_ioctl(0, GET_SECTOR_COUNT, &sector_count);
    printf("GET_SECTOR_COUNT: res=%d count=%llu\r\n", dr, (unsigned long long)sector_count);

    dr = disk_ioctl(0, GET_BLOCK_SIZE, &block_size);
    printf("GET_BLOCK_SIZE: res=%d block=%lu\r\n", dr, (unsigned long)block_size);

    memset(sector, 0, sizeof(sector));
    dr = disk_read(0, sector, 0, 1);
    printf("disk_read LBA0: res=%d all00=%u allff=%u\r\n",
           dr, sector_is_all(sector, 0x00u), sector_is_all(sector, 0xffu));
    if (RES_OK != dr) {
        printf("SD mount debug end\r\n");
        return;
    }

    dump_boot_like_info("LBA0", sector);
    dump_sector_range("LBA0 partition table", sector, 446u, 64u);
    dump_sector_range("LBA0 signature area", sector, 496u, 16u);

    for (size_t part = 0; part < 4u; ++part) {
        const uint8_t *entry = &sector[446u + part * 16u];
        uint8_t type = entry[4];
        uint32_t start_lba = rd_u32_le(&entry[8]);
        uint32_t sectors = rd_u32_le(&entry[12]);
        uint64_t end_lba = (uint64_t)start_lba + (uint64_t)sectors;

        printf("MBR part%u: status=0x%02x type=0x%02x start=%lu sectors=%lu\r\n",
               (unsigned)(part + 1u), entry[0], type,
               (unsigned long)start_lba, (unsigned long)sectors);
        if ((0u != type) && (0u != sectors)) {
            printf("MBR part%u end_exclusive=%llu card_sectors=%llu %s\r\n",
                   (unsigned)(part + 1u),
                   (unsigned long long)end_lba,
                   (unsigned long long)sector_count,
                   (end_lba > (uint64_t)sector_count) ? "OUT_OF_RANGE" : "in_range");
        }

        if ((0u != type) && (0u != start_lba) && (0u != sectors)) {
            memset(sector, 0, sizeof(sector));
            dr = disk_read(0, sector, (LBA_t)start_lba, 1);
            printf("disk_read part%u start LBA %lu: res=%d all00=%u allff=%u\r\n",
                   (unsigned)(part + 1u), (unsigned long)start_lba, dr,
                   sector_is_all(sector, 0x00u), sector_is_all(sector, 0xffu));
            if (RES_OK == dr) {
                dump_boot_like_info("PART", sector);
                dump_sector_range("PART signature area", sector, 496u, 16u);
            }
            break;
        }
    }

    printf("SD mount debug end\r\n");
}

FRESULT tufty_sdcard_mount(void)
{
    FRESULT fr;

    if (s_sd_mounted) {
        return FR_OK;
    }

    fr = f_mount(&s_sd_fs, "", 1);
    if (FR_OK == fr) {
        s_sd_mounted = true;
    } else {
        printf("SD f_mount failed: %s (%d)\r\n", FRESULT_str(fr), fr);
        tufty_sdcard_dump_mount_debug();
    }

    return fr;
}

void tufty_sdcard_unmount(void)
{
    if (s_sd_mounted) {
        (void)f_unmount("");
        s_sd_mounted = false;
    }
}

bool tufty_sdcard_write_test_file(void)
{
    FIL file;
    FRESULT fr;

    fr = tufty_sdcard_mount();
    if (FR_OK != fr) {
        return false;
    }

    fr = f_open(&file, "sdio_test.txt", FA_OPEN_APPEND | FA_WRITE);
    if (FR_OK != fr) {
        printf("SD f_open failed: %s (%d)\r\n", FRESULT_str(fr), fr);
        return false;
    }

    if (f_printf(&file, "Tufty2040 SDIO write test\r\n") < 0) {
        printf("SD f_printf failed\r\n");
        (void)f_close(&file);
        return false;
    }

    fr = f_close(&file);
    if (FR_OK != fr) {
        printf("SD f_close failed: %s (%d)\r\n", FRESULT_str(fr), fr);
        return false;
    }

    return true;
}

FRESULT tufty_sdcard_write_file(const char *path,
                                const void *data,
                                UINT data_size,
                                UINT *bytes_written)
{
    FIL file;
    FRESULT fr;
    UINT written = 0;

    if ((NULL == path) || ((NULL == data) && (data_size > 0u))) {
        if (bytes_written) {
            *bytes_written = 0;
        }
        return FR_INVALID_PARAMETER;
    }

    fr = tufty_sdcard_mount();
    if (FR_OK != fr) {
        if (bytes_written) {
            *bytes_written = 0;
        }
        return fr;
    }

    fr = f_open(&file, path, FA_CREATE_ALWAYS | FA_WRITE);
    if (FR_OK != fr) {
        printf("SD write_file f_open failed: %s (%d), path=%s\r\n", FRESULT_str(fr), fr, path);
        if (bytes_written) {
            *bytes_written = 0;
        }
        return fr;
    }

    fr = f_write(&file, data, data_size, &written);
    if (FR_OK == fr) {
        FRESULT close_fr = f_close(&file);
        if (FR_OK != close_fr) {
            fr = close_fr;
        }
    } else {
        (void)f_close(&file);
    }

    if (bytes_written) {
        *bytes_written = written;
    }

    if ((FR_OK != fr) || (written != data_size)) {
        printf("SD write_file failed: %s (%d), wrote %u/%u, path=%s\r\n",
               FRESULT_str(fr), fr, written, data_size, path);
    }

    return fr;
}

FRESULT tufty_sdcard_read_file(const char *path,
                               void *buffer,
                               UINT buffer_size,
                               UINT *bytes_read)
{
    FIL file;
    FRESULT fr;
    UINT read_count = 0;

    if ((NULL == path) || ((NULL == buffer) && (buffer_size > 0u))) {
        if (bytes_read) {
            *bytes_read = 0;
        }
        return FR_INVALID_PARAMETER;
    }

    fr = tufty_sdcard_mount();
    if (FR_OK != fr) {
        if (bytes_read) {
            *bytes_read = 0;
        }
        return fr;
    }

    fr = f_open(&file, path, FA_READ);
    if (FR_OK != fr) {
        printf("SD read_file f_open failed: %s (%d), path=%s\r\n", FRESULT_str(fr), fr, path);
        if (bytes_read) {
            *bytes_read = 0;
        }
        return fr;
    }

    fr = f_read(&file, buffer, buffer_size, &read_count);
    {
        FRESULT close_fr = f_close(&file);
        if ((FR_OK == fr) && (FR_OK != close_fr)) {
            fr = close_fr;
        }
    }

    if (bytes_read) {
        *bytes_read = read_count;
    }

    if (FR_OK != fr) {
        printf("SD read_file failed: %s (%d), read %u/%u, path=%s\r\n",
               FRESULT_str(fr), fr, read_count, buffer_size, path);
    }

    return fr;
}

bool tufty_sdcard_read_write_test(void)
{
    static const char test_path[] = "sdio_rw_test.txt";
    static const char test_text[] =
        "Tufty2040 SDIO read/write test\r\n"
        "If you can read this file, FatFs write and read both worked.\r\n";
    char read_buf[sizeof(test_text)];
    FRESULT fr;
    UINT bytes_written = 0;
    UINT bytes_read = 0;

    fr = tufty_sdcard_write_file(test_path, test_text, sizeof(test_text) - 1u, &bytes_written);
    if ((FR_OK != fr) || (bytes_written != sizeof(test_text) - 1u)) {
        return false;
    }

    memset(read_buf, 0, sizeof(read_buf));
    fr = tufty_sdcard_read_file(test_path, read_buf, sizeof(test_text) - 1u, &bytes_read);
    if ((FR_OK != fr) || (bytes_read != sizeof(test_text) - 1u)) {
        return false;
    }

    if (0 != memcmp(read_buf, test_text, sizeof(test_text) - 1u)) {
        printf("SD RW test verify failed: read data mismatch\r\n");
        return false;
    }

    printf("SD RW test OK: wrote and verified %s\r\n", test_path);
    return true;
}
