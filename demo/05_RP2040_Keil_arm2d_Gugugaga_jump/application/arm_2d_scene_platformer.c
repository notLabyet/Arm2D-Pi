/*
 * Copyright (c) 2009-2024 Arm Limited. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*============================ INCLUDES ======================================*/

#include "arm_2d.h"

#if defined(RTE_Acceleration_Arm_2D_Helper_PFB)

#define __USER_SCENE_PLATFORMER_IMPLEMENT__
#include "arm_2d_scenes.h"

#include "arm_2d_helper.h"
#include "arm_2d_scene_platformer.h"
#include "qmi8658_motion.h"
#include "power_key_service.h"

#include <stdlib.h>
#include <string.h>

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
#   pragma GCC diagnostic ignored "-Wunused-value"
#endif

/*============================ MACROS ========================================*/
/*============================ MACROFIED FUNCTIONS ===========================*/

#undef this
#define this (*ptThis)

#define PLATFORMER_WALK_FRAME_COUNT              8u
#define PLATFORMER_WALK_FRAME_WIDTH              48
#define PLATFORMER_WALK_FRAME_HEIGHT             64
#define PLATFORMER_WALK_FRAME_PERIOD_MS           100
#define PLATFORMER_IDLE_FRAME_COUNT              12u
#define PLATFORMER_IDLE_FRAME_PERIOD_MS           160
#define PLATFORMER_JUMP_FRAME_WIDTH                64
#define PLATFORMER_GLIDE_FRAME_WIDTH               96
#define PLATFORMER_GLIDE_FRAME_COUNT                9u
#define PLATFORMER_GLIDE_INTRO_FRAME_COUNT          3u
#define PLATFORMER_GLIDE_FRAME_PERIOD_MS           100
#define PLATFORMER_GROUND_HEIGHT                 42
/* Compensate for the two transparent rows below the sprite feet. */
#define PLATFORMER_PLAYER_DRAW_OFFSET_Y           2
#define PLATFORMER_PLAYER_HITBOX_OFFSET_X         PLATFORMER_GAME_BODY_OFFSET_X
#define PLATFORMER_PLAYER_HITBOX_OFFSET_Y         8
#define PLATFORMER_PLAYER_HITBOX_WIDTH            PLATFORMER_GAME_BODY_WIDTH
#define PLATFORMER_PLAYER_HITBOX_HEIGHT           PLATFORMER_GAME_BODY_HEIGHT
/* The motion service integrates cpx * 14 per 20 ms, i.e. cpx * 7 px/s. */
#define PLATFORMER_GRASS_FRAME_WIDTH              33
#define PLATFORMER_GRASS_FRAME_HEIGHT             21
#define PLATFORMER_STONE_FRAME_WIDTH              48
#define PLATFORMER_STONE_FRAME_HEIGHT             31
#define PLATFORMER_FOREGROUND_MAX_FRAME_WIDTH                         \
            PLATFORMER_STONE_FRAME_WIDTH
#define PLATFORMER_GRASS_FRAME_COUNT              12u
#define PLATFORMER_STONE_FRAME_COUNT               8u
#define PLATFORMER_FOREGROUND_GRASS                0u
#define PLATFORMER_FOREGROUND_STONE                1u
#define PLATFORMER_STONE_INTERVAL                  5u
#define PLATFORMER_GRASS_DEPTH_RANGE                7u
#define PLATFORMER_STONE_DEPTH_RANGE                4u
#define PLATFORMER_FOREGROUND_GAP_MIN               28
#define PLATFORMER_FOREGROUND_GAP_RANGE             33u
#define PLATFORMER_FOREGROUND_LEFT_RETAIN            96
#define PLATFORMER_FOREGROUND_PRELOAD               192
#define PLATFORMER_CAMERA_FOLLOW_X                120
#define PLATFORMER_WORLD_REBASE_X                12000
#ifndef PLATFORMER_SHOW_DEBUG_INFO
#   define PLATFORMER_SHOW_DEBUG_INFO 0
#endif
#define PLATFORMER_MOTION_POSITION_MAX           30000
#define PLATFORMER_MOTION_POSITION_CENTRE        15000
#define PLATFORMER_MID_FAR_PERIOD_WIDTH             720
#define PLATFORMER_MID_FAR_SOURCE_WIDTH            1040
#define PLATFORMER_MID_FAR_LAYER_HEIGHT            114
#define PLATFORMER_MID_FAR_MASK_HEIGHT               32
#define PLATFORMER_MID_FAR_PARALLAX_DIVISOR           8
#define PLATFORMER_CLOUD_SOURCE_WIDTH               1040
/* The opaque far layer begins at Y=116; the last two cloud rows are hidden. */
#define PLATFORMER_CLOUD_LAYER_HEIGHT                 60
#define PLATFORMER_CLOUD_LAYER_Y                      56
#define PLATFORMER_CLOUD_PARALLAX_DIVISOR             24
#define PLATFORMER_FOREGROUND_DIRTY_HEIGHT                           \
            (PLATFORMER_GRASS_FRAME_HEIGHT                           \
            + PLATFORMER_GRASS_DEPTH_RANGE - 1u)
#define PLATFORMER_FOREGROUND_RANDOM_SEED  UINT32_C(0x4D595DF4)

/*============================ TYPES =========================================*/

enum {
    PLATFORMER_ANIMATION_IDLE = 0,
    PLATFORMER_ANIMATION_WALK,
    PLATFORMER_ANIMATION_JUMP,
    PLATFORMER_ANIMATION_GLIDE,
};

enum {
    PLATFORMER_DIRTY_REGION_PLAYER = 0,
    PLATFORMER_DIRTY_REGION_CLOUD,
    PLATFORMER_DIRTY_REGION_MID_FAR,
    PLATFORMER_DIRTY_REGION_FOREGROUND,
    PLATFORMER_DIRTY_REGION_HUD,
    PLATFORMER_DIRTY_REGION_FPS,
    PLATFORMER_DIRTY_REGION_COMBO,
    PLATFORMER_DIRTY_REGION_COURSE,
};

/*============================ GLOBAL VARIABLES ==============================*/

extern const arm_2d_tile_t c_tilePlatformerHeroWalkRGB565;
extern const arm_2d_tile_t c_tilePlatformerHeroWalkMask;
extern const arm_2d_tile_t c_tilePlatformerHeroIdleRGB565;
extern const arm_2d_tile_t c_tilePlatformerHeroIdleMask;
extern const arm_2d_tile_t c_tilePlatformerHeroJumpRGB565;
extern const arm_2d_tile_t c_tilePlatformerHeroJumpMask;
extern const arm_2d_tile_t c_tilePlatformerHeroGlideRGB565;
extern const arm_2d_tile_t c_tilePlatformerHeroGlideMask;
extern const arm_2d_tile_t c_tilePlatformerGrassRGB565;
extern const arm_2d_tile_t c_tilePlatformerGrassMask;
extern const arm_2d_tile_t c_tilePlatformerStoneRGB565;
extern const arm_2d_tile_t c_tilePlatformerStoneMask;
extern const arm_2d_tile_t c_tilePlatformerCookieRGB565;
extern const arm_2d_tile_t c_tilePlatformerCookieMask;
extern const arm_2d_tile_t c_tilePlatformerComboRGB565;
extern const arm_2d_tile_t c_tilePlatformerComboMask;
extern const arm_2d_tile_t c_tilePlatformerCloudRGB565;
extern const arm_2d_tile_t c_tilePlatformerMidFarRGB565;
extern const arm_2d_tile_t c_tilePlatformerMidFarMask;

/* One viewport of the immutable far layer, shared by serial scene rendering.
 * Keep frequently sampled RGB565 and A8 in SRAM instead of single-bit XIP.
 * The 32 KB scene heap remains reserved separately by the scatter file. */
#define PLATFORMER_MID_FAR_CACHE_WIDTH 320
static uint16_t s_hwMidFarCache[PLATFORMER_MID_FAR_LAYER_HEIGHT]
                               [PLATFORMER_MID_FAR_CACHE_WIDTH];
static uint8_t s_chMidFarCacheMask[PLATFORMER_MID_FAR_MASK_HEIGHT]
                                 [PLATFORMER_MID_FAR_CACHE_WIDTH];
static arm_2d_tile_t s_tMidFarCacheTile, s_tMidFarCacheMask;
static int16_t s_iMidFarCacheX;
static bool s_bMidFarCacheValid;

