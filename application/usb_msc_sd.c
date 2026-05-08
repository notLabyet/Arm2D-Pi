#include "usb_msc_sd.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "diskio.h"
#include "tufty_sdcard.h"
#include "tusb.h"

#define USB_MSC_SD_PDRV          0u
#define USB_MSC_SD_BLOCK_SIZE    512u

static bool s_msc_ready;
static uint32_t s_msc_block_count;
static uint8_t s_msc_sector_buf[USB_MSC_SD_BLOCK_SIZE];

static bool usb_msc_sd_ensure_ready(void)
{
    LBA_t sector_count = 0;

    if (s_msc_ready) {
        return true;
    }

    if (0 != (disk_initialize(USB_MSC_SD_PDRV) & (STA_NOINIT | STA_NODISK))) {
        return false;
    }

    if (RES_OK != disk_ioctl(USB_MSC_SD_PDRV, GET_SECTOR_COUNT, &sector_count)) {
        return false;
    }

    if ((sector_count == 0u) || (sector_count > UINT32_MAX)) {
        return false;
    }

    s_msc_block_count = (uint32_t)sector_count;
    s_msc_ready = true;

    return true;
}

bool usb_msc_sd_init(void)
{
    tufty_sdcard_unmount();
    s_msc_ready = false;
    s_msc_block_count = 0;

    if (!usb_msc_sd_ensure_ready()) {
        printf("USB MSC SD init failed\r\n");
        return false;
    }

    printf("USB MSC SD ready: %lu blocks x %u bytes\r\n",
           (unsigned long)s_msc_block_count,
           (unsigned)USB_MSC_SD_BLOCK_SIZE);
    return true;
}

bool usb_msc_sd_is_ready(void)
{
    return usb_msc_sd_ensure_ready();
}

uint32_t usb_msc_sd_block_count(void)
{
    return s_msc_block_count;
}

void tud_msc_inquiry_cb(uint8_t lun,
                        uint8_t vendor_id[8],
                        uint8_t product_id[16],
                        uint8_t product_rev[4])
{
    (void)lun;

    memcpy(vendor_id, "Tufty   ", 8);
    memcpy(product_id, "SD Card         ", 16);
    memcpy(product_rev, "1.0 ", 4);
}

bool tud_msc_test_unit_ready_cb(uint8_t lun)
{
    (void)lun;

    if (!usb_msc_sd_ensure_ready()) {
        tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3a, 0x00);
        return false;
    }

    return true;
}

void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size)
{
    (void)lun;

    (void)usb_msc_sd_ensure_ready();

    *block_count = s_msc_ready ? s_msc_block_count : 0u;
    *block_size = USB_MSC_SD_BLOCK_SIZE;
}

bool tud_msc_is_writable_cb(uint8_t lun)
{
    (void)lun;

    return true;
}

static bool usb_msc_sd_range_is_valid(uint32_t lba, uint32_t offset, uint32_t bufsize)
{
    uint64_t const last_byte = (uint64_t)offset + (uint64_t)bufsize;
    uint64_t const sector_count = (last_byte + USB_MSC_SD_BLOCK_SIZE - 1u) /
                                  USB_MSC_SD_BLOCK_SIZE;

    if (!usb_msc_sd_ensure_ready()) {
        return false;
    }

    if (offset >= USB_MSC_SD_BLOCK_SIZE) {
        return false;
    }

    if (sector_count == 0u) {
        return true;
    }

    return (lba < s_msc_block_count) && (sector_count <= (s_msc_block_count - lba));
}

