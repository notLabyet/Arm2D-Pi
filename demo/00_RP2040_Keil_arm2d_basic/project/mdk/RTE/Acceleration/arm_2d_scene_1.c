/*
 * Copyright (c) 2009-2024 Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the License); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an AS IS BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*============================ INCLUDES ======================================*/

#define __USER_SCENE_1_IMPLEMENT__
#include "arm_2d_scene_1.h"

#if defined(RTE_Acceleration_Arm_2D_Helper_PFB)

#ifdef RTE_Acceleration_Arm_2D_Scene1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "drv_QMI8658.h"
#include "fal.h"
#include "f_util.h"
#include "qmi8658_motion.h"
#include "rp2040_sdcard.h"
#include "rp2040_flash_layout.h"

#if defined(__clang__)
#   pragma clang diagnostic push
#   pragma clang diagnostic ignored "-Wunknown-warning-option"
#   pragma clang diagnostic ignored "-Wreserved-identifier"
#   pragma clang diagnostic ignored "-Wsign-conversion"
#   pragma clang diagnostic ignored "-Wpadded"
#   pragma clang diagnostic ignored "-Wcast-qual"
#   pragma clang diagnostic ignored "-Wcast-align"
#   pragma clang diagnostic ignored "-Wmissing-field-initializers"
#   pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#   pragma clang diagnostic ignored "-Wmissing-prototypes"
#   pragma clang diagnostic ignored "-Wunused-variable"
#   pragma clang diagnostic ignored "-Wgnu-statement-expression"
#   pragma clang diagnostic ignored "-Wdeclaration-after-statement"
#   pragma clang diagnostic ignored "-Wunused-function"
#   pragma clang diagnostic ignored "-Wmissing-declarations"
#   pragma clang diagnostic ignored "-Wimplicit-int-conversion"
#elif __IS_COMPILER_ARM_COMPILER_5__
#   pragma diag_suppress 64,177
#elif __IS_COMPILER_IAR__
#   pragma diag_suppress=Pa089,Pe188,Pe177,Pe174
#elif __IS_COMPILER_GCC__
#   pragma GCC diagnostic push
#   pragma GCC diagnostic ignored "-Wformat="
#   pragma GCC diagnostic ignored "-Wpedantic"
#   pragma GCC diagnostic ignored "-Wunused-function"
#   pragma GCC diagnostic ignored "-Wunused-variable"
#   pragma GCC diagnostic ignored "-Wincompatible-pointer-types"
#endif

/*============================ MACROS ========================================*/

#define SCENE_1_BACKGROUND_VIEW_WIDTH              240
#define SCENE_1_BACKGROUND_VIEW_HEIGHT             230
#define SCENE_1_PARALLAX_Q4_SHIFT                  4
#define SCENE_1_PARALLAX_SPRING_GAIN               1
#define SCENE_1_PARALLAX_DAMPING_SHIFT             1
#define SCENE_1_PARALLAX_RESPONSE_DIV              4
#define SCENE_1_TAP_POLL_INTERVAL_MS               20u
#define SCENE_1_INPUT_DEADZONE                      2
#define SCENE_1_INPUT_SOFT_LIMIT                    24
#define SCENE_1_GYRO_INPUT_GAIN                     2

#ifndef SCENE_1_SABER_JPG_FILE_PATH
#   define SCENE_1_SABER_JPG_FILE_PATH              "saber2.jpg"
#endif

#define SCENE_1_SABER_FRAME_WIDTH                   180
#define SCENE_1_SABER_FRAME_HEIGHT                  180
#define SCENE_1_SABER_FRAME_COLUMNS                 12
#define SCENE_1_SABER_FRAME_COUNT                   100
#define SCENE_1_SABER_FRAME_PERIOD_MS               100
#define SCENE_1_SABER_CACHE_PARTITION               "pic"
#define SCENE_1_SABER_CACHE_MAGIC                   0x31524253u
#define SCENE_1_SABER_CACHE_VERSION                 1u
#define SCENE_1_SABER_CACHE_HEADER_SIZE             256u
#define SCENE_1_SABER_CACHE_DATA_OFFSET             SCENE_1_SABER_CACHE_HEADER_SIZE
#define SCENE_1_SABER_CACHE_SHEET_WIDTH             \
            SCENE_1_SABER_FRAME_WIDTH
#define SCENE_1_SABER_CACHE_FRAME_COLUMNS           1
#define SCENE_1_SABER_CACHE_SHEET_HEIGHT            \
            (SCENE_1_SABER_FRAME_HEIGHT * SCENE_1_SABER_FRAME_COUNT)
#define SCENE_1_SABER_CACHE_FRAME_SIZE              \
            (SCENE_1_SABER_FRAME_WIDTH * SCENE_1_SABER_FRAME_HEIGHT * 2u)
#define SCENE_1_SABER_CACHE_DATA_SIZE               \
            (SCENE_1_SABER_CACHE_FRAME_SIZE * SCENE_1_SABER_FRAME_COUNT)
