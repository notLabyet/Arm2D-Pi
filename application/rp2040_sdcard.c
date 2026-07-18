#include "rp2040_sdcard.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "delays.h"
#include "diskio.h"
#include "f_util.h"
#include "hw_config.h"
#include "SDIO/SdioCard.h"

#ifndef RP2040_SDCARD_PERF_FILE
#   define RP2040_SDCARD_PERF_FILE           "sd_perf.bin"
#endif
#ifndef RP2040_SDCARD_PERF_DEFAULT_SIZE
#   define RP2040_SDCARD_PERF_DEFAULT_SIZE   (12u * 1024u * 1024u)
#endif
#ifndef RP2040_SDCARD_PERF_DEFAULT_CHUNK
#   define RP2040_SDCARD_PERF_DEFAULT_CHUNK  (8u * 1024u)
#endif
#ifndef RP2040_SDCARD_PERF_MAX_RETRIES
#   define RP2040_SDCARD_PERF_MAX_RETRIES    3u
#endif
#define RP2040_SDCARD_PERF_PATTERN_SEED      0x5a17c3e9u

static FATFS s_sd_fs;
static volatile bool s_sd_mounted;
static uint8_t s_perf_write_buf[RP2040_SDCARD_PERF_DEFAULT_CHUNK];
static uint8_t s_perf_read_buf[RP2040_SDCARD_PERF_DEFAULT_CHUNK];

static void perf_capture_sdio_error(rp2040_sdcard_perf_result_t *result)
{
    sd_card_t *card = sd_get_by_num(0);

    if ((NULL != card) && (SD_IF_SDIO == card->type)) {
        result->last_sdio_error_code = sd_sdio_errorCode(card);
        result->last_sdio_error_line = sd_sdio_errorLine(card);
    }
}

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

static void rp2040_sdcard_dump_mount_debug(void)
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

FRESULT rp2040_sdcard_mount(void)
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
        rp2040_sdcard_dump_mount_debug();
    }

    return fr;
}

void rp2040_sdcard_unmount(void)
{
    if (s_sd_mounted) {
        (void)f_unmount("");
        s_sd_mounted = false;
    }
}

bool rp2040_sdcard_is_mounted(void)
{
    return s_sd_mounted;
}