static void __platformer_update_mid_far_cache(int16_t iScrollX)
{
    int16_t iAdvance = iScrollX - s_iMidFarCacheX;
    if (s_bMidFarCacheValid && iAdvance == 0) {
        return;
    }
    /* A wrap, jump or first use reloads the viewport. Ordinary movement
     * retains the overlapping columns and reads only the exposed edge. */
    int16_t iKeep = s_bMidFarCacheValid && iAdvance > 0
                 && iAdvance < PLATFORMER_MID_FAR_CACHE_WIDTH
                 ? PLATFORMER_MID_FAR_CACHE_WIDTH - iAdvance : 0;
    size_t nNew = PLATFORMER_MID_FAR_CACHE_WIDTH - iKeep;
    for (unsigned y = 0; y < PLATFORMER_MID_FAR_LAYER_HEIGHT; y++) {
        if (iKeep) {
            memmove(s_hwMidFarCache[y], &s_hwMidFarCache[y][iAdvance],
                      (size_t)iKeep * sizeof(uint16_t));
        }
        memcpy(&s_hwMidFarCache[y][iKeep],
               &c_tilePlatformerMidFarRGB565.phwBuffer[
                   y * PLATFORMER_MID_FAR_SOURCE_WIDTH + iScrollX + iKeep],
               nNew * sizeof(uint16_t));
        if (y < PLATFORMER_MID_FAR_MASK_HEIGHT) {
            if (iKeep) {
                memmove(s_chMidFarCacheMask[y], &s_chMidFarCacheMask[y][iAdvance],
                          (size_t)iKeep);
            }
            memcpy(&s_chMidFarCacheMask[y][iKeep],
                   &c_tilePlatformerMidFarMask.pchBuffer[
                       y * PLATFORMER_MID_FAR_SOURCE_WIDTH + iScrollX + iKeep],
                   nNew);
        }
    }
    s_tMidFarCacheTile = c_tilePlatformerMidFarRGB565;
    s_tMidFarCacheTile.tRegion = (arm_2d_region_t){
        .tSize = {PLATFORMER_MID_FAR_CACHE_WIDTH, PLATFORMER_MID_FAR_LAYER_HEIGHT},
    };
    s_tMidFarCacheTile.phwBuffer = &s_hwMidFarCache[0][0];
    s_tMidFarCacheMask = c_tilePlatformerMidFarMask;
    s_tMidFarCacheMask.tRegion = (arm_2d_region_t){
        .tSize = {PLATFORMER_MID_FAR_CACHE_WIDTH, PLATFORMER_MID_FAR_MASK_HEIGHT},
    };
    s_tMidFarCacheMask.pchBuffer = &s_chMidFarCacheMask[0][0];
    s_iMidFarCacheX = iScrollX;
    s_bMidFarCacheValid = true;
}

static uint16_t s_hwCloudCache[PLATFORMER_CLOUD_LAYER_HEIGHT]
                              [PLATFORMER_MID_FAR_CACHE_WIDTH];
static arm_2d_tile_t s_tCloudCacheTile;
static int16_t s_iCloudCacheX;
static bool s_bCloudCacheValid;

static void __platformer_update_cloud_cache(int16_t iScrollX)
{
    int16_t iAdvance = iScrollX - s_iCloudCacheX;
    if (s_bCloudCacheValid && iAdvance == 0) {
        return;
    }
    int16_t iKeep = s_bCloudCacheValid && iAdvance > 0
                 && iAdvance < PLATFORMER_MID_FAR_CACHE_WIDTH
                 ? PLATFORMER_MID_FAR_CACHE_WIDTH - iAdvance : 0;
    size_t nNew = PLATFORMER_MID_FAR_CACHE_WIDTH - iKeep;
    for (unsigned y = 0; y < PLATFORMER_CLOUD_LAYER_HEIGHT; y++) {
        if (iKeep) {
            memmove(s_hwCloudCache[y], &s_hwCloudCache[y][iAdvance],
                      (size_t)iKeep * sizeof(uint16_t));
        }
        memcpy(&s_hwCloudCache[y][iKeep],
               &c_tilePlatformerCloudRGB565.phwBuffer[
                   y * PLATFORMER_CLOUD_SOURCE_WIDTH + iScrollX + iKeep],
               nNew * sizeof(uint16_t));
    }
    s_tCloudCacheTile = c_tilePlatformerCloudRGB565;
    s_tCloudCacheTile.tRegion = (arm_2d_region_t){
        .tSize = {PLATFORMER_MID_FAR_CACHE_WIDTH, PLATFORMER_CLOUD_LAYER_HEIGHT},
    };
    s_tCloudCacheTile.phwBuffer = &s_hwCloudCache[0][0];
    s_iCloudCacheX = iScrollX;
    s_bCloudCacheValid = true;
}

/*============================ PROTOTYPES ====================================*/

static void __platformer_set_animation_frame(user_scene_platformer_t *ptThis,
                                              uint8_t chAnimation,
                                              uint8_t u8Frame);
static void __platformer_reset_foreground(user_scene_platformer_t *ptThis);
static void __platformer_update_foreground(user_scene_platformer_t *ptThis);
static void __platformer_rebase_world(user_scene_platformer_t *ptThis);
static void __platformer_draw_foreground(user_scene_platformer_t *ptThis,
                                         const arm_2d_tile_t *ptTarget,
                                         arm_2d_location_t tCanvasLocation,
                                         uint8_t chType);
static void __platformer_draw_parallax_layer(
                                         user_scene_platformer_t *ptThis,
                                         const arm_2d_tile_t *ptTarget,
                                         arm_2d_location_t tCanvasLocation,
                                         const arm_2d_tile_t *ptSource,
                                         int16_t iWidth,
                                         int16_t iHeight,
                                         int16_t iY,
                                         int16_t iScrollX);
static void __platformer_draw_mid_far_layer(
                                         user_scene_platformer_t *ptThis,
                                         const arm_2d_tile_t *ptTarget,
                                         arm_2d_location_t tCanvasLocation);

/*============================ LOCAL VARIABLES ===============================*/
/*============================ IMPLEMENTATION ================================*/

static uint32_t __platformer_random_next(uint32_t *pwState)
{
    *pwState = (*pwState * UINT32_C(1664525)) + UINT32_C(1013904223);
    return *pwState;
}

static void __platformer_update_foreground(user_scene_platformer_t *ptThis)
{
    int32_t lReleaseX = this.lCameraWorldX
                      - PLATFORMER_FOREGROUND_LEFT_RETAIN;
    int32_t lGenerateUntilX = this.lCameraWorldX
                            + this.tGround.tSize.iWidth
                            + PLATFORMER_FOREGROUND_PRELOAD;

    for (uint_fast8_t n = 0; n < PLATFORMER_FOREGROUND_ITEM_COUNT; n++) {
        if (this.bForegroundActive[n]
        && ((this.lForegroundX[n] + PLATFORMER_FOREGROUND_MAX_FRAME_WIDTH)
                < lReleaseX)) {
            this.bForegroundActive[n] = false;
        }
    }

    while (this.lNextForegroundX <= lGenerateUntilX) {
        uint_fast8_t n;
        uint8_t chType;
        uint8_t chFrameCount;
        uint8_t chDepthRange;
        int16_t iFrameHeight;
        int16_t iDepth;

        for (n = 0; n < PLATFORMER_FOREGROUND_ITEM_COUNT; n++) {
            if (!this.bForegroundActive[n]) {
                break;
            }
        }
        if (n >= PLATFORMER_FOREGROUND_ITEM_COUNT) {
            break;
        }

        chType = (0u == ((this.wForegroundSequence + 1u)
                         % PLATFORMER_STONE_INTERVAL))
               ? PLATFORMER_FOREGROUND_STONE
               : PLATFORMER_FOREGROUND_GRASS;
        chFrameCount = (PLATFORMER_FOREGROUND_GRASS == chType)
                     ? PLATFORMER_GRASS_FRAME_COUNT
                     : PLATFORMER_STONE_FRAME_COUNT;
        chDepthRange = (PLATFORMER_FOREGROUND_GRASS == chType)
                     ? PLATFORMER_GRASS_DEPTH_RANGE
                     : PLATFORMER_STONE_DEPTH_RANGE;
        iFrameHeight = (PLATFORMER_FOREGROUND_GRASS == chType)
                     ? PLATFORMER_GRASS_FRAME_HEIGHT
                     : PLATFORMER_STONE_FRAME_HEIGHT;
        iDepth = (int16_t)((__platformer_random_next(
                                &this.wForegroundRandomState) >> 8)
                           % chDepthRange);

        this.lForegroundX[n] = this.lNextForegroundX;
        this.iForegroundY[n] = this.tGround.tLocation.iY
                             - iFrameHeight
                             + iDepth;
        if (PLATFORMER_FOREGROUND_STONE == chType) {
            this.iForegroundY[n]
                += iFrameHeight / 2;
        }
        this.chForegroundTypes[n] = chType;
        this.chForegroundFrames[n] = (uint8_t)(
            (__platformer_random_next(&this.wForegroundRandomState) >> 8)
            % chFrameCount);
        this.bForegroundActive[n] = true;
        this.wForegroundSequence++;
        this.lNextForegroundX += PLATFORMER_FOREGROUND_GAP_MIN
                              + (int32_t)(
            (__platformer_random_next(&this.wForegroundRandomState) >> 8)
            % PLATFORMER_FOREGROUND_GAP_RANGE);
    }
}