#define SCENE_1_SABER_CACHE_TOTAL_SIZE              \
            (SCENE_1_SABER_CACHE_DATA_OFFSET + SCENE_1_SABER_CACHE_DATA_SIZE)
#define SCENE_1_SABER_CACHE_STRIPE_HEIGHT           16
#define SCENE_1_SABER_CACHE_STRIPE_SIZE             \
            (SCENE_1_SABER_FRAME_WIDTH * SCENE_1_SABER_CACHE_STRIPE_HEIGHT * 2u)
#define SCENE_1_FALLBACK_FRAME_WIDTH                180
#define SCENE_1_FALLBACK_FRAME_HEIGHT               180
#define SCENE_1_FALLBACK_FRAME_COLUMNS              12
#define SCENE_1_FALLBACK_FRAME_COUNT                100
#define SCENE_1_FALLBACK_FRAME_PERIOD_MS            100

/*============================ MACROFIED FUNCTIONS ===========================*/

#undef this
#define this (*ptThis)

/*============================ TYPES =========================================*/

enum {
    SCENE_1_SABER_CACHE_STATE_NONE = 0,
    SCENE_1_SABER_CACHE_STATE_BUILDING,
    SCENE_1_SABER_CACHE_STATE_READY,
    SCENE_1_SABER_CACHE_STATE_FAILED,
};

typedef struct scene_1_saber_cache_header_t {
    uint32_t wMagic;
    uint16_t hwVersion;
    uint16_t hwHeaderSize;
    uint16_t hwFrameWidth;
    uint16_t hwFrameHeight;
    uint16_t hwSourceFrameColumns;
    uint16_t hwCacheFrameColumns;
    uint16_t hwFrameCount;
    uint16_t hwSheetWidth;
    uint16_t hwSheetHeight;
    uint32_t wFrameSize;
    uint32_t wDataSize;
    uint32_t wSourceFileSize;
    uint32_t wReserved[8];
} scene_1_saber_cache_header_t;

/*============================ GLOBAL VARIABLES ==============================*/

//extern const arm_2d_tile_t c_tilebackgroundRGB565;
//extern const arm_2d_tile_t c_tilegirlRGB565;
//extern const arm_2d_tile_t c_tilegirlMask;

//extern const arm_2d_tile_t c_tilebg_darkRGB565;

/*============================ PROTOTYPES ====================================*/
/*============================ IMPLEMENTATION ================================*/

static int16_t __scene_1_clamp_i16(int16_t value, int16_t min, int16_t max)
{
    if (value < min) {
        return min;
    }

    if (value > max) {
        return max;
    }

    return value;
}

static int16_t __scene_1_abs_i16(int16_t value)
{
    return (value >= 0) ? value : (int16_t)-value;
}

static int16_t __scene_1_select_horizontal_gyro_offset(int16_t iGyroOffsetX,
                                                       int16_t iGyroOffsetY,
                                                       int16_t iGyroOffsetZ)
{
    int16_t iHorizontalOffset = iGyroOffsetZ;

    if (__scene_1_abs_i16(iGyroOffsetX) >
        __scene_1_abs_i16(iHorizontalOffset)) {
        iHorizontalOffset = iGyroOffsetX;
    }

    if (__scene_1_abs_i16(iGyroOffsetY) >
        __scene_1_abs_i16(iHorizontalOffset)) {
        iHorizontalOffset = iGyroOffsetY;
    }

    return iHorizontalOffset;
}

static int16_t __scene_1_soft_curve_i16(int16_t value)
{
    int32_t sign = (value < 0) ? -1 : 1;
    int32_t magnitude = value;

    if (magnitude < 0) {
        magnitude = -magnitude;
    }

    if (magnitude <= SCENE_1_INPUT_DEADZONE) {
        return 0;
    }

    magnitude -= SCENE_1_INPUT_DEADZONE;
    magnitude = (magnitude * magnitude) / 18 + magnitude;

    if (magnitude > SCENE_1_INPUT_SOFT_LIMIT) {
        magnitude = SCENE_1_INPUT_SOFT_LIMIT;
    }

    return (int16_t)(sign * magnitude);
}

#if __USER_SCENE_1_USE_SD_JPG__
static bool __scene_1_probe_saber_jpg(user_scene_1_t *ptThis)
{
    FIL tFile;
    FRESULT tResult;

    this.iSaberFileStatus = -1;
    this.wSaberFileSize = 0;

    tResult = rp2040_sdcard_mount();
    if (FR_OK != tResult) {
        this.iSaberFileStatus = (int16_t)tResult;
        return false;
    }

    tResult = f_open(&tFile, SCENE_1_SABER_JPG_FILE_PATH, FA_READ);
    if (FR_OK != tResult) {
        this.iSaberFileStatus = (int16_t)tResult;
        return false;
    }

    this.wSaberFileSize = (uint32_t)f_size(&tFile);
    (void)f_close(&tFile);
    this.iSaberFileStatus = 0;

    return this.wSaberFileSize > 0u;
}

