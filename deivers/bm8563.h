/*

MIT License

Copyright (c) 2020-2021 Mika Tuupola

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

-cut-

This file is part of hardware agnostic I2C driver for BM8563 RTC:
https://github.com/tuupola/bm8563

SPDX-License-Identifier: MIT

*/

#ifndef _BM8563_H
#define _BM8563_H

/**
 * @file bm8563.h
 * @brief Hardware-agnostic I2C driver API for the BM8563 real-time clock (RTC).
 *
 * The BM8563 is a low-power RTC with BCD time registers, alarm, and timer features.
 * This header defines register addresses, bit masks, and a small HAL struct so the
 * same logic can run on any platform: supply @ref bm8563_t read/write callbacks that
 * perform SMBus-style I2C transfers to slave address @ref BM8563_ADDRESS (0x51).
 *
 * Time is exchanged through @c struct @c tm in the same convention as POSIX:
 * @c tm_year is years since 1900; @c tm_mon is 0–11; @c tm_wday is 0–6.
 *
 * @see https://github.com/tuupola/bm8563 (upstream MIT-licensed project)
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <time.h>

/** 7-bit I2C slave address (wire is often labeled A0/A1 strapped to GND). */
#define BM8563_ADDRESS          (0x51)
/** Control / status register 1: STOP halts the clock divider chain; TEST* are factory test bits. */
#define BM8563_CONTROL_STATUS1  (0x00)
#define BM8563_TESTC            (0b00001000)
#define BM8563_STOP             (0b00100000)
#define BM8563_TEST1            (0b10000000)
/** Control / status 2: alarm/timer interrupt enables and flags, TI/TP pulse mode. */
#define BM8563_CONTROL_STATUS2  (0x01)
#define BM8563_TIE              (0b00000001)
#define BM8563_AIE              (0b00000010)
#define BM8563_TF               (0b00000100)
#define BM8563_AF               (0b00001000)
#define BM8563_TI_TP            (0b00010000)
/** First byte of the time block: VL (bit7) indicates low backup voltage when reading. */
#define BM8563_SECONDS          (0x02)
#define BM8563_MINUTES          (0x03)
#define BM8563_HOURS            (0x04)
#define BM8563_DAY              (0x05)
#define BM8563_WEEKDAY          (0x06)
#define BM8563_MONTH            (0x07)
#define BM8563_YEAR             (0x08)
#define BM8563_TIME_SIZE        (0x07)
#define BM8563_CENTURY_BIT      (0b10000000)

#define BM8563_MINUTE_ALARM     (0x09)
#define BM8563_HOUR_ALARM       (0x0a)
#define BM8563_DAY_ALARM        (0x0b)
#define BM8563_WEEKDAY_ALARM    (0x0c)
#define BM8563_ALARM_DISABLE    (0b10000000)
#define BM8563_ALARM_NONE       (0xff)
#define BM8563_ALARM_SIZE       (0x04)

#define BM8563_TIMER_CONTROL    (0x0e)
#define BM8563_TIMER_ENABLE     (0b10000000)
#define BM8563_TIMER_4_096KHZ   (0b00000000)
#define BM8563_TIMER_64HZ       (0b00000001)
#define BM8563_TIMER_1HZ        (0b00000010)
#define BM8563_TIMER_1_60HZ     (0b00000011)
#define BM8563_TIMER            (0x0f)

/**
 * IOCTL-style command codes: high byte = register offset, low nibble distinguishes read/write.
 * Used by @ref bm8563_ioctl for alarm/timer and raw register access without extra wrappers.
 */
#define BM8563_ALARM_SET        (0x0900)
#define BM8563_ALARM_READ       (0x0901)
#define BM8563_CONTROL_STATUS1_READ     (0x0000)
#define BM8563_CONTROL_STATUS1_WRITE    (0x0001)
#define BM8563_CONTROL_STATUS2_READ     (0x0100)
#define BM8563_CONTROL_STATUS2_WRITE    (0x0101)
#define BM8563_TIMER_CONTROL_READ       (0x0e00)
#define BM8563_TIMER_CONTROL_WRITE      (0x0e01)
#define BM8563_TIMER_READ               (0x0f00)
#define BM8563_TIMER_WRITE              (0x0f01)

/* Status codes. */
#define BM8563_ERROR_NOTTY      (-1)
#define BM8563_OK               (0x00)
#define BM8563_ERR_LOW_VOLTAGE  (0x80)

/**
 * Platform I2C binding: function pointers plus an opaque handle (e.g. I2C peripheral index).
 * @c read / @c write return @ref BM8563_OK on success or a negative platform error code.
 */
typedef struct {
    int32_t (* read)(void *handle, uint8_t address, uint8_t reg, uint8_t *buffer, uint16_t size);
    int32_t (* write)(void *handle, uint8_t address, uint8_t reg, uint8_t *buffer, uint16_t size);
    void *handle;
} bm8563_t;

typedef int32_t bm8563_err_t;

/** Clear control registers to a known idle state after power-up. */
bm8563_err_t bm8563_init(const bm8563_t *bm);
/** Read seven BCD time registers into @p time; may return @ref BM8563_ERR_LOW_VOLTAGE. */
bm8563_err_t bm8563_read(const bm8563_t *bm, struct tm *time);
/** Write calendar/time from @p time (BCD, century bit for years >= 2000). */
bm8563_err_t bm8563_write(const bm8563_t *bm, const struct tm *time);
/** Alarm set/read, timer, or raw CSR access depending on @p command. */
bm8563_err_t bm8563_ioctl(const bm8563_t *bm, int16_t command, void *buffer);
/** Optional teardown hook; default implementation is a no-op. */
bm8563_err_t bm8563_close(const bm8563_t *bm);

#ifdef __cplusplus
}
#endif
#endif