int32_t tud_msc_read10_cb(uint8_t lun,
                          uint32_t lba,
                          uint32_t offset,
                          void *buffer,
                          uint32_t bufsize)
{
    uint8_t *dst = (uint8_t *)buffer;
    uint32_t done = 0;

    (void)lun;

    if (!usb_msc_sd_range_is_valid(lba, offset, bufsize)) {
        tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x21, 0x00);
        return -1;
    }

    if (bufsize == 0u) {
        return 0;
    }

    if ((offset == 0u) && ((bufsize % USB_MSC_SD_BLOCK_SIZE) == 0u)) {
        UINT const count = (UINT)(bufsize / USB_MSC_SD_BLOCK_SIZE);

        if (RES_OK != disk_read(USB_MSC_SD_PDRV, (BYTE *)buffer, (LBA_t)lba, count)) {
            tud_msc_set_sense(lun, SCSI_SENSE_MEDIUM_ERROR, 0x11, 0x00);
            return -1;
        }

        return (int32_t)bufsize;
    }

    while (done < bufsize) {
        uint32_t const chunk = USB_MSC_SD_BLOCK_SIZE - offset;
        uint32_t const copy_count = ((bufsize - done) < chunk) ? (bufsize - done) : chunk;

        if (RES_OK != disk_read(USB_MSC_SD_PDRV, s_msc_sector_buf, (LBA_t)lba, 1u)) {
            tud_msc_set_sense(lun, SCSI_SENSE_MEDIUM_ERROR, 0x11, 0x00);
            return -1;
        }

        memcpy(&dst[done], &s_msc_sector_buf[offset], copy_count);
        done += copy_count;
        lba++;
        offset = 0;
    }

    return (int32_t)bufsize;
}

int32_t tud_msc_write10_cb(uint8_t lun,
                           uint32_t lba,
                           uint32_t offset,
                           uint8_t *buffer,
                           uint32_t bufsize)
{
    uint32_t done = 0;

    (void)lun;

    if (!usb_msc_sd_range_is_valid(lba, offset, bufsize)) {
        tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x21, 0x00);
        return -1;
    }

    if (bufsize == 0u) {
        return 0;
    }

    if ((offset == 0u) && ((bufsize % USB_MSC_SD_BLOCK_SIZE) == 0u)) {
        UINT const count = (UINT)(bufsize / USB_MSC_SD_BLOCK_SIZE);

        if (RES_OK != disk_write(USB_MSC_SD_PDRV, buffer, (LBA_t)lba, count)) {
            tud_msc_set_sense(lun, SCSI_SENSE_MEDIUM_ERROR, 0x0c, 0x00);
            return -1;
        }

        return (int32_t)bufsize;
    }

    while (done < bufsize) {
        uint32_t const chunk = USB_MSC_SD_BLOCK_SIZE - offset;
        uint32_t const copy_count = ((bufsize - done) < chunk) ? (bufsize - done) : chunk;

        if (RES_OK != disk_read(USB_MSC_SD_PDRV, s_msc_sector_buf, (LBA_t)lba, 1u)) {
            tud_msc_set_sense(lun, SCSI_SENSE_MEDIUM_ERROR, 0x11, 0x00);
            return -1;
        }

        memcpy(&s_msc_sector_buf[offset], &buffer[done], copy_count);
        if (RES_OK != disk_write(USB_MSC_SD_PDRV, s_msc_sector_buf, (LBA_t)lba, 1u)) {
            tud_msc_set_sense(lun, SCSI_SENSE_MEDIUM_ERROR, 0x0c, 0x00);
            return -1;
        }

        done += copy_count;
        lba++;
        offset = 0;
    }

    return (int32_t)bufsize;
}

void tud_msc_write10_complete_cb(uint8_t lun)
{
    (void)lun;
    (void)disk_ioctl(USB_MSC_SD_PDRV, CTRL_SYNC, NULL);
}

int32_t tud_msc_scsi_cb(uint8_t lun,
                        uint8_t const scsi_cmd[16],
                        void *buffer,
                        uint16_t bufsize)
{
    (void)buffer;
    (void)bufsize;

    switch (scsi_cmd[0]) {
        case SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL:
            return 0;

        default:
            tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);
            return -1;
    }
}