static bool __scene_1_init_saber_jpg(user_scene_1_t *ptThis)
{
    arm_2d_err_t err;
#if __USER_SCENE_1_USE_ZJPGD__
    arm_zjpgd_loader_cfg_t tCFG;
#else
    arm_tjpgd_loader_cfg_t tCFG;
#endif

    if (!__scene_1_probe_saber_jpg(ptThis)) {
        return false;
    }

    err = arm_loader_io_fatfs_init(&this.tSaberJPGFile,
                                   SCENE_1_SABER_JPG_FILE_PATH);
    if (ARM_2D_ERR_NONE != err) {
        printf("Scene 1 JPG FatFs IO init failed: %d\r\n", err);
        return false;
    }

    memset(&tCFG, 0, sizeof(tCFG));
    tCFG.bUseHeapForVRES = false;
    tCFG.ptScene = (arm_2d_scene_t *)ptThis;
#if __USER_SCENE_1_USE_ZJPGD__
    tCFG.u2WorkMode = ARM_ZJPGD_MODE_PARTIAL_DECODED;
#else
    tCFG.u2WorkMode = ARM_TJPGD_MODE_PARTIAL_DECODED;
#endif
    tCFG.ImageIO.ptIO = &ARM_LOADER_IO_FATFS;
    tCFG.ImageIO.pTarget = (uintptr_t)&this.tSaberJPGFile;

#if __USER_SCENE_1_USE_ZJPGD__
    arm_zjpgd_loader_init(&this.tSaberJPG, &tCFG);
#else
    arm_tjpgd_loader_init(&this.tSaberJPG, &tCFG);
#endif

    this.tSaberJPGFilm = (arm_2d_helper_film_t)
                    impl_film(this.tSaberJPG,
                              SCENE_1_SABER_FRAME_WIDTH,
                              SCENE_1_SABER_FRAME_HEIGHT,
                              SCENE_1_SABER_FRAME_COLUMNS,
                              SCENE_1_SABER_FRAME_COUNT,
                              SCENE_1_SABER_FRAME_PERIOD_MS);
    this.tFilm = this.tSaberJPGFilm;

    printf("Scene 1 JPG ready: %s frame=%dx%d cols=%u count=%u\r\n",
           SCENE_1_SABER_JPG_FILE_PATH,
           SCENE_1_SABER_FRAME_WIDTH,
           SCENE_1_SABER_FRAME_HEIGHT,
           (unsigned)SCENE_1_SABER_FRAME_COLUMNS,
           (unsigned)SCENE_1_SABER_FRAME_COUNT);

    return true;
}

static scene_1_saber_cache_header_t __scene_1_make_saber_cache_header(
                                                            user_scene_1_t *ptThis)
{
    scene_1_saber_cache_header_t tHeader;

    memset(&tHeader, 0, sizeof(tHeader));
    tHeader.wMagic = SCENE_1_SABER_CACHE_MAGIC;
    tHeader.hwVersion = SCENE_1_SABER_CACHE_VERSION;
    tHeader.hwHeaderSize = SCENE_1_SABER_CACHE_HEADER_SIZE;
    tHeader.hwFrameWidth = SCENE_1_SABER_FRAME_WIDTH;
    tHeader.hwFrameHeight = SCENE_1_SABER_FRAME_HEIGHT;
    tHeader.hwSourceFrameColumns = SCENE_1_SABER_FRAME_COLUMNS;
    tHeader.hwCacheFrameColumns = SCENE_1_SABER_CACHE_FRAME_COLUMNS;
    tHeader.hwFrameCount = SCENE_1_SABER_FRAME_COUNT;
    tHeader.hwSheetWidth = SCENE_1_SABER_CACHE_SHEET_WIDTH;
    tHeader.hwSheetHeight = SCENE_1_SABER_CACHE_SHEET_HEIGHT;
    tHeader.wFrameSize = SCENE_1_SABER_CACHE_FRAME_SIZE;
    tHeader.wDataSize = SCENE_1_SABER_CACHE_DATA_SIZE;
    tHeader.wSourceFileSize = this.wSaberFileSize;

    return tHeader;
}

static bool __scene_1_saber_cache_header_match(
                                    const scene_1_saber_cache_header_t *ptHeader,
                                    const scene_1_saber_cache_header_t *ptExpected)
{
    return (NULL != ptHeader) &&
           (NULL != ptExpected) &&
           (ptHeader->wMagic == ptExpected->wMagic) &&
           (ptHeader->hwVersion == ptExpected->hwVersion) &&
           (ptHeader->hwHeaderSize == ptExpected->hwHeaderSize) &&
           (ptHeader->hwFrameWidth == ptExpected->hwFrameWidth) &&
           (ptHeader->hwFrameHeight == ptExpected->hwFrameHeight) &&
           (ptHeader->hwSourceFrameColumns == ptExpected->hwSourceFrameColumns) &&
           (ptHeader->hwCacheFrameColumns == ptExpected->hwCacheFrameColumns) &&
           (ptHeader->hwFrameCount == ptExpected->hwFrameCount) &&
           (ptHeader->hwSheetWidth == ptExpected->hwSheetWidth) &&
           (ptHeader->hwSheetHeight == ptExpected->hwSheetHeight) &&
           (ptHeader->wFrameSize == ptExpected->wFrameSize) &&
           (ptHeader->wDataSize == ptExpected->wDataSize) &&
           (ptHeader->wSourceFileSize == ptExpected->wSourceFileSize);
}