static void __platformer_reset_foreground(user_scene_platformer_t *ptThis)
{
    memset(this.bForegroundActive, 0, sizeof(this.bForegroundActive));
    this.wForegroundRandomState = PLATFORMER_FOREGROUND_RANDOM_SEED;
    this.wForegroundSequence = 0;
    this.lNextForegroundX = (int32_t)(
        (__platformer_random_next(&this.wForegroundRandomState) >> 8)
        % (PLATFORMER_FOREGROUND_GAP_MIN + 1));
    __platformer_update_foreground(ptThis);
}

static void __platformer_rebase_world(user_scene_platformer_t *ptThis)
{
    int32_t lOffset;

    if (this.lCameraWorldX < PLATFORMER_WORLD_REBASE_X) {
        return;
    }

    lOffset = this.lCameraWorldX;
    this.lPlayerWorldX -= lOffset;
    platformer_game_rebase(&this.tGame, lOffset);
    this.lNextForegroundX -= lOffset;
    for (uint_fast8_t n = 0; n < PLATFORMER_FOREGROUND_ITEM_COUNT; n++) {
        if (this.bForegroundActive[n]) {
            this.lForegroundX[n] -= lOffset;
        }
    }
    this.lCameraWorldX = 0;
}

static void __platformer_draw_parallax_layer(
                                         user_scene_platformer_t *ptThis,
                                         const arm_2d_tile_t *ptTarget,
                                         arm_2d_location_t tCanvasLocation,
                                         const arm_2d_tile_t *ptSource,
                                         int16_t iWidth,
                                         int16_t iHeight,
                                         int16_t iY,
                                         int16_t iScrollX)
{
    arm_2d_region_t tRegion = {
        .tLocation = {
            .iX = tCanvasLocation.iX - iScrollX,
            .iY = tCanvasLocation.iY + iY,
        },
        .tSize = {
            .iWidth = iWidth,
            .iHeight = iHeight,
        },
    };

    if (arm_2d_helper_pfb_is_region_being_drawing(ptTarget, &tRegion, NULL)) {
        arm_2d_tile_copy_only(ptSource, ptTarget, &tRegion);
        ARM_2D_OP_WAIT_ASYNC();
    }
}

static void __platformer_draw_mid_far_layer(
                                         user_scene_platformer_t *ptThis,
                                         const arm_2d_tile_t *ptTarget,
                                         arm_2d_location_t tCanvasLocation)
{
    arm_2d_region_t tTopSourceRegion = {
        .tSize = {
            .iWidth = PLATFORMER_MID_FAR_CACHE_WIDTH,
            .iHeight = PLATFORMER_MID_FAR_MASK_HEIGHT,
        },
    };
    arm_2d_region_t tTopTargetRegion = {
        .tLocation = {
            .iX = tCanvasLocation.iX,
            .iY = tCanvasLocation.iY
                + this.tGround.tLocation.iY
                - PLATFORMER_MID_FAR_LAYER_HEIGHT,
        },
        .tSize = {
            .iWidth = PLATFORMER_MID_FAR_CACHE_WIDTH,
            .iHeight = PLATFORMER_MID_FAR_MASK_HEIGHT,
        },
    };
    arm_2d_region_t tBottomSourceRegion = {
        .tLocation = {
            .iY = PLATFORMER_MID_FAR_MASK_HEIGHT,
        },
        .tSize = {
            .iWidth = PLATFORMER_MID_FAR_CACHE_WIDTH,
            .iHeight = PLATFORMER_MID_FAR_LAYER_HEIGHT
                     - PLATFORMER_MID_FAR_MASK_HEIGHT,
        },
    };
    arm_2d_region_t tBottomTargetRegion = {
        .tLocation = {
            .iX = tTopTargetRegion.tLocation.iX,
            .iY = tTopTargetRegion.tLocation.iY
                + PLATFORMER_MID_FAR_MASK_HEIGHT,
        },
        .tSize = tBottomSourceRegion.tSize,
    };

    if (arm_2d_helper_pfb_is_region_being_drawing(ptTarget,
                                           &tTopTargetRegion,
                                           NULL)) {
        arm_2d_tile_t tTopSourceTile;
        arm_2d_tile_t tTopMaskTile;

        (void)arm_2d_tile_generate_child(&s_tMidFarCacheTile,
                                         &tTopSourceRegion,
                                         &tTopSourceTile,
                                         false);
        (void)arm_2d_tile_generate_child(&s_tMidFarCacheMask,
                                         &tTopSourceRegion,
                                         &tTopMaskTile,
                                         false);
        tTopSourceTile.tInfo.bDerivedResource = true;
        tTopMaskTile.tInfo.bDerivedResource = true;
        arm_2d_tile_copy_with_src_mask_only(&tTopSourceTile,
                                            &tTopMaskTile,
                                            ptTarget,
                                            &tTopTargetRegion);
        ARM_2D_OP_WAIT_ASYNC();
    }

    if (arm_2d_helper_pfb_is_region_being_drawing(ptTarget,
                                           &tBottomTargetRegion,
                                           NULL)) {
        arm_2d_tile_t tBottomSourceTile;

        (void)arm_2d_tile_generate_child(&s_tMidFarCacheTile,
                                         &tBottomSourceRegion,
                                         &tBottomSourceTile,
                                         false);
        tBottomSourceTile.tInfo.bDerivedResource = true;
        arm_2d_tile_copy_only(&tBottomSourceTile,
                              ptTarget,
                              &tBottomTargetRegion);
        ARM_2D_OP_WAIT_ASYNC();
    }
}

static void __platformer_draw_foreground(user_scene_platformer_t *ptThis,
                                         const arm_2d_tile_t *ptTarget,
                                         arm_2d_location_t tCanvasLocation,
                                         uint8_t chType)
{
    const arm_2d_tile_t *ptSource = (PLATFORMER_FOREGROUND_STONE == chType)
                                  ? &c_tilePlatformerStoneRGB565
                                  : &c_tilePlatformerGrassRGB565;
    const arm_2d_tile_t *ptMask = (PLATFORMER_FOREGROUND_STONE == chType)
                                ? &c_tilePlatformerStoneMask
                                : &c_tilePlatformerGrassMask;
    int16_t iFrameWidth = (PLATFORMER_FOREGROUND_STONE == chType)
                        ? PLATFORMER_STONE_FRAME_WIDTH
                        : PLATFORMER_GRASS_FRAME_WIDTH;
    int16_t iFrameHeight = (PLATFORMER_FOREGROUND_STONE == chType)
                         ? PLATFORMER_STONE_FRAME_HEIGHT
                         : PLATFORMER_GRASS_FRAME_HEIGHT;
    int16_t iDrawOffsetX = (PLATFORMER_FOREGROUND_MAX_FRAME_WIDTH
                          - iFrameWidth) / 2;

    for (uint_fast8_t n = 0; n < PLATFORMER_FOREGROUND_ITEM_COUNT; n++) {
        int32_t lScreenX;
        arm_2d_tile_t tPropTile;
        arm_2d_tile_t tPropMask;
        arm_2d_region_t tFrame;
        arm_2d_region_t tPropRegion;

        if (!this.bForegroundActive[n]
        ||  (chType != this.chForegroundTypes[n])) {
            continue;
        }

        lScreenX = this.lForegroundX[n]
                 - this.lCameraWorldX
                 + iDrawOffsetX;
        if ((lScreenX <= -iFrameWidth)
        ||  (lScreenX >= this.tGround.tSize.iWidth)) {
            continue;
        }

        tFrame = (arm_2d_region_t){
            .tLocation = {
                .iX = (int16_t)(this.chForegroundFrames[n]
                              * iFrameWidth),
                .iY = 0,
            },
            .tSize = {
                .iWidth = iFrameWidth,
                .iHeight = iFrameHeight,
            },
        };
        if (PLATFORMER_FOREGROUND_STONE == chType) {
            int16_t iVisibleHeight = this.tGround.tLocation.iY
                                   - this.iForegroundY[n];
            if (iVisibleHeight <= 0) {
                continue;
            }
            if (iVisibleHeight < tFrame.tSize.iHeight) {
                tFrame.tSize.iHeight = iVisibleHeight;
            }
        }
        tPropRegion = (arm_2d_region_t){
            .tLocation = {
                .iX = tCanvasLocation.iX + (int16_t)lScreenX,
                .iY = tCanvasLocation.iY
                    + this.iForegroundY[n],
            },
            .tSize = tFrame.tSize,
        };

        if (!arm_2d_helper_pfb_is_region_being_drawing(ptTarget,
                                                &tPropRegion,
                                                NULL)) {
            continue;
        }
        (void)arm_2d_tile_generate_child(ptSource,
                                          &tFrame,
                                          &tPropTile,
                                          false);
        (void)arm_2d_tile_generate_child(ptMask,
                                          &tFrame,
                                          &tPropMask,
                                          false);
        tPropTile.tInfo.bDerivedResource = true;
        tPropMask.tInfo.bDerivedResource = true;
        arm_2d_tile_copy_with_src_mask_only(&tPropTile,
                                            &tPropMask,
                                            ptTarget,
                                            &tPropRegion);
        ARM_2D_OP_WAIT_ASYNC();
    }
}