bool rp2040_sdcard_write_test_file(void)
{
    FIL file;
    FRESULT fr;

    fr = rp2040_sdcard_mount();
    if (FR_OK != fr) {
        return false;
    }

    fr = f_open(&file, "sdio_test.txt", FA_OPEN_APPEND | FA_WRITE);
    if (FR_OK != fr) {
        printf("SD f_open failed: %s (%d)\r\n", FRESULT_str(fr), fr);
        return false;
    }

    if (f_printf(&file, "RP2040 SDIO write test\r\n") < 0) {
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

FRESULT rp2040_sdcard_write_file(const char *path,
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

    fr = rp2040_sdcard_mount();
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

FRESULT rp2040_sdcard_read_file(const char *path,
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

    fr = rp2040_sdcard_mount();
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

bool rp2040_sdcard_read_write_test(void)
{
    static const char test_path[] = "sdio_rw_test.txt";
    static const char test_text[] =
        "RP2040 SDIO read/write test\r\n"
        "If you can read this file, FatFs write and read both worked.\r\n";
    char read_buf[sizeof(test_text)];
    FRESULT fr;
    UINT bytes_written = 0;
    UINT bytes_read = 0;

    fr = rp2040_sdcard_write_file(test_path, test_text, sizeof(test_text) - 1u, &bytes_written);
    if ((FR_OK != fr) || (bytes_written != sizeof(test_text) - 1u)) {
        return false;
    }

    memset(read_buf, 0, sizeof(read_buf));
    fr = rp2040_sdcard_read_file(test_path, read_buf, sizeof(test_text) - 1u, &bytes_read);
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

static uint32_t perf_speed_kib_per_s(uint32_t bytes, uint32_t time_ms)
{
    if (0u == time_ms) {
        time_ms = 1u;
    }

    return (uint32_t)(((uint64_t)bytes * 1000u) / ((uint64_t)time_ms * 1024u));
}

static uint8_t perf_pattern_byte(uint32_t offset)
{
    uint32_t x = offset ^ RP2040_SDCARD_PERF_PATTERN_SEED;

    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;

    return (uint8_t)x;
}

static void perf_fill_buffer(uint8_t *buffer, UINT length)
{
    for (UINT i = 0; i < length; ++i) {
        buffer[i] = perf_pattern_byte(i);
    }
}

static uint32_t perf_count_verify_errors(const uint8_t *buffer,
                                         uint32_t file_offset,
                                         UINT length)
{
    uint32_t errors = 0;

    for (UINT i = 0; i < length; ++i) {
        if (buffer[i] != perf_pattern_byte(i)) {
            errors++;
            if (errors <= 8u) {
                printf("SD perf verify mismatch at %lu: got 0x%02x expected 0x%02x\r\n",
                       (unsigned long)(file_offset + i),
                       buffer[i],
                       perf_pattern_byte(i));
            }
        }
    }

    return errors;
}

static void perf_print_result(const rp2040_sdcard_perf_result_t *result)
{
    printf("SD perf result: %s\r\n", result->passed ? "PASS" : "FAIL");
    printf("  file=%lu bytes chunk=%lu bytes\r\n",
           (unsigned long)result->file_size_bytes,
           (unsigned long)result->chunk_size_bytes);
    printf("  write: %lu ms, %lu KiB/s, errors=%lu short=%lu retries=%lu\r\n",
           (unsigned long)result->write_time_ms,
           (unsigned long)result->write_kib_per_s,
           (unsigned long)result->write_errors,
           (unsigned long)result->short_writes,
           (unsigned long)result->write_retries);
    printf("  sync:  %lu ms\r\n", (unsigned long)result->sync_time_ms);
    printf("  read:  %lu ms, %lu KiB/s, errors=%lu short=%lu retries=%lu\r\n",
           (unsigned long)result->read_time_ms,
           (unsigned long)result->read_kib_per_s,
           (unsigned long)result->read_errors,
           (unsigned long)result->short_reads,
           (unsigned long)result->read_retries);
    printf("  verify: %lu ms, verify_errors=%lu\r\n",
           (unsigned long)result->verify_time_ms,
           (unsigned long)result->verify_errors);
    printf("  transfer_errors=%lu last_sdio_error=%lu line=%lu\r\n",
           (unsigned long)result->transfer_errors,
           (unsigned long)result->last_sdio_error_code,
           (unsigned long)result->last_sdio_error_line);
    printf("  last_error: %s (%d)\r\n",
           FRESULT_str(result->last_error),
           result->last_error);
}

bool rp2040_sdcard_perf_test(uint32_t file_size_bytes,
                            uint32_t chunk_size_bytes,
                            rp2040_sdcard_perf_result_t *result)
{
    rp2040_sdcard_perf_result_t local_result;
    rp2040_sdcard_perf_result_t *r = result ? result : &local_result;
    FIL file;
    FRESULT fr;
    uint32_t offset;
    uint32_t start_ms;
    uint32_t sync_start_ms;
    uint32_t verify_start_ms;

    memset(r, 0, sizeof(*r));
    r->file_size_bytes = file_size_bytes;
    r->chunk_size_bytes = chunk_size_bytes;
    r->last_error = FR_OK;

    if ((0u == file_size_bytes) ||
        (0u == chunk_size_bytes) ||
        (chunk_size_bytes > sizeof(s_perf_write_buf))) {
        r->last_error = FR_INVALID_PARAMETER;
        perf_print_result(r);
        return false;
    }

    fr = rp2040_sdcard_mount();
    if (FR_OK != fr) {
        r->last_error = fr;
        perf_print_result(r);
        return false;
    }

    (void)f_unlink(RP2040_SDCARD_PERF_FILE);

    printf("SD perf write/read test start: file=%lu bytes chunk=%lu bytes retries=%u\r\n",
           (unsigned long)file_size_bytes,
           (unsigned long)chunk_size_bytes,
           (unsigned)RP2040_SDCARD_PERF_MAX_RETRIES);

    perf_fill_buffer(s_perf_write_buf, (UINT)chunk_size_bytes);

    fr = f_open(&file, RP2040_SDCARD_PERF_FILE, FA_CREATE_ALWAYS | FA_WRITE);
    if (FR_OK != fr) {
        r->last_error = fr;
        perf_print_result(r);
        return false;
    }

    start_ms = millis();
    for (offset = 0; offset < file_size_bytes;) {
        UINT request = (UINT)((file_size_bytes - offset) < chunk_size_bytes ?
                              (file_size_bytes - offset) : chunk_size_bytes);
        bool chunk_done = false;

        for (uint32_t attempt = 0; attempt <= RP2040_SDCARD_PERF_MAX_RETRIES; ++attempt) {
            UINT written = 0;
            fr = f_write(&file, s_perf_write_buf, request, &written);

            if ((FR_OK == fr) && (written == request)) {
                chunk_done = true;
                break;
            }

            r->transfer_errors++;
            perf_capture_sdio_error(r);
            if (FR_OK != fr) {
                r->write_errors++;
                r->last_error = fr;
            }
            if (written != request) {
                r->short_writes++;
                if (FR_OK == fr) {
                    r->last_error = FR_DISK_ERR;
                }
            }
            if (attempt < RP2040_SDCARD_PERF_MAX_RETRIES) {
                r->write_retries++;
                (void)f_lseek(&file, offset);
            }
        }

        if (!chunk_done) {
            (void)f_close(&file);
            perf_print_result(r);
            return false;
        }

        offset += request;
    }
    r->write_time_ms = millis() - start_ms;
    r->write_kib_per_s = perf_speed_kib_per_s(file_size_bytes, r->write_time_ms);

    sync_start_ms = millis();
    fr = f_sync(&file);
    r->sync_time_ms = millis() - sync_start_ms;
    if (FR_OK != fr) {
        r->last_error = fr;
        r->transfer_errors++;
        perf_capture_sdio_error(r);
        (void)f_close(&file);
        perf_print_result(r);
        return false;
    }

    fr = f_close(&file);
    if (FR_OK != fr) {
        r->last_error = fr;
        r->transfer_errors++;
        perf_capture_sdio_error(r);
        perf_print_result(r);
        return false;
    }

    fr = f_open(&file, RP2040_SDCARD_PERF_FILE, FA_READ);
    if (FR_OK != fr) {
        r->last_error = fr;
        perf_print_result(r);
        return false;
    }

    for (offset = 0; offset < file_size_bytes;) {
        UINT request = (UINT)((file_size_bytes - offset) < chunk_size_bytes ?
                              (file_size_bytes - offset) : chunk_size_bytes);
        bool chunk_done = false;

        for (uint32_t attempt = 0; attempt <= RP2040_SDCARD_PERF_MAX_RETRIES; ++attempt) {
            uint32_t read_start_ms;
            UINT read_count = 0;
            read_start_ms = millis();
            fr = f_read(&file, s_perf_read_buf, request, &read_count);
            r->read_time_ms += millis() - read_start_ms;

            if ((FR_OK == fr) && (read_count == request)) {
                chunk_done = true;
                break;
            }

            r->transfer_errors++;
            perf_capture_sdio_error(r);
            if (FR_OK != fr) {
                r->read_errors++;
                r->last_error = fr;
            }
            if (read_count != request) {
                r->short_reads++;
                if (FR_OK == fr) {
                    r->last_error = FR_DISK_ERR;
                }
            }
            if (attempt < RP2040_SDCARD_PERF_MAX_RETRIES) {
                r->read_retries++;
                (void)f_lseek(&file, offset);
            }
        }

        if (!chunk_done) {
            (void)f_close(&file);
            perf_print_result(r);
            return false;
        }

        verify_start_ms = millis();
        r->verify_errors += perf_count_verify_errors(s_perf_read_buf, offset, request);
        r->verify_time_ms += millis() - verify_start_ms;
        offset += request;
    }
    r->read_kib_per_s = perf_speed_kib_per_s(file_size_bytes, r->read_time_ms);

    fr = f_close(&file);
    if (FR_OK != fr) {
        r->last_error = fr;
        r->transfer_errors++;
        perf_capture_sdio_error(r);
    }

    r->passed = (FR_OK == fr) &&
                (0u == r->write_errors) &&
                (0u == r->read_errors) &&
                (0u == r->short_writes) &&
                (0u == r->short_reads) &&
                (0u == r->verify_errors) &&
                (0u == r->transfer_errors);
    if (r->passed) {
        r->last_error = FR_OK;
    }

    perf_print_result(r);
    return r->passed;
}

bool rp2040_sdcard_default_perf_test(void)
{
    return rp2040_sdcard_perf_test(RP2040_SDCARD_PERF_DEFAULT_SIZE,
                                  RP2040_SDCARD_PERF_DEFAULT_CHUNK,
                                  NULL);
}