static void __scene_1_use_saber_cache(user_scene_1_t *ptThis)
{
    uintptr_t pchFrameData =
        (uintptr_t)(RP2040_FLASH_XIP_BASE +
                    (uint32_t)this.ptSaberCachePartition->offset +
                    SCENE_1_SABER_CACHE_DATA_OFFSET);

    this.tSaberCacheTile = (arm_2d_tile_t) {
        .tRegion = {
            .tSize = {
                .iWidth = SCENE_1_SABER_CACHE_SHEET_WIDTH,
                .iHeight = SCENE_1_SABER_CACHE_SHEET_HEIGHT,
            },
        },
        .tInfo = {
            .bIsRoot = true,
            .bHasEnforcedColour = true,
            .tColourInfo = {
                .chScheme = ARM_2D_COLOUR_RGB565,
            },
        },
        .phwBuffer = (uint16_t *)pchFrameData,
    };

    this.tFilm = (arm_2d_helper_film_t)
                    impl_film(this.tSaberCacheTile,
                              SCENE_1_SABER_FRAME_WIDTH,
                              SCENE_1_SABER_FRAME_HEIGHT,
                              SCENE_1_SABER_CACHE_FRAME_COLUMNS,
                              SCENE_1_SABER_FRAME_COUNT,
                              SCENE_1_SABER_FRAME_PERIOD_MS);
    this.bSaberCacheReady = true;
    this.chSaberCacheState = SCENE_1_SABER_CACHE_STATE_READY;
}

static bool __scene_1_prepare_saber_cache(user_scene_1_t *ptThis)
{
    scene_1_saber_cache_header_t tHeader;
    scene_1_saber_cache_header_t tExpected;

    this.ptSaberCachePartition = fal_partition_find(SCENE_1_SABER_CACHE_PARTITION);
    if (NULL == this.ptSaberCachePartition) {
        printf("Scene 1 FAL partition %s not found\r\n",
               SCENE_1_SABER_CACHE_PARTITION);
        this.chSaberCacheState = SCENE_1_SABER_CACHE_STATE_FAILED;
        return false;
    }

    this.wSaberCacheTotalSize = SCENE_1_SABER_CACHE_TOTAL_SIZE;
    if (this.ptSaberCachePartition->len < this.wSaberCacheTotalSize) {
        printf("Scene 1 FAL pic too small: need %lu have %lu\r\n",
               (unsigned long)this.wSaberCacheTotalSize,
               (unsigned long)this.ptSaberCachePartition->len);
        this.chSaberCacheState = SCENE_1_SABER_CACHE_STATE_FAILED;
        return false;
    }

    tExpected = __scene_1_make_saber_cache_header(ptThis);
    if (sizeof(tHeader) !=
        (size_t)fal_partition_read(this.ptSaberCachePartition,
                                   0,
                                   (uint8_t *)&tHeader,
                                   sizeof(tHeader))) {
        this.chSaberCacheState = SCENE_1_SABER_CACHE_STATE_BUILDING;
        return false;
    }

    if (__scene_1_saber_cache_header_match(&tHeader, &tExpected)) {
        __scene_1_use_saber_cache(ptThis);
        printf("Scene 1 FAL saber cache hit: %lu bytes\r\n",
               (unsigned long)this.wSaberCacheTotalSize);
        return true;
    }

    this.chSaberCacheState = SCENE_1_SABER_CACHE_STATE_BUILDING;
    this.phwSaberCacheStripeBuffer =
        (uint16_t *)malloc(SCENE_1_SABER_CACHE_STRIPE_SIZE);
    if (NULL == this.phwSaberCacheStripeBuffer) {
        printf("Scene 1 FAL saber cache buffer alloc failed: %lu bytes\r\n",
               (unsigned long)SCENE_1_SABER_CACHE_STRIPE_SIZE);
        this.chSaberCacheState = SCENE_1_SABER_CACHE_STATE_FAILED;
        return false;
    }
    return false;
}

static bool __scene_1_saber_cache_write_header(user_scene_1_t *ptThis,
                                               bool bValid)
{
    scene_1_saber_cache_header_t tHeader =
        __scene_1_make_saber_cache_header(ptThis);

    if (!bValid) {
        tHeader.wMagic = 0u;
    }

    return sizeof(tHeader) ==
           (size_t)fal_partition_write(this.ptSaberCachePartition,
                                       0,
                                       (const uint8_t *)&tHeader,
                                       sizeof(tHeader));
}