static arm_2d_region_t __platformer_get_player_hit_box(
                                        arm_2d_location_t tPlayerLocation)
{
    return (arm_2d_region_t){
        .tLocation = {
            .iX = tPlayerLocation.iX + PLATFORMER_PLAYER_HITBOX_OFFSET_X,
            .iY = tPlayerLocation.iY + PLATFORMER_PLAYER_HITBOX_OFFSET_Y,
        },
        .tSize = {
            .iWidth = PLATFORMER_PLAYER_HITBOX_WIDTH,
            .iHeight = PLATFORMER_PLAYER_HITBOX_HEIGHT,
        },
    };
}

static void __platformer_set_animation_frame(user_scene_platformer_t *ptThis,
                                              uint8_t chAnimation,
                                              uint8_t u8Frame)
{
    const arm_2d_tile_t *ptSource = (PLATFORMER_ANIMATION_WALK == chAnimation)
                                  ? &c_tilePlatformerHeroWalkRGB565
                                  : &c_tilePlatformerHeroIdleRGB565;
    const arm_2d_tile_t *ptMask = (PLATFORMER_ANIMATION_WALK == chAnimation)
                                ? &c_tilePlatformerHeroWalkMask
                                : &c_tilePlatformerHeroIdleMask;
    int16_t iFrameWidth = PLATFORMER_ANIMATION_GLIDE == chAnimation
                         ? PLATFORMER_GLIDE_FRAME_WIDTH
                         : PLATFORMER_ANIMATION_JUMP == chAnimation
                           ? PLATFORMER_JUMP_FRAME_WIDTH : PLATFORMER_WALK_FRAME_WIDTH;
    arm_2d_region_t tFrameRegion = {
        .tLocation = {
            .iX = (int16_t)(u8Frame * iFrameWidth),
            .iY = 0,
        },
        .tSize = {
            .iWidth = iFrameWidth,
            .iHeight = PLATFORMER_WALK_FRAME_HEIGHT,
        },
    };

    if (PLATFORMER_ANIMATION_JUMP == chAnimation) {
        ptSource = &c_tilePlatformerHeroJumpRGB565;
        ptMask = &c_tilePlatformerHeroJumpMask;
    }
    if (PLATFORMER_ANIMATION_GLIDE == chAnimation) {
        ptSource = &c_tilePlatformerHeroGlideRGB565;
        ptMask = &c_tilePlatformerHeroGlideMask;
    }
    this.chPlayingAnimation = chAnimation;
    this.u8AnimationFrame = u8Frame;
    (void)arm_2d_tile_generate_child(ptSource,
                                      &tFrameRegion,
                                      &this.tHeroTile,
                                      false);
    (void)arm_2d_tile_generate_child(ptMask,
                                      &tFrameRegion,
                                      &this.tHeroMask,
                                      false);
    this.tHeroTile.tInfo.bDerivedResource = true;
    this.tHeroMask.tInfo.bDerivedResource = true;
}

static void __platformer_reset_player(user_scene_platformer_t *ptThis)
{
    uint32_t wNowMs = (uint32_t)arm_2d_helper_convert_ticks_to_ms(
                                    arm_2d_helper_get_system_timestamp());

    qmi8658_motion_reset_position(PLATFORMER_MOTION_POSITION_CENTRE, 0);
    this.lPlayerWorldX = this.tPlayerStartLocation.iX
                       - this.tPlayfield.tLocation.iX;
    platformer_game_init(&this.tGame, this.tGround.tLocation.iY,
                          (int16_t)this.lPlayerWorldX, wNowMs);
    platformer_speed_control_init(&this.tSpeedControl);
    this.lCameraWorldX = 0;
    this.iMidFarScrollX = 0;
    this.iCloudScrollX = 0;
    this.chMidFarScrollRemainder = 0;
    this.chCloudScrollRemainder = 0;
    this.tPlayerLocation = this.tPlayerStartLocation;
    this.tPlayerHitBox = __platformer_get_player_hit_box(
                                                this.tPlayerLocation);
    this.bWalking = false;
    this.wFPSWindowMs = wNowMs;
    (void)power_key_service_consume_press();
    power_key_service_set_airborne(false);
    this.lAnimationTimestamp = 0;
    __platformer_set_animation_frame(ptThis, PLATFORMER_ANIMATION_IDLE, 0);
}

static void __platformer_update_player(user_scene_platformer_t *ptThis)
{
    int16_t iTiltDeg10 = 0;
    int16_t iSpeedPps;
    int32_t lCameraTarget;
    int32_t lCameraAdvance;
    int32_t lCloudAdvance;
    int32_t lMidFarAdvance;
    uint32_t wNowMs = (uint32_t)arm_2d_helper_convert_ticks_to_ms(
                                    arm_2d_helper_get_system_timestamp());

    bool bMotionReady = qmi8658_motion_get_tilt_angle_deg10(&iTiltDeg10);
    iSpeedPps = platformer_speed_control_update(&this.tSpeedControl,
                    iTiltDeg10, bMotionReady, wNowMs - this.tGame.wLastUpdateMs);
    this.bWalking = iSpeedPps > 0;

    this.bRawJumpHeld = power_key_service_is_raw_pressed();
    this.tGame.bGlideHeld = power_key_service_is_pressed();
    this.tGame.bGlideQualified = power_key_service_glide_ready();
    platformer_game_update(&this.tGame, wNowMs, iSpeedPps,
                             power_key_service_consume_press());
    power_key_service_set_airborne(!this.tGame.bGrounded);
    this.lPlayerWorldX = this.tGame.lXQ8 / PLATFORMER_GAME_Q8;
    lCameraTarget = this.lPlayerWorldX - PLATFORMER_CAMERA_FOLLOW_X;
    if (lCameraTarget > this.lCameraWorldX) {
        lCameraAdvance = lCameraTarget - this.lCameraWorldX;
        this.lCameraWorldX = lCameraTarget;
        lMidFarAdvance = lCameraAdvance + this.chMidFarScrollRemainder;
        this.iMidFarScrollX = (int16_t)(
            (this.iMidFarScrollX
                + (lMidFarAdvance / PLATFORMER_MID_FAR_PARALLAX_DIVISOR))
            % PLATFORMER_MID_FAR_PERIOD_WIDTH);
        this.chMidFarScrollRemainder = (uint8_t)(
            lMidFarAdvance % PLATFORMER_MID_FAR_PARALLAX_DIVISOR);
        lCloudAdvance = lCameraAdvance + this.chCloudScrollRemainder;
        this.iCloudScrollX = (int16_t)(
            (this.iCloudScrollX
                + (lCloudAdvance / PLATFORMER_CLOUD_PARALLAX_DIVISOR))
            % PLATFORMER_MID_FAR_PERIOD_WIDTH);
        this.chCloudScrollRemainder = (uint8_t)(
            lCloudAdvance % PLATFORMER_CLOUD_PARALLAX_DIVISOR);
    }
    this.tPlayerLocation.iX = this.tPlayfield.tLocation.iX + (int16_t)(
        this.lPlayerWorldX - this.lCameraWorldX);
    this.tPlayerLocation.iY = (int16_t)(this.tGame.lFootYQ8 / PLATFORMER_GAME_Q8)
                             - PLATFORMER_WALK_FRAME_HEIGHT;
    this.tPlayerHitBox = __platformer_get_player_hit_box(this.tPlayerLocation);
    __platformer_rebase_world(ptThis);
}

static void __platformer_update_animation(user_scene_platformer_t *ptThis)
{
    uint8_t chAnimation = this.bWalking
                          ? PLATFORMER_ANIMATION_WALK
                          : PLATFORMER_ANIMATION_IDLE;
    uint8_t u8FrameCount = this.bWalking
                         ? PLATFORMER_WALK_FRAME_COUNT
                         : PLATFORMER_IDLE_FRAME_COUNT;
    uint32_t wFramePeriod = this.bWalking
                          ? PLATFORMER_WALK_FRAME_PERIOD_MS
                          : PLATFORMER_IDLE_FRAME_PERIOD_MS;

    int8_t chJumpFrame = platformer_game_jump_frame(&this.tGame);

    if (this.tGame.bGliding) {
        chAnimation = PLATFORMER_ANIMATION_GLIDE;
        u8FrameCount = PLATFORMER_GLIDE_FRAME_COUNT;
        wFramePeriod = PLATFORMER_GLIDE_FRAME_PERIOD_MS;
    } else if (chJumpFrame >= 0) {
        if ((PLATFORMER_ANIMATION_JUMP != this.chPlayingAnimation)
        ||  (chJumpFrame != this.u8AnimationFrame)) {
            __platformer_set_animation_frame(ptThis,
                                              PLATFORMER_ANIMATION_JUMP,
                                              (uint8_t)chJumpFrame);
        }
        return;
    }

    if (chAnimation != this.chPlayingAnimation) {
        this.lAnimationTimestamp = (PLATFORMER_ANIMATION_GLIDE == chAnimation)
                                   ? arm_2d_helper_get_system_timestamp()
                                   : 0;
        __platformer_set_animation_frame(ptThis, chAnimation, 0);
        return;
    }

    if (arm_2d_helper_is_time_out(wFramePeriod,
                                  &this.lAnimationTimestamp)) {
        uint8_t u8NextFrame = this.u8AnimationFrame + 1u;

        if (u8NextFrame >= u8FrameCount) {
            /* Play airflow onset once; keep the developed flow looping. */
            u8NextFrame = (PLATFORMER_ANIMATION_GLIDE == chAnimation)
                          ? PLATFORMER_GLIDE_INTRO_FRAME_COUNT
                          : 0;
        }
        __platformer_set_animation_frame(ptThis, chAnimation, u8NextFrame);
    }
}

static void __on_scene_platformer_depose(arm_2d_scene_t *ptScene)
{
    user_scene_platformer_t *ptThis = (user_scene_platformer_t *)ptScene;

    power_key_service_set_airborne(false);
    arm_2d_helper_dirty_region_remove_items(
        &this.use_as__arm_2d_scene_t.tDirtyRegionHelper,
        this.tDirtyRegionItems,
        dimof(this.tDirtyRegionItems));
    ptScene->ptPlayer = NULL;
    if (!this.bUserAllocated) {
        __arm_2d_free_scratch_memory(ARM_2D_MEM_TYPE_UNSPECIFIED, ptScene);
    }
}

static void __on_scene_platformer_load(arm_2d_scene_t *ptScene)
{
    user_scene_platformer_t *ptThis = (user_scene_platformer_t *)ptScene;

    arm_2d_helper_dirty_region_add_items(
        &this.use_as__arm_2d_scene_t.tDirtyRegionHelper,
        this.tDirtyRegionItems,
        dimof(this.tDirtyRegionItems));
    for (uint_fast8_t n = 0; n < dimof(this.tDirtyRegionItems); n++) {
        arm_2d_helper_dirty_region_item_force_to_use_minimal_enclosure(
            &this.tDirtyRegionItems[n], true);
    }
    qmi8658_motion_set_bounds(PLATFORMER_MOTION_POSITION_MAX, 0);
    __platformer_reset_player(ptThis);
    __platformer_reset_foreground(ptThis);
}

static void __platformer_update_course_regions(user_scene_platformer_t *ptThis)
{
    this.hwCourseRegionMask = 0;
    for (uint_fast8_t n = 0; n < PLATFORMER_GAME_OBJECT_COUNT; n++) {
        const platformer_game_object_t *ptObject = &this.tGame.tObjects[n];
        arm_2d_region_t tOld = this.tCourseRegions[n];
        arm_2d_region_t tNew = {0};
        int32_t lLeft = ptObject->lX - this.lCameraWorldX;
        int32_t lRight = lLeft + ptObject->hwWidth;

        if (ptObject->bActive && lRight > 0
        &&  lLeft < this.tPlayfield.tSize.iWidth) {
            lLeft = MAX(0, lLeft);
            lRight = MIN(this.tPlayfield.tSize.iWidth, lRight);
            tNew = (arm_2d_region_t){
                .tLocation = {
                    .iX = this.tPlayfield.tLocation.iX + (int16_t)lLeft,
                    .iY = ptObject->iTop,
                },
                .tSize = {
                    .iWidth = (int16_t)(lRight - lLeft),
                    .iHeight = ptObject->chHeight,
                },
            };
        }
        this.tCourseRegions[n] = tNew;
        /* Keep a separate item per slot: collecting a cookie while stationary
         * must erase its old pixels, without refreshing the gap to another. */
        if (tOld.tSize.iWidth > 0 || tNew.tSize.iWidth > 0) {
            this.hwCourseRegionMask |= (uint16_t)(1u << n);
        }
        arm_2d_helper_dirty_region_item_suspend_update(
            &this.tDirtyRegionItems[PLATFORMER_DIRTY_REGION_COURSE + n],
            tOld.tLocation.iX == tNew.tLocation.iX
         && tOld.tLocation.iY == tNew.tLocation.iY
         && tOld.tSize.iWidth == tNew.tSize.iWidth
         && tOld.tSize.iHeight == tNew.tSize.iHeight);
    }
}