static bool __scene_1_saber_cache_build_one_frame(user_scene_1_t *ptThis)
{
    uint16_t hwStripeY = this.hwSaberCacheStripeY;
    uint16_t hwStripeHeight = SCENE_1_SABER_CACHE_STRIPE_HEIGHT;
    arm_2d_tile_t tTargetTile = {
        .tRegion = {
            .tSize = {
                .iWidth = SCENE_1_SABER_FRAME_WIDTH,
                .iHeight = SCENE_1_SABER_CACHE_STRIPE_HEIGHT,
            },
        },
        .tInfo = {
            .bIsRoot = true,
            .bHasEnforcedColour = true,
            .tColourInfo = {
                .chScheme = ARM_2D_COLOUR_RGB565,
            },
        },
        .phwBuffer = this.phwSaberCacheStripeBuffer,
    };
    arm_2d_region_t tTargetRegion = {
        .tLocation = { 0, 0 },
        .tSize = {
            .iWidth = SCENE_1_SABER_FRAME_WIDTH,
            .iHeight = SCENE_1_SABER_CACHE_STRIPE_HEIGHT,
        },
    };
    uint32_t wOffset;
    uint16_t hwFrame = this.hwSaberCacheFrameIndex;

    if ((NULL == this.ptSaberCachePartition) ||
        (NULL == this.phwSaberCacheStripeBuffer) ||
        !this.bSaberJPGReady) {
        this.chSaberCacheState = SCENE_1_SABER_CACHE_STATE_FAILED;
        return false;
    }

    if (0u == hwFrame) {
        if (!__scene_1_saber_cache_write_header(ptThis, false)) {
            free(this.phwSaberCacheStripeBuffer);
            this.phwSaberCacheStripeBuffer = NULL;
            this.chSaberCacheState = SCENE_1_SABER_CACHE_STATE_FAILED;
            return false;
        }
    }

    if (hwFrame >= SCENE_1_SABER_FRAME_COUNT) {
        if (!__scene_1_saber_cache_write_header(ptThis, true)) {
            free(this.phwSaberCacheStripeBuffer);
            this.phwSaberCacheStripeBuffer = NULL;
            this.chSaberCacheState = SCENE_1_SABER_CACHE_STATE_FAILED;
            return false;
        }
        __scene_1_use_saber_cache(ptThis);
        free(this.phwSaberCacheStripeBuffer);
        this.phwSaberCacheStripeBuffer = NULL;
        printf("Scene 1 FAL saber cache complete: %u frames\r\n",
               (unsigned)SCENE_1_SABER_FRAME_COUNT);
        return true;
    }

    if ((hwStripeY + hwStripeHeight) > SCENE_1_SABER_FRAME_HEIGHT) {
        hwStripeHeight = SCENE_1_SABER_FRAME_HEIGHT - hwStripeY;
        tTargetTile.tRegion.tSize.iHeight = (int16_t)hwStripeHeight;
        tTargetRegion.tSize.iHeight = (int16_t)hwStripeHeight;
    }

    arm_2d_helper_film_set_frame(&this.tSaberJPGFilm, hwFrame);
    {
        arm_2d_tile_t tSourceFrame =
            *(const arm_2d_tile_t *)&this.tSaberJPGFilm;
        arm_2d_tile_t tSourceStripe =
            impl_child_tile(tSourceFrame,
                            0,
                            hwStripeY,
                            SCENE_1_SABER_FRAME_WIDTH,
                            hwStripeHeight);

        arm_2d_tile_copy_only(&tSourceStripe,
                              &tTargetTile,
                              &tTargetRegion);
    }
    ARM_2D_OP_WAIT_ASYNC();

    wOffset = SCENE_1_SABER_CACHE_DATA_OFFSET +
              ((uint32_t)hwFrame * SCENE_1_SABER_CACHE_FRAME_SIZE) +
              ((uint32_t)hwStripeY * SCENE_1_SABER_FRAME_WIDTH * 2u);

    if ((uint32_t)(SCENE_1_SABER_FRAME_WIDTH * hwStripeHeight * 2u) !=
        (uint32_t)fal_partition_write(this.ptSaberCachePartition,
                                      (long)wOffset,
                                      (const uint8_t *)this.phwSaberCacheStripeBuffer,
                                      SCENE_1_SABER_FRAME_WIDTH *
                                          hwStripeHeight * 2u)) {
        printf("Scene 1 FAL saber cache write failed at frame %u y=%u\r\n",
               (unsigned)hwFrame,
               (unsigned)hwStripeY);
        free(this.phwSaberCacheStripeBuffer);
        this.phwSaberCacheStripeBuffer = NULL;
        this.chSaberCacheState = SCENE_1_SABER_CACHE_STATE_FAILED;
        return false;
    }

    this.hwSaberCacheStripeY += hwStripeHeight;
    if (this.hwSaberCacheStripeY >= SCENE_1_SABER_FRAME_HEIGHT) {
        this.hwSaberCacheStripeY = 0;
        this.hwSaberCacheFrameIndex++;
    }

    return true;
}
#endif