static void __on_scene_platformer_frame_start(arm_2d_scene_t *ptScene)
{
    user_scene_platformer_t *ptThis = (user_scene_platformer_t *)ptScene;
    arm_2d_location_t tOldPlayerLocation = this.tPlayerLocation;
    int32_t lOldCameraWorldX = this.lCameraWorldX;
    int16_t iOldCloudScrollX = this.iCloudScrollX;
    int16_t iOldMidFarScrollX = this.iMidFarScrollX;
    uint8_t chOldPlayingAnimation = this.chPlayingAnimation;
    uint8_t u8OldAnimationFrame = this.u8AnimationFrame;

    uint32_t wOldScore = platformer_game_score(&this.tGame);
    uint16_t hwOldCombo = this.tGame.hwCombo;
    uint8_t chOldComboPhase = this.chComboPhase;
    uint8_t chOldComboBar = this.chComboBar;
    bool bOldRawJumpHeld = this.bRawJumpHeld;
    bool bOldGlideHeld = this.tGame.bGlideHeld;
    bool bOldGliding = this.tGame.bGliding;
    uint32_t wOldProfileSequence = this.tDisplayProfile.sequence;
    this.tDisplayProfile = platform_display_profile_get();
    if (PLATFORMER_SHOW_DEBUG_INFO
    && wOldProfileSequence != this.tDisplayProfile.sequence) {
        power_key_service_get_audit(&this.tKeyAudit);
        power_key_service_get_pulse_ms(&this.hwKeyLowMs, &this.hwKeyHighMs);
        this.hwKeySampleGapMs = power_key_service_max_sample_gap_ms();
    }
    this.bFrameDrawn = false;
    __platformer_update_player(ptThis);
    /* Quantized feedback keeps the badge still between meaningful changes. */
    uint16_t hwComboAge = PLATFORMER_GAME_COMBO_WINDOW_MS
                         - this.tGame.hwComboRemainingMs;
    this.chComboPhase = hwComboAge < 80u ? 0u : hwComboAge < 160u ? 1u : 2u;
    this.chComboBar = (this.tGame.hwComboRemainingMs * 10u
                      + PLATFORMER_GAME_COMBO_WINDOW_MS - 1u)
                     / PLATFORMER_GAME_COMBO_WINDOW_MS;
    arm_2d_helper_dirty_region_item_suspend_update(
        &this.tDirtyRegionItems[PLATFORMER_DIRTY_REGION_COMBO],
        (hwOldCombo < 2u && this.tGame.hwCombo < 2u)
     || (hwOldCombo == this.tGame.hwCombo
      && chOldComboPhase == this.chComboPhase
      && chOldComboBar == this.chComboBar));
    uint32_t wCacheStartUs = platform_display_profile_timestamp();
    __platformer_update_mid_far_cache(this.iMidFarScrollX);
    __platformer_update_cloud_cache(this.iCloudScrollX);
    platform_display_profile_section(DISPLAY_PROFILE_CACHE, wCacheStartUs);
    __platformer_update_foreground(ptThis);
    __platformer_update_animation(ptThis);
    __platformer_update_course_regions(ptThis);

    arm_2d_helper_dirty_region_item_suspend_update(
        &this.tDirtyRegionItems[PLATFORMER_DIRTY_REGION_PLAYER],
        (tOldPlayerLocation.iX == this.tPlayerLocation.iX)
     && (tOldPlayerLocation.iY == this.tPlayerLocation.iY)
     && (chOldPlayingAnimation == this.chPlayingAnimation)
     && (u8OldAnimationFrame == this.u8AnimationFrame));
    arm_2d_helper_dirty_region_item_suspend_update(
        &this.tDirtyRegionItems[PLATFORMER_DIRTY_REGION_CLOUD],
        iOldCloudScrollX == this.iCloudScrollX);
    arm_2d_helper_dirty_region_item_suspend_update(
        &this.tDirtyRegionItems[PLATFORMER_DIRTY_REGION_MID_FAR],
        iOldMidFarScrollX == this.iMidFarScrollX);
    arm_2d_helper_dirty_region_item_suspend_update(
        &this.tDirtyRegionItems[PLATFORMER_DIRTY_REGION_FOREGROUND],
        lOldCameraWorldX == this.lCameraWorldX);
    arm_2d_helper_dirty_region_item_suspend_update(
        &this.tDirtyRegionItems[PLATFORMER_DIRTY_REGION_HUD],
        wOldScore == platformer_game_score(&this.tGame)
     && (!PLATFORMER_SHOW_DEBUG_INFO
         || (bOldRawJumpHeld == this.bRawJumpHeld
          && bOldGlideHeld == this.tGame.bGlideHeld
          && bOldGliding == this.tGame.bGliding)));
    arm_2d_helper_dirty_region_item_suspend_update(
        &this.tDirtyRegionItems[PLATFORMER_DIRTY_REGION_FPS],
        !PLATFORMER_SHOW_DEBUG_INFO
     || (this.hwDrawnFPS == this.hwFPS
      && wOldProfileSequence == this.tDisplayProfile.sequence));
    this.hwDrawnFPS = this.hwFPS;
}

static void __before_scene_platformer_switching_out(arm_2d_scene_t *ptScene)
{
    user_scene_platformer_t *ptThis = (user_scene_platformer_t *)ptScene;
    ARM_2D_UNUSED(ptThis);
}

static void __on_scene_platformer_frame_complete(arm_2d_scene_t *ptScene)
{
    user_scene_platformer_t *ptThis = (user_scene_platformer_t *)ptScene;
    uint32_t wNowMs = (uint32_t)arm_2d_helper_convert_ticks_to_ms(
                                    arm_2d_helper_get_system_timestamp());
    uint32_t wElapsedMs = wNowMs - this.wFPSWindowMs;

    if (this.bFrameDrawn && this.hwFrameCount < UINT16_MAX) {
        this.hwFrameCount++;
    }
    if (wElapsedMs >= 1000u) {
        this.hwFPS = (uint16_t)MIN(999u,
            ((uint32_t)this.hwFrameCount * 1000u + wElapsedMs / 2u) / wElapsedMs);
        this.hwFrameCount = 0;
        this.wFPSWindowMs = wNowMs;
    }
}

static void __platformer_draw_course(user_scene_platformer_t *ptThis,
                                     const arm_2d_tile_t *ptTile,
                                     arm_2d_location_t tCanvasLocation,
                                     uint8_t chType)
{
    for (uint_fast8_t n = 0; n < PLATFORMER_GAME_OBJECT_COUNT; n++) {
        const platformer_game_object_t *ptObject = &this.tGame.tObjects[n];
        int32_t lScreenX = ptObject->lX - this.lCameraWorldX;
        arm_2d_region_t tObject;
        arm_2d_region_t tDetail;

        if (!ptObject->bActive || ptObject->chType != chType
        ||  lScreenX + ptObject->hwWidth <= 0
        ||  lScreenX >= this.tPlayfield.tSize.iWidth) {
            continue;
        }
        tObject = (arm_2d_region_t){
            .tLocation = {
                .iX = tCanvasLocation.iX + this.tPlayfield.tLocation.iX
                    + (int16_t)lScreenX,
                .iY = tCanvasLocation.iY + ptObject->iTop,
            },
            .tSize = {
                .iWidth = (int16_t)ptObject->hwWidth,
                .iHeight = ptObject->chHeight,
            },
        };
        if (!arm_2d_helper_pfb_is_region_being_drawing(ptTile, &tObject, NULL)) {
            continue;
        }
        tDetail = tObject;
        if (PLATFORMER_GAME_PLATFORM == chType) {
            arm_2d_fill_colour(ptTile, &tObject, __RGB(0x48, 0x37, 0x29));
            ARM_2D_OP_WAIT_ASYNC();
            tDetail.tSize.iHeight = 4;
            arm_2d_fill_colour(ptTile, &tDetail, __RGB(0x98, 0xD6, 0x45));
            ARM_2D_OP_WAIT_ASYNC();
            tDetail.tLocation.iY += 4;
            tDetail.tSize.iHeight = 3;
            arm_2d_fill_colour(ptTile, &tDetail, __RGB(0x56, 0x8E, 0x26));
            ARM_2D_OP_WAIT_ASYNC();
        } else {
            arm_2d_tile_copy_with_src_mask_only(&c_tilePlatformerCookieRGB565,
                                                &c_tilePlatformerCookieMask,
                                                ptTile, &tObject);
            ARM_2D_OP_WAIT_ASYNC();
        }
    }
}

static void __platformer_draw_combo_part(const arm_2d_tile_t *ptTile,
                                         arm_2d_region_t tSource,
                                         arm_2d_location_t tLocation)
{
    arm_2d_tile_t tImage, tMask;
    arm_2d_region_t tTarget = {.tLocation = tLocation, .tSize = tSource.tSize};

    if (!arm_2d_helper_pfb_is_region_being_drawing(ptTile, &tTarget, NULL)) {
        return;
    }
    arm_2d_tile_generate_child(&c_tilePlatformerComboRGB565, &tSource, &tImage, false);
    arm_2d_tile_generate_child(&c_tilePlatformerComboMask, &tSource, &tMask, false);
    arm_2d_tile_copy_with_src_mask_only(&tImage, &tMask, ptTile, &tTarget);
    ARM_2D_OP_WAIT_ASYNC();
}

static void __platformer_draw_combo(user_scene_platformer_t *ptThis,
                                    const arm_2d_tile_t *ptTile,
                                    const arm_2d_region_t *ptRegion)
{
    if (this.tGame.hwCombo < 2u
    || !arm_2d_helper_pfb_is_region_being_drawing(ptTile, ptRegion, NULL)) {
        return;
    }
    arm_2d_location_t tOrigin = ptRegion->tLocation;
    tOrigin.iY += this.chComboPhase == 0u ? 0 : this.chComboPhase == 1u ? 2 : 1;
    __platformer_draw_combo_part(ptTile, (arm_2d_region_t){{0, 12}, {40, 12}},
        (arm_2d_location_t){tOrigin.iX + 7, tOrigin.iY});

    /* Word and count share the generated artwork; no runtime font rendering. */
    uint8_t chGlyphs[4] = {10}; /* Atlas slot 10 is the multiplication x. */
    uint8_t chCount = 1;
    uint16_t hwValue = this.tGame.hwCombo;
    if (hwValue >= 100u) {
        chGlyphs[chCount++] = hwValue / 100u;
    }
    if (hwValue >= 10u) {
        chGlyphs[chCount++] = hwValue / 10u % 10u;
    }
    chGlyphs[chCount++] = hwValue % 10u;
    for (uint_fast8_t n = 0; n < chCount; n++) {
        __platformer_draw_combo_part(ptTile,
            (arm_2d_region_t){{chGlyphs[n] * 9, 0}, {9, 12}},
            (arm_2d_location_t){tOrigin.iX + (54 - chCount * 9) / 2 + n * 9,
                                tOrigin.iY + 13});
    }

    arm_2d_region_t tBar = {
        .tLocation = {ptRegion->tLocation.iX + 11, ptRegion->tLocation.iY + 29},
        .tSize = {32, 1},
    };
    arm_2d_fill_colour(ptTile, &tBar, __RGB(0x56, 0x3C, 0x2A));
    ARM_2D_OP_WAIT_ASYNC();
    tBar.tSize.iWidth = this.chComboBar * 32 / 10;
    arm_2d_fill_colour(ptTile, &tBar, __RGB(0xFF, 0xCB, 0x52));
    ARM_2D_OP_WAIT_ASYNC();
}