static void __scene_1_update_parallax(user_scene_1_t *ptThis)
{
    int32_t const nTargetXQ4 =
        ((int32_t)__scene_1_soft_curve_i16(this.iTargetOffsetX)
         << SCENE_1_PARALLAX_Q4_SHIFT);
    int32_t const nTargetYQ4 =
        ((int32_t)__scene_1_soft_curve_i16(this.iTargetOffsetY)
         << SCENE_1_PARALLAX_Q4_SHIFT);
    int32_t const nDeltaXQ4 = nTargetXQ4 - this.nParallaxOffsetXQ4;
    int32_t const nDeltaYQ4 = nTargetYQ4 - this.nParallaxOffsetYQ4;

    this.nParallaxVelocityXQ4 +=
        (nDeltaXQ4 * SCENE_1_PARALLAX_SPRING_GAIN) /
        SCENE_1_PARALLAX_RESPONSE_DIV;
    this.nParallaxVelocityYQ4 +=
        (nDeltaYQ4 * SCENE_1_PARALLAX_SPRING_GAIN) /
        SCENE_1_PARALLAX_RESPONSE_DIV;

    this.nParallaxVelocityXQ4 -=
        this.nParallaxVelocityXQ4 >> SCENE_1_PARALLAX_DAMPING_SHIFT;
    this.nParallaxVelocityYQ4 -=
        this.nParallaxVelocityYQ4 >> SCENE_1_PARALLAX_DAMPING_SHIFT;

    this.nParallaxOffsetXQ4 += this.nParallaxVelocityXQ4;
    this.nParallaxOffsetYQ4 += this.nParallaxVelocityYQ4;

    this.iOffsetX =
        (int16_t)(this.nParallaxOffsetXQ4 >> SCENE_1_PARALLAX_Q4_SHIFT);
    this.iOffsetY =
        (int16_t)(this.nParallaxOffsetYQ4 >> SCENE_1_PARALLAX_Q4_SHIFT);
}


static void __on_scene_1_load(arm_2d_scene_t *ptScene)
{
    user_scene_1_t *ptThis = (user_scene_1_t *)ptScene;

    this.chOpacity = 0u;
    this.bLightOn = false;
    this.iTargetOffsetX = 0;
    this.iTargetOffsetY = 0;
    this.iOffsetX = 0;
    this.iOffsetY = 0;
    this.nParallaxOffsetXQ4 = 0;
    this.nParallaxOffsetYQ4 = 0;
    this.nParallaxVelocityXQ4 = 0;
    this.nParallaxVelocityYQ4 = 0;
    qmi8658_motion_reset_position(0, 0);

#if __USER_SCENE_1_USE_SD_JPG__
    if (this.bSaberJPGReady && !this.bSaberCacheReady) {
    #if __USER_SCENE_1_USE_ZJPGD__
        arm_zjpgd_loader_on_load(&this.tSaberJPG);
    #else
        arm_tjpgd_loader_on_load(&this.tSaberJPG);
    #endif
    }
#endif

#if __USER_SCENE_1_USE_LMSK__
    ARM_LMSK_GROUP_ON_LOAD();
#endif
}

static void __on_scene_1_depose(arm_2d_scene_t *ptScene)
{
    user_scene_1_t *ptThis = (user_scene_1_t *)ptScene;

#if __USER_SCENE_1_USE_LMSK__
    ARM_LMSK_GROUP_DEPOSE();
#endif

#if __USER_SCENE_1_USE_SD_JPG__
    if (NULL != this.phwSaberCacheStripeBuffer) {
        free(this.phwSaberCacheStripeBuffer);
        this.phwSaberCacheStripeBuffer = NULL;
    }

    if (this.bSaberJPGReady) {
    #if __USER_SCENE_1_USE_ZJPGD__
        arm_zjpgd_loader_depose(&this.tSaberJPG);
    #else
        arm_tjpgd_loader_depose(&this.tSaberJPG);
    #endif
    }
#endif

    arm_foreach(int64_t, this.lTimestamp, ptItem) {
        *ptItem = 0;
    }

    ptScene->ptPlayer = NULL;
    if (!this.bUserAllocated) {
        __arm_2d_free_scratch_memory(ARM_2D_MEM_TYPE_UNSPECIFIED, ptScene);
    }
}

static void __on_scene_1_frame_start(arm_2d_scene_t *ptScene)
{
    user_scene_1_t *ptThis = (user_scene_1_t *)ptScene;
    int32_t nOpacity = 0;
    int16_t iGyroOffsetX = 0;
    int16_t iGyroOffsetY = 0;
    int16_t iGyroOffsetZ = 0;
    qmi8658_tap_status_t tTapStatus;

    (void)qmi8658_motion_get_gyro_offset_xyz(&iGyroOffsetX,
                                             &iGyroOffsetY,
                                             &iGyroOffsetZ);
    this.iTargetOffsetX =
        (int16_t)(__scene_1_select_horizontal_gyro_offset(iGyroOffsetX,
                                                          iGyroOffsetY,
                                                          iGyroOffsetZ) *
                  SCENE_1_GYRO_INPUT_GAIN);
    this.iTargetOffsetY = 0;

    __scene_1_update_parallax(ptThis);

    if (arm_2d_helper_is_time_out(this.tFilm.hwPeriodPerFrame,
                                  &this.lTimestamp[0])) {
        arm_2d_helper_film_next_frame(&this.tFilm);
#if !__USER_SCENE_1_USE_SD_JPG__
        arm_2d_helper_film_next_frame(&this.tFilmMask);
#else
        if (!this.bSaberJPGReady) {
            arm_2d_helper_film_next_frame(&this.tFilmMask);
        }
#endif
        if (this.use_as__arm_2d_scene_t.bUseDirtyRegionHelper) {
            arm_2d_helper_dirty_region_item_suspend_update(
                &this.use_as__arm_2d_scene_t.tDirtyRegionHelper.tDefaultItem,
                false);
        }
        this.lTimestamp[0] = 0;
    } else {
        if (this.use_as__arm_2d_scene_t.bUseDirtyRegionHelper) {
            arm_2d_helper_dirty_region_item_suspend_update(
                &this.use_as__arm_2d_scene_t.tDirtyRegionHelper.tDefaultItem,
                true);
        }
    }

#if __USER_SCENE_1_USE_SD_JPG__
    if (this.bSaberJPGReady && !this.bSaberCacheReady) {
    #if __USER_SCENE_1_USE_ZJPGD__
        arm_zjpgd_loader_on_frame_start(&this.tSaberJPG);
    #else
        arm_tjpgd_loader_on_frame_start(&this.tSaberJPG);
    #endif
    }
#endif

    if (!this.bLightOn &&
        arm_2d_helper_is_time_out(SCENE_1_TAP_POLL_INTERVAL_MS,
                                  &this.lTimestamp[2])) {
        if (QMI8658A_ReadTapStatus(&tTapStatus) &&
            (tTapStatus.num != QMI8658_TAP_NUM_NONE) &&
            (tTapStatus.axis == QMI8658_TAP_AXIS_Z)) {
            this.bLightOn = true;
            this.lTimestamp[1] = 0;
        }
        this.lTimestamp[2] = 0;
    }

    if (this.bLightOn) {
        if (arm_2d_helper_time_half_cos_slider(0,
                                               255,
                                               1000,
                                               &nOpacity,
                                               &this.lTimestamp[1])) {
            this.chOpacity = 255u;
        } else {
            this.chOpacity = (uint8_t)nOpacity;
        }
    }

#if __USER_SCENE_1_USE_LMSK__
    ARM_LMSK_GROUP_ON_FRAME_START();
#endif
}

static void __on_scene_1_frame_complete(arm_2d_scene_t *ptScene)
{
    user_scene_1_t *ptThis = (user_scene_1_t *)ptScene;

#if __USER_SCENE_1_USE_SD_JPG__
    if (this.bSaberJPGReady && !this.bSaberCacheReady) {
    #if __USER_SCENE_1_USE_ZJPGD__
        arm_zjpgd_loader_on_frame_complete(&this.tSaberJPG);
    #else
        arm_tjpgd_loader_on_frame_complete(&this.tSaberJPG);
    #endif
    }
#else
    ARM_2D_UNUSED(ptThis);
#endif

#if __USER_SCENE_1_USE_LMSK__
    ARM_LMSK_GROUP_ON_FRAME_COMPLETE();
#endif
}