static void __platformer_draw_hud(user_scene_platformer_t *ptThis,
                                  const arm_2d_tile_t *ptTile,
                                  arm_2d_region_t *ptHUD,
                                  arm_2d_region_t *ptFPS)
{
    if (!arm_2d_helper_pfb_is_region_being_drawing(ptTile, ptHUD, NULL)) {
        return;
    }
    arm_2d_region_t tLine = *ptHUD;
    tLine.tSize = (arm_2d_size_t){240, 8};
    arm_lcd_text_set_target_framebuffer(ptTile);
    arm_lcd_text_set_font(&ARM_2D_FONT_6x8.use_as__arm_2d_font_t);
    arm_lcd_text_set_display_mode(ARM_2D_DRW_PATN_MODE_COPY);
    arm_lcd_text_set_opacity(255);
    arm_lcd_text_set_colour(__RGB(0x23, 0x45, 0x52), GLCD_COLOR_WHITE);
    if (arm_2d_helper_pfb_is_region_being_drawing(ptTile, &tLine, NULL)) {
        arm_lcd_text_set_draw_region(&tLine);
        arm_lcd_text_location(0, 0);
#if PLATFORMER_SHOW_DEBUG_INFO
        arm_lcd_printf("COOKIES %06lu  G10 R%u H%u G%u",
            (unsigned long)this.tGame.wCookies,
            (unsigned)this.bRawJumpHeld,
            (unsigned)this.tGame.bGlideHeld,
            (unsigned)this.tGame.bGliding);
#else
        arm_lcd_printf("COOKIES %06lu", (unsigned long)this.tGame.wCookies);
#endif
        ARM_2D_OP_WAIT_ASYNC();
    }
#if PLATFORMER_SHOW_DEBUG_INFO
    tLine.tLocation.iY += 8;
    tLine.tSize.iWidth = 240;
    if (arm_2d_helper_pfb_is_region_being_drawing(ptTile, &tLine, NULL)) {
        arm_lcd_text_set_draw_region(&tLine);
        arm_lcd_text_location(0, 0);
        arm_lcd_printf("S%u P%u B%u C%u T%u",
            (unsigned)this.tKeyAudit.sio_changes,
            (unsigned)this.tKeyAudit.pad_changes,
            (unsigned)this.tKeyAudit.sio_seen,
            (unsigned)this.tKeyAudit.pad_seen,
            (unsigned)this.tKeyAudit.window_ms);
        ARM_2D_OP_WAIT_ASYNC();
    }
    tLine.tLocation.iY += 8;
    if (arm_2d_helper_pfb_is_region_being_drawing(ptTile, &tLine, NULL)) {
        arm_lcd_text_set_draw_region(&tLine);
        arm_lcd_text_location(0, 0);
        arm_lcd_printf("E%lu A%u V%u X%02X",
            (unsigned long)(this.tKeyAudit.edges % 1000000u),
            (unsigned)this.tKeyAudit.age_ms,
            (unsigned)this.tKeyAudit.levels,
            (unsigned)this.tKeyAudit.faults);
        ARM_2D_OP_WAIT_ASYNC();
    }
    if (arm_2d_helper_pfb_is_region_being_drawing(ptTile, ptFPS, NULL)) {
        arm_lcd_text_set_draw_region(ptFPS);
        arm_lcd_text_location(0, 0);
        arm_lcd_printf("FPS %03u", (unsigned)this.hwFPS);
        ARM_2D_OP_WAIT_ASYNC();
    }
#else
    (void)ptFPS;
#endif
    arm_lcd_text_set_draw_region(NULL);
}