static
IMPL_PFB_ON_DRAW(__pfb_draw_scene_1_handler)
{
    user_scene_1_t *ptThis = (user_scene_1_t *)pTarget;

    arm_2d_canvas(ptTile, __top_canvas) {
#if __USER_SCENE_1_USE_SD_JPG__
        if (SCENE_1_SABER_CACHE_STATE_BUILDING == this.chSaberCacheState) {
            if (bIsNewFrame) {
                (void)__scene_1_saber_cache_build_one_frame(ptThis);
            }

            arm_2d_fill_colour(ptTile, &__top_canvas, GLCD_COLOR_BLACK);

            arm_2d_dock_vertical(__top_canvas, 72, 0) {
                int32_t nProgress =
                    ((int32_t)this.hwSaberCacheFrameIndex * 100) /
                    SCENE_1_SABER_FRAME_COUNT;
                arm_2d_region_t tBarOuter = {
                    .tLocation = {
                        .iX = 30,
                        .iY = (int16_t)(__vertical_region.tLocation.iY + 40),
                    },
                    .tSize = {
                        .iWidth = 180,
                        .iHeight = 10,
                    },
                };
                arm_2d_region_t tBarInner = tBarOuter;

                tBarInner.tLocation.iX += 1;
                tBarInner.tLocation.iY += 1;
                tBarInner.tSize.iWidth =
                    (int16_t)(((tBarOuter.tSize.iWidth - 2) * nProgress) / 100);
                tBarInner.tSize.iHeight -= 2;

                arm_lcd_text_set_target_framebuffer((arm_2d_tile_t *)ptTile);
                arm_lcd_text_set_font(&ARM_2D_FONT_6x8.use_as__arm_2d_font_t);
                arm_lcd_text_set_draw_region(&__vertical_region);
                arm_lcd_text_set_colour(GLCD_COLOR_WHITE, GLCD_COLOR_BLACK);
                arm_lcd_text_location(4, 2);
                arm_lcd_printf("Caching JPG %03u/%03u",
                               (unsigned)this.hwSaberCacheFrameIndex,
                               (unsigned)SCENE_1_SABER_FRAME_COUNT);
                arm_lcd_text_location(10, 4);
                arm_lcd_printf("%ld%%", (long)nProgress);

                arm_2d_fill_colour(ptTile, &tBarOuter, GLCD_COLOR_DARK_GREY);
                if (tBarInner.tSize.iWidth > 0) {
                    arm_2d_fill_colour(ptTile, &tBarInner, GLCD_COLOR_GREEN);
                }
            }

            ARM_2D_OP_WAIT_ASYNC();
            return arm_fsm_rt_cpl;
        }
#else
        ARM_2D_UNUSED(bIsNewFrame);
#endif

        if (
#if __USER_SCENE_1_USE_SD_JPG__
            this.bSaberJPGReady
#else
            false
#endif
        ) {
            arm_2d_fill_colour(ptTile, &__top_canvas, GLCD_COLOR_BLACK);
            arm_2d_align_centre(__top_canvas,
                                this.tFilm.use_as__arm_2d_tile_t.tRegion.tSize) {
                arm_2d_region_t tFilmRegion = {
                    .tLocation = __centre_region.tLocation,
                    .tSize = this.tFilm.use_as__arm_2d_tile_t.tRegion.tSize,
                };

                arm_2d_tile_copy_only((const arm_2d_tile_t *)&this.tFilm,
                                      ptTile,
                                      &tFilmRegion);

                if (this.use_as__arm_2d_scene_t.bUseDirtyRegionHelper) {
                    arm_2d_helper_dirty_region_update_item(
                        &this.use_as__arm_2d_scene_t.tDirtyRegionHelper.tDefaultItem,
                        (arm_2d_tile_t *)ptTile,
                        &__top_canvas,
                        &tFilmRegion);
                }
            }
        }
    }

    ARM_2D_OP_WAIT_ASYNC();

    return arm_fsm_rt_cpl;
}

ARM_NONNULL(1)
user_scene_1_t *__arm_2d_scene_1_init(arm_2d_scene_player_t *ptDispAdapter,
                                      user_scene_1_t *ptThis)
{
    bool bUserAllocated = false;

    assert(NULL != ptDispAdapter);

    if (NULL == ptThis) {
        ptThis = (user_scene_1_t *)
                    __arm_2d_allocate_scratch_memory(sizeof(user_scene_1_t),
                                                     __alignof__(user_scene_1_t),
                                                     ARM_2D_MEM_TYPE_UNSPECIFIED);
        assert(NULL != ptThis);
        if (NULL == ptThis) {
            return NULL;
        }
    } else {
        bUserAllocated = true;
    }

    memset(ptThis, 0, sizeof(user_scene_1_t));

    *ptThis = (user_scene_1_t) {
        .use_as__arm_2d_scene_t = {
            .tCanvas = { GLCD_COLOR_BLACK },
            .fnOnLoad = &__on_scene_1_load,
            .fnScene = &__pfb_draw_scene_1_handler,
            .fnOnFrameStart = &__on_scene_1_frame_start,
            .fnOnFrameCPL = &__on_scene_1_frame_complete,
            .fnDepose = &__on_scene_1_depose,
            .bUseDirtyRegionHelper = false,
        },
        .bUserAllocated = bUserAllocated,
    };

#if __USER_SCENE_1_USE_LMSK__
    ARM_LMSK_ITEM_INIT_WITH_ROM(SCENE_1_LMSK_CMSIS, c_lmskCMSISLogo, 1881);
#endif

#if __USER_SCENE_1_USE_SD_JPG__
    this.bSaberJPGReady = __scene_1_init_saber_jpg(ptThis);
    if (this.bSaberJPGReady) {
        (void)__scene_1_prepare_saber_cache(ptThis);
    }

#else
    __scene_1_init_fallback_animation(ptThis);
#endif

    arm_2d_scene_player_append_scenes(ptDispAdapter,
                                      &this.use_as__arm_2d_scene_t,
                                      1);

    return ptThis;
}

ARM_NONNULL(1)
user_scene_1_t *__arm_2d_scene1_init(arm_2d_scene_player_t *ptDispAdapter,
                                     user_scene_1_t *ptThis)
{
    return __arm_2d_scene_1_init(ptDispAdapter, ptThis);
}

#if defined(__clang__)
#   pragma clang diagnostic pop
#endif

#endif

#endif