static
IMPL_PFB_ON_DRAW(__pfb_draw_scene_platformer_handler)
{
    user_scene_platformer_t *ptThis = (user_scene_platformer_t *)pTarget;

    arm_2d_canvas(ptTile, __canvas) {
        arm_2d_region_t tPlayerRegion = {
            .tLocation = {
                .iX = __canvas.tLocation.iX
                    + this.tPlayerLocation.iX
                    - (this.tHeroTile.tRegion.tSize.iWidth
                        - PLATFORMER_WALK_FRAME_WIDTH) / 2,
                .iY = __canvas.tLocation.iY + this.tPlayerLocation.iY
                    + PLATFORMER_PLAYER_DRAW_OFFSET_Y,
            },
            .tSize = this.tHeroTile.tRegion.tSize,
        };
        arm_2d_region_t tGround = {
            .tLocation = {
                .iX = __canvas.tLocation.iX + this.tGround.tLocation.iX,
                .iY = __canvas.tLocation.iY + this.tGround.tLocation.iY,
            },
            .tSize = this.tGround.tSize,
        };
        arm_2d_region_t tMidFarDirtyRegion = {
            .tLocation = {
                .iX = __canvas.tLocation.iX + this.tPlayfield.tLocation.iX,
                .iY = __canvas.tLocation.iY
                    + this.tGround.tLocation.iY
                    - PLATFORMER_MID_FAR_LAYER_HEIGHT,
            },
            .tSize = {
                .iWidth = this.tPlayfield.tSize.iWidth,
                .iHeight = PLATFORMER_MID_FAR_LAYER_HEIGHT,
            },
        };
        arm_2d_region_t tCloudDirtyRegion = {
            .tLocation = {
                .iX = __canvas.tLocation.iX
                    + this.tPlayfield.tLocation.iX,
                .iY = __canvas.tLocation.iY
                    + this.tPlayfield.tLocation.iY
                    + PLATFORMER_CLOUD_LAYER_Y,
            },
            .tSize = {
                .iWidth = this.tPlayfield.tSize.iWidth,
                .iHeight = PLATFORMER_CLOUD_LAYER_HEIGHT,
            },
        };
        arm_2d_region_t tForegroundDirtyRegion = {
            .tLocation = {
                .iX = __canvas.tLocation.iX + this.tPlayfield.tLocation.iX,
                .iY = __canvas.tLocation.iY
                    + this.tGround.tLocation.iY
                    - PLATFORMER_GRASS_FRAME_HEIGHT,
            },
            .tSize = {
                .iWidth = this.tPlayfield.tSize.iWidth,
                .iHeight = PLATFORMER_FOREGROUND_DIRTY_HEIGHT,
            },
        };
        arm_2d_region_t tHUD = {
            .tLocation = {.iX = __canvas.tLocation.iX + 8,
                          .iY = __canvas.tLocation.iY + 8},
            .tSize = {.iWidth = 304, .iHeight = 28},
        };
        /* Only the six score digits need refreshing during normal gameplay. */
        arm_2d_region_t tScoreDirtyRegion = {
            .tLocation = {.iX = tHUD.tLocation.iX + 8 * 6,
                          .iY = tHUD.tLocation.iY},
            .tSize = {.iWidth = (PLATFORMER_SHOW_DEBUG_INFO ? 20 : 6) * 6, .iHeight = 8},
        };
        arm_2d_region_t tFPS = {
            .tLocation = {.iX = __canvas.tLocation.iX + 260,
                          .iY = __canvas.tLocation.iY + 8},
            .tSize = {.iWidth = 54, .iHeight = 8},
        };
        arm_2d_region_t tProfileDirtyRegion = tHUD;
        arm_2d_region_t tCombo = {
            .tLocation = {__canvas.tLocation.iX + __canvas.tSize.iWidth - 62,
                          __canvas.tLocation.iY + 24},
            .tSize = {54, 30},
        };

        arm_2d_helper_dirty_region_update_item(
            &this.tDirtyRegionItems[PLATFORMER_DIRTY_REGION_PLAYER],
            (arm_2d_tile_t *)ptTile,
            &__canvas,
            &tPlayerRegion);
        arm_2d_helper_dirty_region_update_item(
            &this.tDirtyRegionItems[PLATFORMER_DIRTY_REGION_CLOUD],
            (arm_2d_tile_t *)ptTile,
            &__canvas,
            &tCloudDirtyRegion);
        arm_2d_helper_dirty_region_update_item(
            &this.tDirtyRegionItems[PLATFORMER_DIRTY_REGION_MID_FAR],
            (arm_2d_tile_t *)ptTile,
            &__canvas,
            &tMidFarDirtyRegion);
        arm_2d_helper_dirty_region_update_item(
            &this.tDirtyRegionItems[PLATFORMER_DIRTY_REGION_FOREGROUND],
            (arm_2d_tile_t *)ptTile,
            &__canvas,
            &tForegroundDirtyRegion);
        for (uint_fast8_t n = 0; n < dimof(this.tCourseRegions); n++) {
            /* Update empty slots once per frame too: the helper's 8-bit
             * lifecycle must not alias after a slot is hidden for 256 frames. */
            if (!bIsNewFrame
            &&  !(this.hwCourseRegionMask & (uint16_t)(1u << n))) {
                continue;
            }
            arm_2d_region_t tCourse = this.tCourseRegions[n];
            tCourse.tLocation.iX += __canvas.tLocation.iX;
            tCourse.tLocation.iY += __canvas.tLocation.iY;
            /* An empty rectangle retires the previous visible area once. */
            arm_2d_helper_dirty_region_update_item(
                &this.tDirtyRegionItems[PLATFORMER_DIRTY_REGION_COURSE + n],
                (arm_2d_tile_t *)ptTile, &__canvas, &tCourse);
        }
        arm_2d_helper_dirty_region_update_item(
            &this.tDirtyRegionItems[PLATFORMER_DIRTY_REGION_HUD],
            (arm_2d_tile_t *)ptTile, &__canvas, &tScoreDirtyRegion);
        arm_2d_helper_dirty_region_update_item(
            &this.tDirtyRegionItems[PLATFORMER_DIRTY_REGION_FPS],
            (arm_2d_tile_t *)ptTile, &__canvas, &tProfileDirtyRegion);

        /* Dirty items above must also run during PFB dry runs. Rasterization
         * below needs a real target; no drawing operation feeds these items. */
        arm_2d_helper_dirty_region_update_item(
            &this.tDirtyRegionItems[PLATFORMER_DIRTY_REGION_COMBO],
            (arm_2d_tile_t *)ptTile, &__canvas, &tCombo);
        if (!arm_2d_helper_pfb_is_region_being_drawing(ptTile, NULL, NULL)) {
            return arm_fsm_rt_cpl;
        }
        this.bFrameDrawn = true;

        uint32_t wDrawStartUs = platform_display_profile_timestamp();
        __platformer_draw_parallax_layer(
                                     ptThis,
                                     ptTile,
                                     __canvas.tLocation,
                                     &s_tCloudCacheTile,
                                     PLATFORMER_MID_FAR_CACHE_WIDTH,
                                     PLATFORMER_CLOUD_LAYER_HEIGHT,
                                     this.tPlayfield.tLocation.iY
                                        + PLATFORMER_CLOUD_LAYER_Y,
                                     0);
        platform_display_profile_section(DISPLAY_PROFILE_CLOUD, wDrawStartUs);

        wDrawStartUs = platform_display_profile_timestamp();
        __platformer_draw_mid_far_layer(ptThis,
                                        ptTile,
                                        __canvas.tLocation);
        platform_display_profile_section(DISPLAY_PROFILE_FAR, wDrawStartUs);

        wDrawStartUs = platform_display_profile_timestamp();
        if (arm_2d_helper_pfb_is_region_being_drawing(ptTile, &tGround, NULL)) {
            arm_2d_region_t tGrass = tGround;

            arm_2d_fill_colour(ptTile, &tGround, __RGB(0x56, 0x3C, 0x2A));
            ARM_2D_OP_WAIT_ASYNC();
            tGrass.tSize.iHeight = 6;
            arm_2d_fill_colour(ptTile, &tGrass, __RGB(0x4E, 0xAE, 0x12));
            ARM_2D_OP_WAIT_ASYNC();
        }

        __platformer_draw_foreground(ptThis,
                                     ptTile,
                                     __canvas.tLocation,
                                     PLATFORMER_FOREGROUND_STONE);
        __platformer_draw_course(ptThis, ptTile, __canvas.tLocation,
                                  PLATFORMER_GAME_PLATFORM);

        if (arm_2d_helper_pfb_is_region_being_drawing(ptTile,
                                               &tPlayerRegion,
                                               NULL)) {
            arm_2d_tile_copy_with_src_mask_only(&this.tHeroTile,
                                                &this.tHeroMask,
                                                ptTile,
                                                &tPlayerRegion);
            ARM_2D_OP_WAIT_ASYNC();
        }

        __platformer_draw_foreground(ptThis,
                                     ptTile,
                                     __canvas.tLocation,
                                     PLATFORMER_FOREGROUND_GRASS);
        __platformer_draw_course(ptThis, ptTile, __canvas.tLocation,
                                  PLATFORMER_GAME_COOKIE);
        platform_display_profile_section(DISPLAY_PROFILE_SPRITES, wDrawStartUs);
        __platformer_draw_hud(ptThis, ptTile, &tHUD, &tFPS);
        __platformer_draw_combo(ptThis, ptTile, &tCombo);
    }

    return arm_fsm_rt_cpl;
}

ARM_NONNULL(1)
user_scene_platformer_t *__arm_2d_scene_platformer_init(
                                        arm_2d_scene_player_t *ptDispAdapter,
                                        user_scene_platformer_t *ptThis)
{
    bool bUserAllocated = false;
    arm_2d_region_t tScreen;
    int16_t iPlayerStartX;
    int16_t iGroundY;

    assert(NULL != ptDispAdapter);

    tScreen = arm_2d_helper_pfb_get_display_area(
                &ptDispAdapter->use_as__arm_2d_helper_pfb_t);
    iPlayerStartX = (tScreen.tSize.iWidth
                    - PLATFORMER_WALK_FRAME_WIDTH) / 3;
    iGroundY = tScreen.tSize.iHeight - PLATFORMER_GROUND_HEIGHT;

    if (NULL == ptThis) {
        ptThis = (user_scene_platformer_t *)
                    __arm_2d_allocate_scratch_memory(
                        sizeof(user_scene_platformer_t),
                        __alignof__(user_scene_platformer_t),
                        ARM_2D_MEM_TYPE_UNSPECIFIED);
        assert(NULL != ptThis);
        if (NULL == ptThis) {
            return NULL;
        }
    } else {
        bUserAllocated = true;
        memset(ptThis, 0, sizeof(user_scene_platformer_t));
    }

    *ptThis = (user_scene_platformer_t){
        .use_as__arm_2d_scene_t = {
            .tCanvas = {__RGB(0x80, 0xCB, 0xEE)},
            .fnOnLoad = &__on_scene_platformer_load,
            .fnScene = &__pfb_draw_scene_platformer_handler,
            .fnOnFrameStart = &__on_scene_platformer_frame_start,
            .fnBeforeSwitchOut = &__before_scene_platformer_switching_out,
            .fnOnFrameCPL = &__on_scene_platformer_frame_complete,
            .fnDepose = &__on_scene_platformer_depose,
            .bUseDirtyRegionHelper = true,
        },
        .bUserAllocated = bUserAllocated,
        .tPlayfield = tScreen,
        .tGround = {
            .tLocation = {
                .iX = tScreen.tLocation.iX,
                .iY = tScreen.tLocation.iY + iGroundY,
            },
            .tSize = {
                .iWidth = tScreen.tSize.iWidth,
                .iHeight = PLATFORMER_GROUND_HEIGHT,
            },
        },
        .tPlayerStartLocation = {
            .iX = tScreen.tLocation.iX + iPlayerStartX,
            .iY = tScreen.tLocation.iY + iGroundY
                - PLATFORMER_WALK_FRAME_HEIGHT,
        },
        .tPlayerLocation = {
            .iX = tScreen.tLocation.iX + iPlayerStartX,
            .iY = tScreen.tLocation.iY + iGroundY
                - PLATFORMER_WALK_FRAME_HEIGHT,
        },
    };

    qmi8658_motion_set_bounds(PLATFORMER_MOTION_POSITION_MAX, 0);
    __platformer_reset_player(ptThis);
    __platformer_reset_foreground(ptThis);

    arm_2d_scene_player_append_scenes(ptDispAdapter,
                                      &this.use_as__arm_2d_scene_t,
                                      1);

    return ptThis;
}

#if defined(__clang__)
#   pragma clang diagnostic pop
#elif __IS_COMPILER_GCC__
#   pragma GCC diagnostic pop
#endif

#endif
